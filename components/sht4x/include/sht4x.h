/*
 * Driver minimo do Sensirion SHT4x (SHT40/41/45) usando a API nova do I2C
 * (driver/i2c_master.h). Protocolo bem simples: manda 1 byte de comando,
 * espera a medicao, le 6 bytes (T[MSB,LSB,CRC] + UR[MSB,LSB,CRC]) e confere o CRC-8.
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Endereco I2C padrao do SHT40 (Adafruit STEMMA QT) */
#define SHT4X_ADDR_PADRAO   0x44

/* Handle de um sensor ja "pendurado" num barramento I2C existente */
typedef struct {
    i2c_master_dev_handle_t dev;   /* device handle da API i2c_master */
} sht4x_t;

/**
 * @brief Adiciona um SHT4x a um barramento I2C JA criado.
 *
 * Nao cria barramento: recebe um bus handle (de i2c_new_master_bus, ou no futuro
 * de bsp_i2c_get_handle) e registra o dispositivo. Assim o mesmo sensor funciona
 * tanto num barramento dedicado quanto compartilhado com o touch.
 *
 * @param bus     handle do barramento I2C mestre
 * @param addr    endereco 7-bit (use SHT4X_ADDR_PADRAO)
 * @param scl_hz  clock do dispositivo em Hz (ex.: 100000 no bring-up, 400000 depois)
 * @param out     preenchido na saida
 */
esp_err_t sht4x_add(i2c_master_bus_handle_t bus, uint8_t addr, uint32_t scl_hz, sht4x_t *out);

/**
 * @brief Mede temperatura (C) e umidade relativa (%) em alta precisao.
 *
 * Sequencia: comando 0xFD -> espera ~10 ms -> le 6 bytes -> confere CRC -> converte.
 * Retorna ESP_ERR_INVALID_CRC se algum CRC nao bater.
 */
esp_err_t sht4x_medir(sht4x_t *s, float *temp_c, float *ur_pct);

/**
 * @brief Le o numero de serie de 32 bits (bom para confirmar comunicacao no bring-up).
 */
esp_err_t sht4x_ler_serial(sht4x_t *s, uint32_t *serial);

#ifdef __cplusplus
}
#endif
