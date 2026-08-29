# Monitor_TdxUR — Data Logger de Temperatura e Umidade

**Versão 0.1.6**

Firmware em **ESP-IDF (C)** para um data logger usado em **ensaio de infiltração de água em gabinete**. A detecção de água se dá pela subida da **umidade absoluta / ponto de orvalho (Td)** do ar interno — grandezas que, ao contrário da umidade relativa, não dependem da temperatura.

## Hardware

- **Placa:** Waveshare **ESP32-P4-WIFI6-Touch-LCD-7B** (ESP32-P4 + display DSI 7" 1024×600 com touch capacitivo).
- **Sensor:** Adafruit **SHT40** (I²C `0x44`), ligado ao conector I²C da placa (PH2.0, `SDA=GPIO7 / SCL=GPIO8`) — **mesmo barramento do touch** (compartilhado, driver `i2c_master` é thread-safe).
- **Armazenamento:** microSD (SDMMC 4-bit) com fallback para LittleFS na flash interna.
- **Buzzer (alarme sonoro):** ativo **TMB12A03** no **GPIO5** (driver direto, `GPIO_DRIVE_CAP_3`, ~18 mA @ 3V3). Recomenda-se **diodo de flyback** (ex.: 1N4148) em paralelo, por ser buzzer magnético.
- Sem Wi-Fi e sem BLE. Alimentação pela USB-C (bancada) ou LiPo.

## Funcionalidades

- Leitura periódica do SHT40; cálculo de **ponto de orvalho (Magnus)** e **umidade absoluta (g/m³)**.
- Log em **CSV** (um arquivo por ensaio), com `fflush`+`fsync` a cada amostra (seguro contra queda de energia). Fallback automático para LittleFS quando não há cartão.
- **GUI (LVGL 9.3)** com 3 telas:
  - **Launcher:** escolha entre *Coletar dados* e *Visualizar teste*.
  - **Coleta:** cards ao vivo (T, UR, Td, AH), gráfico Td/UR, botão *Marcar evento* (registra o instante exato de cada gota), botão *Iniciar/parar log*, e **baseline + alarme de infiltração** (o card do Td fica vermelho quando o Td sobe mais que o limiar acima da referência).
  - **Visualizar:** lista os `log_*.csv` da mídia e desenha o gráfico do ensaio escolhido (Td autoescalado + UR) com marcas de evento.

## Formato do CSV

```
seg_boot;hhmmss;temp_C;ur_pct;td_C;ah_gm3;evento
```
Separador `;`, decimal `.`. `evento=1` marca o instante de uma ação (ex.: gota d'água).

## Estrutura

```
main/            app_main, config central (config_projeto.h), tasks e orquestracao
components/
  sht4x/         driver do sensor (API i2c_master nova, CRC-8)
  psicrometria/  ponto de orvalho (Magnus) + umidade absoluta
  registro/      log CSV no SD + fallback LittleFS (abrir/fechar por ensaio, listar)
  ui/            interface LVGL (launcher, coleta, visualizador)
partitions.csv   factory 6 MB + particao 'littlefs' 2 MB
sdkconfig.defaults  PSRAM, LVGL, particao custom
```

Ajustes ficam centralizados em [`main/config_projeto.h`](main/config_projeto.h): intervalo de amostragem, endereço do sensor, janela de baseline e limiar do alarme.

## Como compilar e gravar

Requer **ESP-IDF v5.4** e terminal **PowerShell**.

```powershell
. .\idf_env.ps1              # ativa o ambiente (dot-source; ajuste os caminhos do seu PC)
idf.py set-target esp32p4    # so na 1a vez
idf.py -p COM9 flash monitor # COM conforme a porta do seu PC
```

### ⚠️ Patch obrigatório no BSP (após baixar os componentes)

Os `managed_components/` não são versionados (são baixados a partir de `dependencies.lock`). O BSP da Waveshare (`waveshare__esp32_p4_wifi6_touch_lcd_7b`) usa `memcpy` **sem incluir `<string.h>`**, o que quebra o build. Após o primeiro download dos componentes, adicione no topo de
`managed_components/waveshare__esp32_p4_wifi6_touch_lcd_7b/esp32_p4_wifi6_touch_lcd_7b.c`:

```c
#include <string.h>
```

Não apague o `dependencies.lock` depois disso, senão o gerenciador re-baixa e desfaz o patch.

## Notas

- **Modo bancada:** para testes rápidos, `INTERVALO_AMOSTRAGEM_S` e `BASELINE_SEGUNDOS` podem ser reduzidos (ver comentários no `config_projeto.h`); os valores reais do ensaio são **15 s** e **180 s**.
- As fontes Montserrat padrão do LVGL não trazem acentos do português, por isso os textos da interface estão sem acento.

## Histórico

- **v0.1.6** — **Alarme sonoro**: buzzer ativo no **GPIO5** (`GPIO_DRIVE_CAP_3`) que apita por **15 s** (cadência 0,5 s ligado / 1,0 s desligado) na **borda de subida** do alarme de infiltração; não reapita enquanto o alarme seguir armado (só após desarmar e armar de novo). Novo componente `buzzer`. Tela de coleta: **campo de tempo de gravação HH:MM:SS** (centro), com Baseline movido à esquerda e Eventos à direita.
- **v0.1.5** — Tela *Visualizar teste*: lista de logs em **ordem alfabética decrescente** (os mais recentes no topo, sem precisar rolar); **cabeçalho amarelo** com o **uso do microSD** (usado / total) lido do próprio cartão via `esp_vfs_fat_info` (fallback LittleFS `esp_littlefs_info`).
- **v0.1.4** — Tela *Visualizar teste*: **tamanho de cada arquivo** (B/KB/MB) ao lado do nome na lista; **gráfico com as 4 grandezas** do log (Td verde, Temp vermelho e AH amarelo no eixo esquerdo autoescalado; UR azul no eixo direito).
- **v0.1.3** — Tela de coleta: **contagem regressiva** do baseline (*capturando... N s*, com decremento suave a cada ~400 ms reancorado a cada amostra); label do baseline reposicionado ~5 mm à esquerda. Config restaurada para os **valores reais do ensaio** (amostragem **15 s**, baseline **180 s**).
- **v0.1.2** — Ajustes na tela de coleta: título dinâmico (*Mostrando os dados online* / *Coletando os dados*); legendas das grandezas movidas para **abaixo** do eixo (`Td / Temp / AH` à esquerda, `UR` à direita) com baseline centralizado; botões *Marcar evento*/*Iniciar log* 25% mais estreitos; **alarme de infiltração reposicionado entre os dois botões e agora piscando** (~1 Hz: 0,7 s aceso / 0,3 s apagado); **gráfico é limpo ao iniciar um novo log**; grade do gráfico trocada por **linha pontilhada discreta**.
- **v0.1.1** — Tela de coleta: linhas **selecionáveis** de Temperatura (vermelho) e Umid. absoluta (amarelo) via *ticks* nos cards (só visualização; não afetam o log). Cada card recebe o **fundo na cor da sua linha**, com texto de contraste; alarme do Td em vermelho mantido.
- **v0.1.0** — Versão inicial: aquisição SHT40, cálculo de Td/umidade absoluta, log CSV no SD com fallback LittleFS, GUI (launcher, coleta com baseline/alarme) e visualizador de ensaios.
