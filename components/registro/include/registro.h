/*
 * Componente 'registro': grava as amostras num CSV no cartao SD (fallback LittleFS).
 * A partir do Marco 3b o log e por ENSAIO: registro_iniciar() so monta a midia e
 * sobe a task; registro_abrir()/registro_fechar() abrem e fecham o arquivo do ensaio.
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Uma amostra pronta para gravar e/ou exibir (contrato aquisicao <-> logger <-> UI) */
typedef struct {
    int64_t seg_boot;   /* segundos desde o boot */
    float   temp_c;     /* temperatura (C) */
    float   ur_pct;     /* umidade relativa (%) */
    float   td_c;       /* ponto de orvalho (C) */
    float   ah_gm3;     /* umidade absoluta (g/m3) */
    uint8_t evento;     /* 0 = normal, 1 = marcador de evento */
} amostra_t;

/**
 * @brief Monta a midia (SD, com fallback LittleFS) e sobe a task do logger.
 *        NAO abre arquivo ainda - isso e feito por registro_abrir().
 * @param fila_out  recebe a fila onde a aquisicao/marcador postam amostras.
 * @return ESP_OK se montou alguma midia; erro caso contrario.
 */
esp_err_t registro_iniciar(QueueHandle_t *fila_out);

/**
 * @brief Abre um novo arquivo de log (log_NNNN.csv) e escreve o cabecalho.
 * @param nome_out  (opcional) recebe o caminho do arquivo criado.
 * @param n         tamanho do buffer nome_out.
 * @return ESP_OK se abriu; erro se ja havia um aberto, sem midia, ou falha de I/O.
 */
esp_err_t registro_abrir(char *nome_out, size_t n);

/**
 * @brief Fecha o arquivo de log atual (flush + close).
 */
void registro_fechar(void);

/**
 * @brief Ha um arquivo de log aberto (gravando)?
 */
bool registro_gravando(void);

/**
 * @brief Caminho base da midia montada ("/sdcard" ou "/littlefs"); "" se nenhuma.
 */
const char *registro_base(void);

/**
 * @brief Capacidade da midia montada, lida do proprio sistema de arquivos.
 *        SD -> esp_vfs_fat_info; LittleFS -> esp_littlefs_info.
 * @param total_bytes  (opcional) recebe a capacidade total em bytes.
 * @param livre_bytes  (opcional) recebe o espaco livre em bytes.
 * @return ESP_OK se leu; erro/sem midia caso contrario.
 */
esp_err_t registro_capacidade(uint64_t *total_bytes, uint64_t *livre_bytes);

/**
 * @brief Lista os arquivos log_*.csv da midia montada (caminhos completos, ordenados).
 * @param nomes  matriz [max][48] que recebe os caminhos.
 * @param max    numero maximo de nomes.
 * @return quantidade preenchida.
 */
int registro_listar(char nomes[][48], int max);

#ifdef __cplusplus
}
#endif
