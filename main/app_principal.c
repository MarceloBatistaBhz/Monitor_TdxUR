/*
 * 22_umidade_temp - Data logger de temperatura e umidade (ensaio de infiltracao)
 * Placa: Waveshare ESP32-P4-WIFI6-Touch-LCD-7B + sensor Adafruit SHT40 (I2C 0x44)
 *
 * ============ MARCO 3b - GUI de coleta (completa) ============
 * Display + touch + LVGL. SHT40 no barramento compartilhado do touch (GPIO7/8).
 *   - 4 cards ao vivo + grafico Td/UR
 *   - botao "Marcar evento" (e tecla 'm'): grava linha evento=1 no instante exato
 *   - botao "Iniciar/parar log": abre/fecha um CSV por ensaio (SD ou LittleFS)
 *   - baseline: media do Td nos primeiros minutos; alerta quando Td sobe > limiar
 * ============================================================
 */
#include <stdio.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "bsp/esp-bsp.h"

#include "config_projeto.h"
#include "sht4x.h"
#include "psicrometria.h"
#include "registro.h"
#include "ui.h"

static const char *TAG = "app";

static i2c_master_bus_handle_t s_bus;    /* barramento compartilhado do BSP (touch + SHT40) */
static QueueHandle_t     s_fila;         /* aquisicao/marcador -> logger */
static SemaphoreHandle_t s_mtx;          /* protege o estado abaixo */
static amostra_t         s_ultima;
static bool              s_tem_ultima;

/* Estado de gravacao + baseline (protegido por s_mtx) */
static bool     s_log_ativo;             /* espelha "esta gravando" */
static bool     s_base_ativo;            /* capturando baseline */
static int64_t  s_base_t0;               /* inicio da captura (s desde o boot) */
static double   s_base_soma;             /* soma de Td na janela */
static int      s_base_n;
static bool     s_base_pronto;           /* baseline calculado */
static float    s_base_td;               /* valor do baseline (C) */

static void task_aquisicao(void *arg)
{
    sht4x_t sensor;
    ESP_ERROR_CHECK(sht4x_add(s_bus, SHT40_ENDERECO, I2C_CLOCK_HZ, &sensor));

    uint32_t serial = 0;
    if (sht4x_ler_serial(&sensor, &serial) == ESP_OK) {
        ESP_LOGI(TAG, "SHT40 OK no 0x%02X, serial=0x%08lX", SHT40_ENDERECO, (unsigned long)serial);
    } else {
        ESP_LOGW(TAG, "Nao li o serial do SHT40 - confira SDA/SCL/3V3/GND e o endereco");
    }
    ESP_LOGI(TAG, "seg_boot ; temp_C ; UR_%% ; Td_C ; AH_g/m3   (tecla '%c' = marcar evento)",
             TECLA_EVENTO);

    const TickType_t periodo = pdMS_TO_TICKS(INTERVALO_AMOSTRAGEM_S * 1000);
    TickType_t proximo = xTaskGetTickCount();
    for (;;) {
        float t, ur;
        esp_err_t err = sht4x_medir(&sensor, &t, &ur);
        if (err == ESP_OK) {
            amostra_t a = {
                .seg_boot = esp_timer_get_time() / 1000000,
                .temp_c = t, .ur_pct = ur,
                .td_c = psicro_ponto_orvalho(t, ur),
                .ah_gm3 = psicro_umidade_absoluta(t, ur),
                .evento = 0,
            };
            ESP_LOGI(TAG, "%8lld ; %6.2f ; %5.2f ; %6.2f ; %5.2f",
                     (long long)a.seg_boot, a.temp_c, a.ur_pct, a.td_c, a.ah_gm3);

            bool logando, pronto;
            float base_td;
            int base_restante = 0;
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_ultima = a;
            s_tem_ultima = true;
            logando = s_log_ativo;
            if (logando && s_base_ativo) {                     /* acumula o baseline */
                s_base_soma += a.td_c;
                s_base_n++;
                if ((a.seg_boot - s_base_t0) >= BASELINE_SEGUNDOS && s_base_n > 0) {
                    s_base_td = (float)(s_base_soma / s_base_n);
                    s_base_pronto = true;
                    s_base_ativo = false;
                }
            }
            if (s_base_ativo) {                                /* ainda capturando: quanto falta */
                base_restante = (int)(BASELINE_SEGUNDOS - (a.seg_boot - s_base_t0));
                if (base_restante < 0) base_restante = 0;
            }
            pronto = s_base_pronto;
            base_td = s_base_td;
            xSemaphoreGive(s_mtx);

            ui_nova_amostra(&a);
            if (logando) {
                ui_set_baseline(pronto, base_td, base_restante);
                ui_set_alerta(pronto && (a.td_c > base_td + LIMIAR_ALERTA_C));
            }
            if (s_fila != NULL && xQueueSend(s_fila, &a, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Fila cheia - amostra nao gravada");
            }
        } else {
            ESP_LOGE(TAG, "Falha na leitura: %s", esp_err_to_name(err));
        }
        xTaskDelayUntil(&proximo, periodo);
    }
}

/* Marca um evento AGORA (instante exato). So faz sentido enquanto grava. */
static void marcar_evento_agora(void)
{
    amostra_t ev;
    bool tem, logando;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    logando = s_log_ativo;
    tem = s_tem_ultima;
    ev = s_ultima;
    xSemaphoreGive(s_mtx);

    if (!logando) {
        ESP_LOGW(TAG, "Inicie o log antes de marcar um evento");
        return;
    }
    if (!tem) {
        ESP_LOGW(TAG, "Evento ignorado: ainda sem leitura do sensor");
        return;
    }
    ev.seg_boot = esp_timer_get_time() / 1000000;
    ev.evento = 1;
    if (s_fila != NULL && xQueueSend(s_fila, &ev, 0) == pdTRUE) {
        ESP_LOGW(TAG, ">>> EVENTO marcado em %lld s (Td=%.2f C) <<<",
                 (long long)ev.seg_boot, ev.td_c);
        ui_evento_marcado();
    } else {
        ESP_LOGW(TAG, "Evento NAO gravado (sem midia ou fila cheia)");
    }
}

/* Botao "Iniciar/parar log": abre/fecha um arquivo por ensaio + baseline */
static void app_toggle_log(void)
{
    if (registro_gravando()) {
        registro_fechar();
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_log_ativo = false;
        s_base_ativo = false;
        s_base_pronto = false;
        xSemaphoreGive(s_mtx);
        ui_set_gravando(false, NULL);
        ui_set_baseline(false, 0.0f, 0);
        ui_set_alerta(false);
    } else {
        char nome[40];
        if (registro_abrir(nome, sizeof nome) == ESP_OK) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_log_ativo = true;
            s_base_ativo = true;
            s_base_pronto = false;
            s_base_soma = 0.0;
            s_base_n = 0;
            s_base_t0 = esp_timer_get_time() / 1000000;
            xSemaphoreGive(s_mtx);
            ui_set_gravando(true, nome);
            ui_set_baseline(false, 0.0f, BASELINE_SEGUNDOS);
            ui_set_alerta(false);
        } else {
            ESP_LOGE(TAG, "Nao consegui iniciar o log (sem cartao/midia?)");
        }
    }
}

/* Le teclas no serial; a tecla TECLA_EVENTO tambem marca um evento */
static void task_serial(void *arg)
{
    for (;;) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (tolower(c) == TECLA_EVENTO) {
            marcar_evento_agora();
        }
    }
}

/* Callbacks que a UI dispara no toque */
static const ui_callbacks_t s_ui_cb = {
    .on_marcar_evento = marcar_evento_agora,
    .on_toggle_log    = app_toggle_log,
};

void app_main(void)
{
    ESP_LOGI(TAG, "== Marco 3c: launcher (Coletar / Visualizar) ==");

    s_mtx = xSemaphoreCreateMutex();

    /* Leitura de teclas no console (marcador), sem atrapalhar o log de saida */
    setvbuf(stdin, NULL, _IONBF, 0);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    uart_vfs_dev_use_driver(UART_NUM_0);

    /* 1) Display + touch + LVGL (tambem inicializa o I2C do touch em GPIO7/8) */
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = { .buff_dma = true, .buff_spiram = false, .sw_rotate = true },
    };
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();
    if (disp != NULL) {
        bsp_display_rotate(disp, LV_DISPLAY_ROTATION_0);
    }

    /* 2) SHT40 no MESMO barramento do touch (compartilhado, thread-safe) */
    bsp_i2c_init();
    s_bus = bsp_i2c_get_handle();

    /* 3) Tela */
    bsp_display_lock(0);
    ui_iniciar();
    bsp_display_unlock();
    ui_set_callbacks(&s_ui_cb);

    /* 4) Registro: monta a midia (SD/LittleFS) e sobe o logger. O arquivo so
     *    abre quando o usuario tocar "Iniciar log". */
    if (registro_iniciar(&s_fila) != ESP_OK) {
        ESP_LOGW(TAG, "Sem midia de gravacao - a UI funciona, mas nao havera log");
    }

    /* 5) Tasks: aquisicao (core 1) e leitor de teclas (core 0) */
    xTaskCreatePinnedToCore(task_aquisicao, "aquisicao", STACK_AQUISICAO, NULL,
                            PRIO_AQUISICAO, NULL, CORE_AQUISICAO);
    xTaskCreatePinnedToCore(task_serial, "serial", STACK_SERIAL, NULL,
                            PRIO_SERIAL, NULL, CORE_SERIAL);
}
