#include "bateria.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define BAT_GPIO        20         /* interno; divisor 1/3 (R92 200k / R93 100k) no hardware */
#define BAT_DIVISOR     3.0f       /* Vbat = Vadc * (200k+100k)/100k = Vadc * 3 */
#define BAT_AMOSTRAS    16         /* media p/ reduzir ruido */

static const char *TAG = "bateria";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t          s_cali;
static adc_channel_t              s_canal;
static adc_unit_t                 s_unidade;
static bool                       s_ok;
static bool                       s_tem_cali;

void bateria_iniciar(void)
{
    /* Deixa o IDF resolver unidade+canal a partir do GPIO (GPIO20 cai no ADC1) */
    esp_err_t err = adc_oneshot_io_to_channel(BAT_GPIO, &s_unidade, &s_canal);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d nao mapeia p/ ADC: %s", BAT_GPIO, esp_err_to_name(err));
        return;
    }

    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = s_unidade };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit falhou");
        return;
    }

    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(s_adc, s_canal, &ccfg);

    /* Calibracao (curve fitting no ESP32-P4): raw -> mV */
    adc_cali_curve_fitting_config_t cal = {
        .unit_id  = s_unidade,
        .chan     = s_canal,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_tem_cali = (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) == ESP_OK);

    s_ok = true;
    ESP_LOGI(TAG, "Bateria: ADC unit%d canal%d no GPIO%d (divisor x%.0f, cali=%d)",
             (int)s_unidade + 1, (int)s_canal, BAT_GPIO, (double)BAT_DIVISOR, s_tem_cali);
}

float bateria_ler_v(void)
{
    if (!s_ok) {
        return -1.0f;
    }
    int acc = 0, n = 0;
    for (int i = 0; i < BAT_AMOSTRAS; i++) {
        int raw;
        if (adc_oneshot_read(s_adc, s_canal, &raw) == ESP_OK) {
            acc += raw;
            n++;
        }
    }
    if (n == 0) {
        return -1.0f;
    }
    int raw_med = acc / n;

    float v_pino;
    if (s_tem_cali) {
        int mv = 0;
        adc_cali_raw_to_voltage(s_cali, raw_med, &mv);
        v_pino = mv / 1000.0f;
    } else {
        /* fallback sem calibracao: fundo de escala ~3.3 V p/ DB_12, 12 bits */
        v_pino = (raw_med / 4095.0f) * 3.3f;
    }
    return v_pino * BAT_DIVISOR;
}
