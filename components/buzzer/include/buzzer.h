/*
 * Componente 'buzzer': sinaliza o alarme de infiltracao com som (buzzer ATIVO).
 * Buzzer TMB12A03 no GPIO5 (driver direto, GPIO_DRIVE_CAP_3). ~18 mA @ 3V3.
 * OBS de hardware: buzzer magnetico -> use um diodo de flyback (ex.: 1N4148)
 * em paralelo para proteger o GPIO do pico reverso ao desligar.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Configura o GPIO do buzzer e sobe a task da cadencia. Chamar uma vez no boot. */
void buzzer_iniciar(void);

/**
 * @brief Informa o estado do alarme de infiltracao (Td > baseline + limiar).
 *        Na BORDA DE SUBIDA (desarmado -> armado) dispara UM alarme sonoro de 15 s
 *        na cadencia 0,5 s ligado / 1,0 s desligado (10 apitos). Enquanto o alarme
 *        continuar armado NAO reapita; so volta a apitar apos desarmar e armar de novo.
 */
void buzzer_set_alarme(bool alarme_ativo);

#ifdef __cplusplus
}
#endif
