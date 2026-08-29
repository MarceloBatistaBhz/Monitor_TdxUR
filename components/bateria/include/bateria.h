/*
 * Componente 'bateria': le a tensao da bateria (ou da USB, se nao houver bateria)
 * pelo ADC no GPIO20 da ESP32-P4-WIFI6-Touch-LCD-7B.
 *
 * Hardware (esquematico): rede BAT (terminal da bateria / BATS do carregador ETA6098)
 *   -> R92 200k -> GPIO20 -> R93 100k -> GND, com C190 100nF de filtro.
 *   Divisor 1/3: Vbat = Vadc * 3. GPIO20 e interno (nao esta em nenhum header).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Configura o ADC (unidade+canal resolvidos do GPIO20) + calibracao. Chamar uma vez no boot. */
void bateria_iniciar(void);

/** @brief Le a tensao da bateria em volts (ja aplica o fator 3 do divisor). Retorna <0 em erro. */
float bateria_ler_v(void);

#ifdef __cplusplus
}
#endif
