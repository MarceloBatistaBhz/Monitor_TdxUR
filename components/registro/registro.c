#include "registro.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>         /* strncasecmp/strcasecmp */
#include <stdlib.h>          /* qsort */
#include <unistd.h>          /* fsync */
#include <sys/stat.h>        /* stat  */
#include <dirent.h>          /* opendir/readdir */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_littlefs.h"
#if SOC_SDMMC_IO_POWER_EXTERNAL
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

static const char *TAG = "registro";

/* ---- Cartao SD (especifico desta placa Waveshare ESP32-P4) ---- */
#define SD_PONTO_MONTAGEM   "/sdcard"
#define SD_LDO_CANAL        4       /* VDD do SD sai do LDO interno canal 4 (ver 04_sdmmc) */
#define SD_PIN_CLK          43
#define SD_PIN_CMD          44
#define SD_PIN_D0           39
#define SD_PIN_D1           40
#define SD_PIN_D2           41
#define SD_PIN_D3           42

/* ---- LittleFS (fallback na flash interna quando NAO ha cartao) ---- */
#define LFS_PONTO_MONTAGEM  "/littlefs"
#define LFS_PARTICAO        "littlefs"   /* label da particao em partitions.csv */

/* ---- Logger ---- */
#define FILA_PROFUNDIDADE   8
#define STACK_LOGGER        4096
#define PRIO_LOGGER         4
#define CORE_LOGGER         0

static QueueHandle_t     s_fila;
static FILE             *s_arq;          /* NULL = nao esta gravando */
static char              s_caminho[40];
static char              s_base[16];     /* "/sdcard" ou "/littlefs" */
static SemaphoreHandle_t s_arq_mtx;      /* protege s_arq (logger x abrir/fechar) */

static esp_err_t montar_sd(sdmmc_card_t **card)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
#if SOC_SDMMC_IO_POWER_EXTERNAL
    sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = SD_LDO_CANAL };
    sd_pwr_ctrl_handle_t pwr = NULL;
    esp_err_t lerr = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &pwr);
    if (lerr != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao ligar o LDO do SD: %s", esp_err_to_name(lerr));
        return lerr;
    }
    host.pwr_ctrl_handle = pwr;
#endif
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
#if SOC_SDMMC_USE_GPIO_MATRIX
    slot.clk = SD_PIN_CLK;  slot.cmd = SD_PIN_CMD;
    slot.d0  = SD_PIN_D0;   slot.d1  = SD_PIN_D1;
    slot.d2  = SD_PIN_D2;   slot.d3  = SD_PIN_D3;
#endif
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mnt = {
        .format_if_mount_failed = false,   /* NUNCA formatar o cartao do usuario */
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_PONTO_MONTAGEM, &host, &slot, &mnt, card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD nao montou (%s)", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t montar_littlefs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = LFS_PONTO_MONTAGEM,
        .partition_label = LFS_PARTICAO,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao montar LittleFS: %s", esp_err_to_name(err));
    }
    return err;
}

/* Acha o primeiro nome livre na 'base': base/log_0001.csv, ... (nao sobrescreve) */
static void escolher_nome(const char *base, char *out, size_t n)
{
    for (int i = 1; i <= 9999; i++) {
        snprintf(out, n, "%s/log_%04d.csv", base, i);
        struct stat st;
        if (stat(out, &st) != 0) {
            return;
        }
    }
}

/* "hh:mm:ss" a partir de segundos desde o boot (relogio corrido) */
static void formata_hhmmss(int64_t seg, char *out, size_t n)
{
    int h = (int)((seg / 3600) % 100);
    int m = (int)((seg / 60) % 60);
    int s = (int)(seg % 60);
    snprintf(out, n, "%02d:%02d:%02d", h, m, s);
}

/* Task do logger: dona do FILE*. So grava se ha arquivo aberto. */
static void task_logger(void *arg)
{
    amostra_t a;
    for (;;) {
        if (xQueueReceive(s_fila, &a, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        xSemaphoreTake(s_arq_mtx, portMAX_DELAY);
        if (s_arq != NULL) {
            char hhmmss[12];
            formata_hhmmss(a.seg_boot, hhmmss, sizeof hhmmss);
            /* separador ';' e decimal '.' (troque para ',' se for abrir direto no Excel pt-BR) */
            fprintf(s_arq, "%lld;%s;%.2f;%.2f;%.2f;%.2f;%u\n",
                    (long long)a.seg_boot, hhmmss,
                    a.temp_c, a.ur_pct, a.td_c, a.ah_gm3, a.evento);
            fflush(s_arq);
            fsync(fileno(s_arq));    /* seguro contra queda de energia */
        }
        xSemaphoreGive(s_arq_mtx);
    }
}

esp_err_t registro_iniciar(QueueHandle_t *fila_out)
{
    *fila_out = NULL;
    s_arq_mtx = xSemaphoreCreateMutex();

    /* Escolhe a midia: 1o o SD; se nao houver, o LittleFS interno */
    sdmmc_card_t *card = NULL;
    if (montar_sd(&card) == ESP_OK) {
        sdmmc_card_print_info(stdout, card);
        strcpy(s_base, SD_PONTO_MONTAGEM);
        ESP_LOGI(TAG, "Armazenamento: cartao SD");
    } else if (montar_littlefs() == ESP_OK) {
        strcpy(s_base, LFS_PONTO_MONTAGEM);
        ESP_LOGW(TAG, "Sem cartao SD -> usando LittleFS interno (fallback)");
    } else {
        ESP_LOGE(TAG, "Sem SD e sem LittleFS - nao havera gravacao");
        return ESP_FAIL;
    }

    s_fila = xQueueCreate(FILA_PROFUNDIDADE, sizeof(amostra_t));
    if (s_fila == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(task_logger, "logger", STACK_LOGGER, NULL,
                                PRIO_LOGGER, NULL, CORE_LOGGER) != pdPASS) {
        vQueueDelete(s_fila);
        s_fila = NULL;
        return ESP_ERR_NO_MEM;
    }

    *fila_out = s_fila;
    return ESP_OK;
}

esp_err_t registro_abrir(char *nome_out, size_t n)
{
    esp_err_t r = ESP_FAIL;
    xSemaphoreTake(s_arq_mtx, portMAX_DELAY);
    if (s_arq != NULL) {
        ESP_LOGW(TAG, "registro_abrir: ja ha um log aberto");
    } else if (s_base[0] == '\0') {
        ESP_LOGE(TAG, "registro_abrir: sem midia montada");
    } else {
        escolher_nome(s_base, s_caminho, sizeof s_caminho);
        s_arq = fopen(s_caminho, "w");
        if (s_arq != NULL) {
            fprintf(s_arq, "seg_boot;hhmmss;temp_C;ur_pct;td_C;ah_gm3;evento\n");
            fflush(s_arq);
            fsync(fileno(s_arq));
            ESP_LOGI(TAG, "Iniciado log em %s", s_caminho);
            if (nome_out != NULL && n > 0) {
                strncpy(nome_out, s_caminho, n - 1);
                nome_out[n - 1] = '\0';
            }
            r = ESP_OK;
        } else {
            ESP_LOGE(TAG, "Nao consegui criar %s", s_caminho);
        }
    }
    xSemaphoreGive(s_arq_mtx);
    return r;
}

void registro_fechar(void)
{
    xSemaphoreTake(s_arq_mtx, portMAX_DELAY);
    if (s_arq != NULL) {
        fclose(s_arq);
        s_arq = NULL;
        ESP_LOGI(TAG, "Log fechado (%s)", s_caminho);
    }
    xSemaphoreGive(s_arq_mtx);
}

bool registro_gravando(void)
{
    xSemaphoreTake(s_arq_mtx, portMAX_DELAY);
    bool r = (s_arq != NULL);
    xSemaphoreGive(s_arq_mtx);
    return r;
}

const char *registro_base(void)
{
    return s_base;
}

esp_err_t registro_capacidade(uint64_t *total_bytes, uint64_t *livre_bytes)
{
    if (total_bytes) *total_bytes = 0;
    if (livre_bytes) *livre_bytes = 0;
    if (s_base[0] == '\0') {
        return ESP_ERR_INVALID_STATE;            /* nenhuma midia montada */
    }
    if (strcmp(s_base, SD_PONTO_MONTAGEM) == 0) {
        return esp_vfs_fat_info(SD_PONTO_MONTAGEM, total_bytes, livre_bytes);
    }
    /* LittleFS: devolve total e USADO -> converto para livre */
    size_t tot = 0, usado = 0;
    esp_err_t err = esp_littlefs_info(LFS_PARTICAO, &tot, &usado);
    if (err == ESP_OK) {
        if (total_bytes) *total_bytes = tot;
        if (livre_bytes) *livre_bytes = (tot > usado) ? (tot - usado) : 0;
    }
    return err;
}

static int cmp_nome(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

int registro_listar(char nomes[][48], int max)
{
    if (s_base[0] == '\0') {
        return 0;
    }
    DIR *d = opendir(s_base);
    if (d == NULL) {
        ESP_LOGW(TAG, "Nao consegui abrir %s para listar", s_base);
        return 0;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        /* aceita "log_*.csv" - case-insensitive: o FAT devolve nomes 8.3 em MAIUSCULAS */
        size_t dl = strlen(e->d_name);
        bool eh_log = (strncasecmp(e->d_name, "log_", 4) == 0);
        bool eh_csv = (dl >= 4 && strcasecmp(e->d_name + dl - 4, ".csv") == 0);
        if (eh_log && eh_csv) {
            snprintf(nomes[n], sizeof nomes[0], "%s/%.24s", s_base, e->d_name);
            n++;
        }
    }
    closedir(d);
    qsort(nomes, n, sizeof nomes[0], cmp_nome);   /* ordem alfabetica = cronologica */
    return n;
}
