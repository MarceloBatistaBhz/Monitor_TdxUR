/*
 * Psicrometria: converte (temperatura, umidade relativa) em grandezas que
 * NAO dependem da temperatura, que sao o criterio do ensaio de infiltracao:
 *   - ponto de orvalho (Td)      -> formula de Magnus
 *   - umidade absoluta (g/m3)
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ponto de orvalho (C) pela formula de Magnus.
 * @param temp_c  temperatura em C
 * @param ur_pct  umidade relativa em % (0..100)
 */
float psicro_ponto_orvalho(float temp_c, float ur_pct);

/**
 * @brief Umidade absoluta (g/m3).
 * @param temp_c  temperatura em C
 * @param ur_pct  umidade relativa em % (0..100)
 */
float psicro_umidade_absoluta(float temp_c, float ur_pct);

#ifdef __cplusplus
}
#endif
