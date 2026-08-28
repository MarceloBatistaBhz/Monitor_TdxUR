#include "sht4x.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Comandos do SHT4x (1 byte cada) */
#define CMD_MEDIR_ALTA_PREC   0xFD   /* mede T e UR em alta precisao (~8.3 ms) */
#define CMD_LER_SERIAL        0x89   /* le numero de serie */

/* Espera apos o comando de medicao (datasheet: max 8.3 ms) -> 10 ms com folga */
#define ESPERA_MEDICAO_MS     10
/* Espera do comando de serial (~1 ms) -> 5 ms com folga */
#define ESPERA_SERIAL_MS      5

/* Timeout das transacoes I2C, em ms */
#define I2C_TIMEOUT_MS        1000

/* CRC-8 do SHT4x: polinomio 0x31, valor inicial 0xFF. Confere cada par de bytes. */
static uint8_t crc8(const uint8_t *dados, int n)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < n; i++) {
        crc ^= dados[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

esp_err_t sht4x_add(i2c_master_bus_handle_t bus, uint8_t addr, uint32_t scl_hz, sht4x_t *out)
{
    if (bus == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = scl_hz,
    };
    return i2c_master_bus_add_device(bus, &dev_cfg, &out->dev);
}

/*
 * Envia 1 byte de comando, ESPERA soltando o barramento, e le 'n' bytes.
 * Importante NAO usar i2c_master_transmit_receive aqui: ele faria um repeated-start
 * e seguraria o barramento durante os 10 ms de medicao. Fazendo transmit -> delay ->
 * receive, o barramento fica livre no meio-tempo (util quando o touch dividir o mesmo bus).
 */
static esp_err_t comando_e_leitura(sht4x_t *s, uint8_t cmd, uint32_t espera_ms,
                                   uint8_t *buf, size_t n)
{
    esp_err_t err = i2c_master_transmit(s->dev, &cmd, 1, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(espera_ms));
    return i2c_master_receive(s->dev, buf, n, I2C_TIMEOUT_MS);
}

esp_err_t sht4x_medir(sht4x_t *s, float *temp_c, float *ur_pct)
{
    if (s == NULL || temp_c == NULL || ur_pct == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t r[6];
    esp_err_t err = comando_e_leitura(s, CMD_MEDIR_ALTA_PREC, ESPERA_MEDICAO_MS, r, sizeof r);
    if (err != ESP_OK) {
        return err;
    }
    /* Confere o CRC de cada par (temperatura e umidade) */
    if (crc8(&r[0], 2) != r[2] || crc8(&r[3], 2) != r[5]) {
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t t_raw  = (uint16_t)((r[0] << 8) | r[1]);
    uint16_t rh_raw = (uint16_t)((r[3] << 8) | r[4]);

    /* Conversoes do datasheet do SHT4x */
    *temp_c = -45.0f + 175.0f * ((float)t_raw / 65535.0f);
    float rh = -6.0f + 125.0f * ((float)rh_raw / 65535.0f);
    if (rh < 0.0f)   rh = 0.0f;        /* a formula pode extrapolar um pouco nas pontas */
    if (rh > 100.0f) rh = 100.0f;
    *ur_pct = rh;
    return ESP_OK;
}

esp_err_t sht4x_ler_serial(sht4x_t *s, uint32_t *serial)
{
    if (s == NULL || serial == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t r[6];
    esp_err_t err = comando_e_leitura(s, CMD_LER_SERIAL, ESPERA_SERIAL_MS, r, sizeof r);
    if (err != ESP_OK) {
        return err;
    }
    if (crc8(&r[0], 2) != r[2] || crc8(&r[3], 2) != r[5]) {
        return ESP_ERR_INVALID_CRC;
    }
    *serial = ((uint32_t)r[0] << 24) | ((uint32_t)r[1] << 16) |
              ((uint32_t)r[3] << 8)  |  (uint32_t)r[4];
    return ESP_OK;
}
