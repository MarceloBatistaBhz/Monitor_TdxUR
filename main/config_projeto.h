/*
 * Configuracao central do projeto - mexa AQUI em vez de espalhar numeros pelo codigo.
 */
#pragma once

/* ============================================================
 * Sensor SHT40 no barramento I2C COMPARTILHADO do touch
 * ------------------------------------------------------------
 * A partir do Marco 3 o SHT40 usa o MESMO barramento do touch (GPIO7/8), obtido
 * com bsp_i2c_get_handle() depois de bsp_display_start(). Nao criamos mais um
 * barramento proprio. Fisicamente continua sendo o conector "I2C" da placa
 * (PH2.0, GPIO7/8). O driver sht4x nao mudou - so recebe outro bus handle.
 * ============================================================ */
#define SHT40_ENDERECO        0x44             /* Adafruit SHT40 */
#define I2C_CLOCK_HZ          100000           /* velocidade do SHT40 (por-dispositivo; touch roda a 400k) */

/* ============================================================
 * Aquisicao
 * ============================================================ */
#define INTERVALO_AMOSTRAGEM_S   15            /* valor real do ensaio */
/* Dica de bancada: baixe para 2 ou 3 s enquanto testa, para ver leituras rapido. */

/* ============================================================
 * Tasks (o ESP32-P4 tem 2 nucleos: deixamos o sensor no core 1,
 * porque o LVGL vai ocupar o core 0 nos proximos marcos)
 * ============================================================ */
#define CORE_AQUISICAO        1
#define STACK_AQUISICAO       4096
#define PRIO_AQUISICAO        5

/* ============================================================
 * Marcador de evento (registrar o instante de cada gota d'agua)
 * Ate a UI existir, e acionado por uma tecla no Serial Monitor.
 * ============================================================ */
#define TECLA_EVENTO          'm'              /* tecla que marca um evento (minuscula) */
#define STACK_SERIAL          3072
#define PRIO_SERIAL           3
#define CORE_SERIAL           0

/* ============================================================
 * Baseline / alarme de infiltracao
 * Ao iniciar o log, captura o Td medio dos primeiros minutos como referencia.
 * Depois, alerta quando o Td atual subir mais que o limiar acima do baseline.
 * ============================================================ */
#define BASELINE_SEGUNDOS     180              /* 3 min - valor real do ensaio (modo bancada: baixe p/ ~20 s) */
#define LIMIAR_ALERTA_C       1.5f             /* alerta: Td subiu mais que isso acima do baseline */
