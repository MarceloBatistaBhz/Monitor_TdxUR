#include "psicrometria.h"
#include <math.h>

/* Coeficientes de Magnus (validos de -45 a +60 C). b e adimensional, c em C. */
#define MAGNUS_B   17.62f
#define MAGNUS_C   243.12f

float psicro_ponto_orvalho(float temp_c, float ur_pct)
{
    if (ur_pct < 0.1f) {
        ur_pct = 0.1f;             /* evita log(0) se a UR vier zerada */
    }
    /* gama = ln(UR/100) + (b*T)/(c+T);  Td = c*gama / (b - gama) */
    float gama = logf(ur_pct / 100.0f) + (MAGNUS_B * temp_c) / (MAGNUS_C + temp_c);
    return (MAGNUS_C * gama) / (MAGNUS_B - gama);
}

float psicro_umidade_absoluta(float temp_c, float ur_pct)
{
    /*
     * AH [g/m3] = 6.112 * e^(17.62*T/(243.12+T)) * UR * 2.1674 / (273.15 + T)
     * O primeiro fator e a pressao de vapor de saturacao (hPa); a constante 2.1674
     * junta a massa molar da agua com a constante dos gases.
     */
    float pressao_sat = 6.112f * expf((MAGNUS_B * temp_c) / (MAGNUS_C + temp_c)); /* hPa */
    return (pressao_sat * ur_pct * 2.1674f) / (273.15f + temp_c);
}
