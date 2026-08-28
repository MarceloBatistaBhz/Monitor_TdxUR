/*
 * Componente 'ui' - interface grafica LVGL do data logger.
 * MARCO 3b: 4 cards + grafico (Td/UR) + botao "Marcar evento" +
 *           botao "Iniciar/parar log" + baseline + alerta de infiltracao.
 */
#pragma once

#include "registro.h"   /* amostra_t */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Callbacks que a UI dispara em resposta ao toque (implementados no app) */
typedef struct {
    void (*on_marcar_evento)(void);   /* botao "Marcar evento" */
    void (*on_toggle_log)(void);      /* botao "Iniciar/parar log" */
} ui_callbacks_t;

/** @brief Cria a tela (chamar com o lock do LVGL: bsp_display_lock). */
void ui_iniciar(void);

/** @brief Registra os callbacks de toque (chamar depois de ui_iniciar). */
void ui_set_callbacks(const ui_callbacks_t *cb);

/** @brief Entrega a amostra mais recente (thread-safe): atualiza cards e grafico. */
void ui_nova_amostra(const amostra_t *a);

/** @brief Incrementa o contador de eventos na tela. */
void ui_evento_marcado(void);

/** @brief Estado da gravacao: liga/desliga o indicador e troca o botao. nome pode ser NULL. */
void ui_set_gravando(bool gravando, const char *nome);

/** @brief Baseline: pronto=false mostra "capturando"; pronto=true mostra o valor. */
void ui_set_baseline(bool pronto, float td_baseline);

/** @brief Alerta de infiltracao: destaca o card do Td em vermelho e mostra o aviso. */
void ui_set_alerta(bool alerta);

#ifdef __cplusplus
}
#endif
