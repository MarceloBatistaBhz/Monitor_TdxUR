# Brownout (reset por subtensão) na ESP32-P4-WIFI6-Touch-LCD-7B

## O que é

**Brownout** = **reset por subtensão** ("brown-out reset", BOR). Quando a tensão de
alimentação (Vdd) **afunda abaixo de um limiar** — mas sem chegar a zero — o **detector de
brownout** interno do chip força um reset para evitar operação instável / corrupção.

Não confundir com:

- **Blackout** — perda **total** de energia (vai a zero).
- **POR (power-on reset)** — reset **normal** quando a energia sobe ao ligar.
- **Sag / droop** — a queda de tensão em si (a *causa*); o brownout é a *resposta* do chip a ela.

## Como confirmar

- No **serial**, no boot, aparece: `Brownout detector was triggered`.
- Via software, o motivo do reset é **`ESP_RST_BROWNOUT`** (`esp_reset_reason()`).

Ou seja: "resetou sozinho" **não** é sinônimo de "o código travou". Antes de caçar bug no
firmware, confira o *reset reason* no serial.

## Observação prática: tema claro vs. escuro (2026-08-28)

Num script com **tema CLARO** (tela majoritariamente branca) a placa dava brownout; o mesmo
hardware com **tema ESCURO** (caso deste projeto, `22_umidade_temp`) **não**.

Mecanismo:

- O **backlight** (LED, PWM) domina o consumo do display e é **praticamente constante** —
  não depende do conteúdo da tela.
- Mas o painel **IPS é *normally-black***: para acender um pixel, os *source drivers* aplicam
  tensão. Uma tela majoritariamente **branca** (tema claro) faz o painel **drenar um pouco mais**.
- Esse "pouco a mais" **não é a causa raiz** — ele apenas **expõe** uma alimentação que já
  estava no limite: no tema escuro a tensão ficava logo **abaixo** do gatilho do brownout, no
  claro **passava** dele.
- Se o script claro também for pesado em render/animação (ex.: `lv_demo_widgets`), soma mais
  consumo ainda.

## Lições / mitigação

1. **UI escura ajuda** — menos corrente no painel (além do conforto visual). A interface deste
   projeto é escura de propósito.
2. **Trate a alimentação**, que é a causa raiz de um brownout marginal:
   - 2× LiPo 3,7 V em paralelo (plano do projeto — 1 célula é marginal para 4 h com o display 7" ligado);
   - fonte / porta USB / cabo melhores (menos resistência série);
   - capacitor de *bulk* próximo à carga;
   - se necessário, ajustar o limiar em `menuconfig` → *Component config* → *ESP System Settings* → *Brownout*.
3. Confirme sempre pelo *reset reason* no serial antes de suspeitar do código.
