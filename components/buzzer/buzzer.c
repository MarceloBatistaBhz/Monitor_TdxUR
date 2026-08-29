#include "buzzer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BUZZER_GPIO      GPIO_NUM_5      /* conector 1, testado OK, nao-strapping */
#define BEEP_ON_MS       500             /* 0,5 s ligado */
#define BEEP_OFF_MS      1000            /* 1,0 s desligado */
#define BEEP_TOTAL_MS    15000           /* alarme sonoro dura 15 s */
#define BEEP_CICLOS      (BEEP_TOTAL_MS / (BEEP_ON_MS + BEEP_OFF_MS))   /* 10 apitos */

static const char *TAG = "buzzer";
static TaskHandle_t s_task;
static volatile bool s_rodando;    /* uma sequencia de 15 s esta tocando */
static bool s_alarme_anterior;     /* p/ detectar a borda de subida do alarme */

/* Toca a sequencia de 15 s a cada disparo; fora disso fica bloqueada esperando. */
static void buzzer_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);      /* espera uma borda de subida */
        s_rodando = true;
        for (int i = 0; i < BEEP_CICLOS; i++) {
            gpio_set_level(BUZZER_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(BEEP_ON_MS));
            gpio_set_level(BUZZER_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(BEEP_OFF_MS));
        }
        s_rodando = false;
    }
}

void buzzer_iniciar(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_drive_capability(BUZZER_GPIO, GPIO_DRIVE_CAP_3));  /* driver mais forte do pino */
    gpio_set_level(BUZZER_GPIO, 0);    /* comeca desligado */

    xTaskCreatePinnedToCore(buzzer_task, "buzzer", 2048, NULL, 4, &s_task, 0);
    ESP_LOGI(TAG, "Buzzer no GPIO%d (drive cap 3): alarme 15 s, 0,5 s on / 1,0 s off", BUZZER_GPIO);
}

void buzzer_set_alarme(bool alarme_ativo)
{
    /* Dispara SO na borda de subida e SO se nao houver sequencia tocando: enquanto o
     * alarme seguir armado nao reapita; volta a apitar apenas apos desarmar e armar de novo. */
    if (alarme_ativo && !s_alarme_anterior && !s_rodando && s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
    s_alarme_anterior = alarme_ativo;
}
