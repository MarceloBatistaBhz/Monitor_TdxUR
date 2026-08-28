#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

/* Cores (tema escuro) */
#define COR_FUNDO      0x10141c
#define COR_CARTAO     0x1e2633
#define COR_DESTAQUE   0x1e8a4c
#define COR_ALERTA     0xc0392b
#define COR_VERDE_BTN  0x1e8a4c
#define COR_AZUL_BTN   0x2f6fb0
#define COR_SUAVE      0x9aa4b0
#define COR_TD_LINHA   0x35d07f
#define COR_UR_LINHA   0x4a90d9

#define GRAFICO_PONTOS 720
#define MAX_LOGS       64
#define VISU_PONTOS    1000       /* max de pontos plotados no visualizador */
#define COR_EVENTO     0xf39c12   /* laranja: marca de evento no grafico do visualizador */

/* --- Telas --- */
static lv_obj_t *s_scr_launcher, *s_scr_coleta, *s_scr_visu;

/* --- Estado da coleta (aquisicao/app escrevem; lv_timer le) --- */
static SemaphoreHandle_t s_mtx;
static amostra_t s_amostra;
static bool      s_nova;
static uint32_t  s_evt_count;
static bool      s_evt_dirty;
static bool      s_grav;
static char      s_grav_nome[40];
static bool      s_base_pronto;
static float     s_base_td;
static bool      s_alerta;

/* --- Widgets da coleta --- */
static lv_obj_t *s_val_temp, *s_val_ur, *s_val_td, *s_val_ah, *s_card_td;
static lv_obj_t *s_chart, *s_lbl_rec, *s_lbl_baseline, *s_lbl_eventos, *s_lbl_alerta;
static lv_obj_t *s_btn_log, *s_btn_log_lbl;
static lv_chart_series_t *s_serie_td, *s_serie_ur;

/* --- Widgets do visualizador --- */
static lv_obj_t *s_visu_lista, *s_visu_area;
static char      s_visu_nomes[MAX_LOGS][48];

static const ui_callbacks_t *s_cb;

/* =================== helpers =================== */

static lv_obj_t *cria_botao(lv_obj_t *parent, const char *txt, uint32_t cor,
                            int w, int h, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(cor), 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_22, 0);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *cria_card(lv_obj_t *parent, const char *titulo, const char *unidade,
                           lv_obj_t **out_valor, bool destaque)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 232, 148);
    lv_obj_set_style_bg_color(card, lv_color_hex(destaque ? COR_DESTAQUE : COR_CARTAO), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lt = lv_label_create(card);
    lv_label_set_text(lt, titulo);
    lv_obj_set_style_text_font(lt, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lt, lv_color_white(), 0);

    lv_obj_t *lv = lv_label_create(card);
    lv_label_set_text(lv, "--");
    lv_obj_set_style_text_font(lv, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lv, lv_color_white(), 0);
    *out_valor = lv;

    lv_obj_t *lu = lv_label_create(card);
    lv_label_set_text(lu, unidade);
    lv_obj_set_style_text_font(lu, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lu, lv_color_hex(COR_SUAVE), 0);
    return card;
}

static void rotulo_eixo(lv_obj_t *ref, const char *txt, uint32_t cor,
                        const lv_font_t *fonte, lv_align_t align, int dx, int dy)
{
    lv_obj_t *l = lv_label_create(lv_obj_get_parent(ref));
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, fonte, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(cor), 0);
    lv_obj_align_to(l, ref, align, dx, dy);
}

/* =================== navegacao =================== */

static void nav_coletar(lv_event_t *e)   { lv_screen_load(s_scr_coleta); }
static void nav_menu(lv_event_t *e)      { lv_screen_load(s_scr_launcher); }

/* Mensagem simples na area de conteudo do visualizador */
static void visu_area_msg(const char *msg)
{
    lv_obj_clean(s_visu_area);
    lv_obj_t *l = lv_label_create(s_visu_area);
    lv_label_set_text(l, msg);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 8, 20);
}

/* Le o CSV do ensaio escolhido e desenha o grafico (Td autoescalado + UR) com marcas de evento */
static void visu_carregar(const char *caminho)
{
    lv_obj_clean(s_visu_area);
    const char *bn = strrchr(caminho, '/');
    bn = bn ? bn + 1 : caminho;

    char linha[96];

    /* passo 1: conta as linhas de dados (pula o cabecalho) */
    FILE *f = fopen(caminho, "r");
    if (f == NULL) { visu_area_msg("Erro ao abrir o arquivo."); return; }
    int R = 0;
    if (fgets(linha, sizeof linha, f)) { /* cabecalho */ }
    while (fgets(linha, sizeof linha, f)) R++;
    fclose(f);
    if (R == 0) { visu_area_msg("Arquivo sem dados."); return; }

    int P = (R < VISU_PONTOS) ? R : VISU_PONTOS;
    int stride = R / P;
    if (stride < 1) stride = 1;

    lv_obj_t *info = lv_label_create(s_visu_area);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(info, lv_color_white(), 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 4, 2);

    lv_obj_t *ch = lv_chart_create(s_visu_area);
    lv_obj_set_size(ch, 500, 380);
    lv_obj_align(ch, LV_ALIGN_TOP_MID, 4, 44);
    lv_obj_set_style_bg_color(ch, lv_color_hex(COR_CARTAO), 0);
    lv_obj_set_style_border_width(ch, 0, 0);
    lv_obj_set_style_radius(ch, 10, 0);
    lv_obj_set_style_pad_all(ch, 4, 0);
    lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ch, P);
    lv_chart_set_range(ch, LV_CHART_AXIS_SECONDARY_Y, 0, 1000);   /* UR 0..100 % */
    lv_chart_set_div_line_count(ch, 5, 0);
    lv_obj_set_style_width(ch, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(ch, 0, LV_PART_INDICATOR);
    lv_chart_series_t *st = lv_chart_add_series(ch, lv_color_hex(COR_TD_LINHA), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_series_t *su = lv_chart_add_series(ch, lv_color_hex(COR_UR_LINHA), LV_CHART_AXIS_SECONDARY_Y);

    /* passo 2: preenche (com downsampling), acha td_min/max e marca eventos */
    f = fopen(caminho, "r");
    if (fgets(linha, sizeof linha, f)) { /* cabecalho */ }
    int j = 0, p = 0, nev = 0, ncur = 0;
    float td_min = 1e9f, td_max = -1e9f;
    while (p < P && fgets(linha, sizeof linha, f)) {
        long long sb; char hh[12]; float t, ur, td, ah; int ev;
        if (sscanf(linha, "%lld;%11[^;];%f;%f;%f;%f;%d", &sb, hh, &t, &ur, &td, &ah, &ev) >= 7) {
            if (j % stride == 0) {
                lv_chart_set_series_value_by_id(ch, st, p, (int32_t)(td * 10.0f));
                lv_chart_set_series_value_by_id(ch, su, p, (int32_t)(ur * 10.0f));
                if (td < td_min) td_min = td;
                if (td > td_max) td_max = td;
                if (ev == 1 && ncur < 32) {
                    lv_chart_cursor_t *c = lv_chart_add_cursor(ch, lv_color_hex(COR_EVENTO), LV_DIR_VER);
                    lv_chart_set_cursor_point(ch, c, st, p);
                    ncur++;
                }
                if (ev == 1) nev++;
                p++;
            } else if (ev == 1) {              /* evento numa linha nao plotada: marca o ponto vizinho */
                nev++;
                if (ncur < 32 && p > 0) {
                    lv_chart_cursor_t *c = lv_chart_add_cursor(ch, lv_color_hex(COR_EVENTO), LV_DIR_VER);
                    lv_chart_set_cursor_point(ch, c, st, p - 1);
                    ncur++;
                }
            }
            j++;
        }
    }
    fclose(f);

    /* autoescala do eixo do Td para destacar a variacao */
    if (td_max <= td_min) { td_min -= 1.0f; td_max += 1.0f; }
    float margem = (td_max - td_min) * 0.15f;
    if (margem < 0.5f) margem = 0.5f;
    float lo = td_min - margem, hi = td_max + margem;
    lv_chart_set_range(ch, LV_CHART_AXIS_PRIMARY_Y, (int32_t)(lo * 10.0f), (int32_t)(hi * 10.0f));

    lv_label_set_text_fmt(info, "%s   -   %d amostras, %d eventos", bn, R, nev);

    /* escalas: Td (verde, dinamico) a esquerda; UR (azul, 0..100) a direita */
    char b1[8], b2[8], b3[8];
    snprintf(b1, sizeof b1, "%.1f", hi);
    snprintf(b2, sizeof b2, "%.1f", (lo + hi) / 2.0f);
    snprintf(b3, sizeof b3, "%.1f", lo);
    rotulo_eixo(ch, "Td",  COR_TD_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_TOP_LEFT,   4, -2);
    rotulo_eixo(ch, b1,    COR_TD_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_LEFT_TOP,   -4,  2);
    rotulo_eixo(ch, b2,    COR_TD_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_LEFT_MID,   -4,  0);
    rotulo_eixo(ch, b3,    COR_TD_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_LEFT_BOTTOM,-4, -2);
    rotulo_eixo(ch, "UR",  COR_UR_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_TOP_RIGHT,  -4, -2);
    rotulo_eixo(ch, "100", COR_UR_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_RIGHT_TOP,    4,  2);
    rotulo_eixo(ch, "50",  COR_UR_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_RIGHT_MID,    4,  0);
    rotulo_eixo(ch, "0",   COR_UR_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -2);

    lv_obj_t *le = lv_label_create(s_visu_area);
    lv_label_set_text(le, LV_SYMBOL_WARNING " marcas verticais = eventos");
    lv_obj_set_style_text_font(le, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(le, lv_color_hex(COR_EVENTO), 0);
    lv_obj_align(le, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_chart_refresh(ch);
}

static void visu_item_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < MAX_LOGS) {
        visu_carregar(s_visu_nomes[i]);
    }
}

static void visu_refresh(void)
{
    lv_obj_clean(s_visu_lista);
    int n = registro_listar(s_visu_nomes, MAX_LOGS);
    if (n == 0) {
        lv_list_add_text(s_visu_lista, "(nenhum log encontrado)");
        return;
    }
    for (int i = 0; i < n; i++) {
        const char *bn = strrchr(s_visu_nomes[i], '/');
        bn = bn ? bn + 1 : s_visu_nomes[i];
        lv_obj_t *b = lv_list_add_button(s_visu_lista, LV_SYMBOL_FILE, bn);
        lv_obj_add_event_cb(b, visu_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void nav_visualizar(lv_event_t *e)
{
    visu_refresh();
    visu_area_msg("Selecione um teste na lista.");
    lv_screen_load(s_scr_visu);
}

/* =================== tela de coleta =================== */

static void timer_cb(lv_timer_t *t)
{
    amostra_t a;
    bool nova, evt_d, grav, bpronto, alerta;
    uint32_t evt;
    float btd;
    char nome[40];
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    a = s_amostra;     nova = s_nova;         s_nova = false;
    evt_d = s_evt_dirty; s_evt_dirty = false;  evt = s_evt_count;
    grav = s_grav;     strcpy(nome, s_grav_nome);
    bpronto = s_base_pronto; btd = s_base_td;
    alerta = s_alerta;
    xSemaphoreGive(s_mtx);

    char buf[64];
    if (nova) {
        snprintf(buf, sizeof buf, "%.1f", a.temp_c); lv_label_set_text(s_val_temp, buf);
        snprintf(buf, sizeof buf, "%.1f", a.ur_pct); lv_label_set_text(s_val_ur, buf);
        snprintf(buf, sizeof buf, "%.1f", a.td_c);   lv_label_set_text(s_val_td, buf);
        snprintf(buf, sizeof buf, "%.1f", a.ah_gm3); lv_label_set_text(s_val_ah, buf);
        lv_chart_set_next_value(s_chart, s_serie_td, (int32_t)(a.td_c   * 10.0f));
        lv_chart_set_next_value(s_chart, s_serie_ur, (int32_t)(a.ur_pct * 10.0f));
    }
    if (evt_d) {
        snprintf(buf, sizeof buf, "Eventos: %lu", (unsigned long)evt);
        lv_label_set_text(s_lbl_eventos, buf);
    }
    if (!grav) {
        lv_label_set_text(s_lbl_baseline, "Baseline: --");
    } else if (!bpronto) {
        lv_label_set_text(s_lbl_baseline, "Baseline: capturando...");
    } else {
        snprintf(buf, sizeof buf, "Baseline Td: %.1f C", btd);
        lv_label_set_text(s_lbl_baseline, buf);
    }

    static int last_grav = -1;
    if ((int)grav != last_grav) {
        last_grav = grav;
        lv_label_set_text(s_btn_log_lbl, grav ? "Parar log" : "Iniciar log");
        lv_obj_set_style_bg_color(s_btn_log, lv_color_hex(grav ? COR_ALERTA : COR_VERDE_BTN), 0);
        if (grav) {
            const char *bn = strrchr(nome, '/');
            snprintf(buf, sizeof buf, LV_SYMBOL_SAVE " GRAVANDO  %s", bn ? bn + 1 : nome);
            lv_label_set_text(s_lbl_rec, buf);
            lv_obj_set_style_text_color(s_lbl_rec, lv_color_hex(COR_ALERTA), 0);
        } else {
            lv_label_set_text(s_lbl_rec, "parado");
            lv_obj_set_style_text_color(s_lbl_rec, lv_color_hex(COR_SUAVE), 0);
        }
    }

    static int last_alerta = -1;
    if ((int)alerta != last_alerta) {
        last_alerta = alerta;
        lv_obj_set_style_bg_color(s_card_td, lv_color_hex(alerta ? COR_ALERTA : COR_DESTAQUE), 0);
        if (alerta) lv_obj_remove_flag(s_lbl_alerta, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_lbl_alerta, LV_OBJ_FLAG_HIDDEN);
    }
}

static void btn_evento_cb(lv_event_t *e)
{
    if (s_cb && s_cb->on_marcar_evento) s_cb->on_marcar_evento();
}
static void btn_log_cb(lv_event_t *e)
{
    if (s_cb && s_cb->on_toggle_log) s_cb->on_toggle_log();
}

static void monta_coleta(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(COR_FUNDO), LV_PART_MAIN);

    lv_obj_t *btn_menu = cria_botao(scr, LV_SYMBOL_LEFT " Menu", COR_CARTAO, 120, 40, nav_menu, NULL);
    lv_obj_align(btn_menu, LV_ALIGN_TOP_LEFT, 16, 8);

    lv_obj_t *titulo = lv_label_create(scr);
    lv_label_set_text(titulo, "Coletando dados");
    lv_obj_set_style_text_font(titulo, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(titulo, lv_color_white(), 0);
    lv_obj_align(titulo, LV_ALIGN_TOP_LEFT, 156, 12);

    s_lbl_rec = lv_label_create(scr);
    lv_label_set_text(s_lbl_rec, "parado");
    lv_obj_set_style_text_font(s_lbl_rec, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_lbl_rec, lv_color_hex(COR_SUAVE), 0);
    lv_obj_align(s_lbl_rec, LV_ALIGN_TOP_RIGHT, -20, 14);

    lv_obj_t *linha = lv_obj_create(scr);
    lv_obj_set_size(linha, 1000, 158);
    lv_obj_align(linha, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_set_style_bg_opa(linha, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(linha, 0, 0);
    lv_obj_set_style_pad_all(linha, 0, 0);
    lv_obj_remove_flag(linha, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(linha, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(linha, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    cria_card(linha, "Temperatura", "C",    &s_val_temp, false);
    cria_card(linha, "Umid. rel.",  "%",    &s_val_ur,   false);
    s_card_td = cria_card(linha, "Pto orvalho", "C", &s_val_td, true);
    cria_card(linha, "Umid. abs.",  "g/m3", &s_val_ah,   false);

    s_chart = lv_chart_create(scr);
    lv_obj_set_size(s_chart, 900, 244);
    lv_obj_align(s_chart, LV_ALIGN_TOP_MID, 0, 214);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(COR_CARTAO), 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_radius(s_chart, 12, 0);
    lv_obj_set_style_pad_all(s_chart, 4, 0);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, GRAFICO_PONTOS);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y,   0, 400);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 1000);
    lv_chart_set_div_line_count(s_chart, 5, 0);
    lv_obj_set_style_width(s_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(s_chart, 0, LV_PART_INDICATOR);
    s_serie_td = lv_chart_add_series(s_chart, lv_color_hex(COR_TD_LINHA), LV_CHART_AXIS_PRIMARY_Y);
    s_serie_ur = lv_chart_add_series(s_chart, lv_color_hex(COR_UR_LINHA), LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_set_all_value(s_chart, s_serie_td, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(s_chart, s_serie_ur, LV_CHART_POINT_NONE);

    rotulo_eixo(s_chart, "Td (C)", COR_TD_LINHA, &lv_font_montserrat_18, LV_ALIGN_OUT_TOP_LEFT,   6, -2);
    rotulo_eixo(s_chart, "40",     COR_TD_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_LEFT_TOP,   -4,  2);
    rotulo_eixo(s_chart, "20",     COR_TD_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_LEFT_MID,   -4,  0);
    rotulo_eixo(s_chart, "0",      COR_TD_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_LEFT_BOTTOM,-4, -2);
    rotulo_eixo(s_chart, "UR (%)", COR_UR_LINHA, &lv_font_montserrat_18, LV_ALIGN_OUT_TOP_RIGHT,  -6, -2);
    rotulo_eixo(s_chart, "100",    COR_UR_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_RIGHT_TOP,    4,  2);
    rotulo_eixo(s_chart, "50",     COR_UR_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_RIGHT_MID,    4,  0);
    rotulo_eixo(s_chart, "0",      COR_UR_LINHA, &lv_font_montserrat_14, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -2);

    s_lbl_baseline = lv_label_create(scr);
    lv_label_set_text(s_lbl_baseline, "Baseline: --");
    lv_obj_set_style_text_font(s_lbl_baseline, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_lbl_baseline, lv_color_white(), 0);
    lv_obj_align(s_lbl_baseline, LV_ALIGN_TOP_LEFT, 24, 470);

    s_lbl_alerta = lv_label_create(scr);
    lv_label_set_text(s_lbl_alerta, LV_SYMBOL_WARNING " POSSIVEL INFILTRACAO (Td subiu > limiar)");
    lv_obj_set_style_text_font(s_lbl_alerta, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_lbl_alerta, lv_color_hex(COR_ALERTA), 0);
    lv_obj_align(s_lbl_alerta, LV_ALIGN_TOP_MID, 0, 468);
    lv_obj_add_flag(s_lbl_alerta, LV_OBJ_FLAG_HIDDEN);

    s_lbl_eventos = lv_label_create(scr);
    lv_label_set_text(s_lbl_eventos, "Eventos: 0");
    lv_obj_set_style_text_font(s_lbl_eventos, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_lbl_eventos, lv_color_white(), 0);
    lv_obj_align(s_lbl_eventos, LV_ALIGN_TOP_RIGHT, -24, 470);

    lv_obj_t *btn_ev = cria_botao(scr, "Marcar evento", COR_AZUL_BTN, 270, 74, btn_evento_cb, NULL);
    lv_obj_align(btn_ev, LV_ALIGN_BOTTOM_LEFT, 24, -14);

    s_btn_log = lv_button_create(scr);
    lv_obj_set_size(s_btn_log, 270, 74);
    lv_obj_align(s_btn_log, LV_ALIGN_BOTTOM_RIGHT, -24, -14);
    lv_obj_set_style_bg_color(s_btn_log, lv_color_hex(COR_VERDE_BTN), 0);
    lv_obj_add_event_cb(s_btn_log, btn_log_cb, LV_EVENT_CLICKED, NULL);
    s_btn_log_lbl = lv_label_create(s_btn_log);
    lv_label_set_text(s_btn_log_lbl, "Iniciar log");
    lv_obj_set_style_text_font(s_btn_log_lbl, &lv_font_montserrat_22, 0);
    lv_obj_center(s_btn_log_lbl);

    lv_timer_create(timer_cb, 400, NULL);
}

/* =================== launcher =================== */

static void monta_launcher(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(COR_FUNDO), LV_PART_MAIN);

    lv_obj_t *t1 = lv_label_create(scr);
    lv_label_set_text(t1, "Data Logger - Ensaio de Infiltracao  v0.1.0");
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(t1, lv_color_white(), 0);
    lv_obj_align(t1, LV_ALIGN_TOP_MID, 0, 90);

    lv_obj_t *t2 = lv_label_create(scr);
    lv_label_set_text(t2, "Escolha uma opcao");
    lv_obj_set_style_text_font(t2, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(t2, lv_color_hex(COR_SUAVE), 0);
    lv_obj_align(t2, LV_ALIGN_TOP_MID, 0, 132);

    lv_obj_t *bc = cria_botao(scr, "Coletar dados", COR_VERDE_BTN, 380, 130, nav_coletar, NULL);
    lv_obj_align(bc, LV_ALIGN_CENTER, -210, 30);

    lv_obj_t *bv = cria_botao(scr, "Visualizar teste", COR_AZUL_BTN, 380, 130, nav_visualizar, NULL);
    lv_obj_align(bv, LV_ALIGN_CENTER, 210, 30);
}

/* =================== visualizador =================== */

static void monta_visualizador(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(COR_FUNDO), LV_PART_MAIN);

    lv_obj_t *btn_menu = cria_botao(scr, LV_SYMBOL_LEFT " Menu", COR_CARTAO, 120, 40, nav_menu, NULL);
    lv_obj_align(btn_menu, LV_ALIGN_TOP_LEFT, 16, 8);

    lv_obj_t *titulo = lv_label_create(scr);
    lv_label_set_text(titulo, "Visualizar teste");
    lv_obj_set_style_text_font(titulo, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(titulo, lv_color_white(), 0);
    lv_obj_align(titulo, LV_ALIGN_TOP_LEFT, 156, 12);

    /* Lista de arquivos (esquerda) */
    s_visu_lista = lv_list_create(scr);
    lv_obj_set_size(s_visu_lista, 360, 500);
    lv_obj_align(s_visu_lista, LV_ALIGN_TOP_LEFT, 16, 70);
    lv_obj_set_style_bg_color(s_visu_lista, lv_color_hex(COR_CARTAO), 0);

    /* Area de conteudo (direita): recebe o grafico do teste selecionado */
    s_visu_area = lv_obj_create(scr);
    lv_obj_set_size(s_visu_area, 596, 504);
    lv_obj_align(s_visu_area, LV_ALIGN_TOP_LEFT, 400, 62);
    lv_obj_set_style_bg_opa(s_visu_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_visu_area, 0, 0);
    lv_obj_set_style_pad_all(s_visu_area, 4, 0);
    lv_obj_remove_flag(s_visu_area, LV_OBJ_FLAG_SCROLLABLE);
    visu_area_msg("Selecione um teste na lista.");
}

/* =================== API publica =================== */

void ui_iniciar(void)
{
    s_mtx = xSemaphoreCreateMutex();

    s_scr_launcher = lv_obj_create(NULL);
    s_scr_coleta   = lv_obj_create(NULL);
    s_scr_visu     = lv_obj_create(NULL);

    monta_coleta(s_scr_coleta);
    monta_visualizador(s_scr_visu);
    monta_launcher(s_scr_launcher);

    lv_screen_load(s_scr_launcher);
}

void ui_set_callbacks(const ui_callbacks_t *cb) { s_cb = cb; }

void ui_nova_amostra(const amostra_t *a)
{
    if (s_mtx == NULL) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_amostra = *a;
    s_nova = true;
    xSemaphoreGive(s_mtx);
}

void ui_evento_marcado(void)
{
    if (s_mtx == NULL) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_evt_count++;
    s_evt_dirty = true;
    xSemaphoreGive(s_mtx);
}

void ui_set_gravando(bool gravando, const char *nome)
{
    if (s_mtx == NULL) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_grav = gravando;
    if (nome != NULL) {
        strncpy(s_grav_nome, nome, sizeof s_grav_nome - 1);
        s_grav_nome[sizeof s_grav_nome - 1] = '\0';
    } else {
        s_grav_nome[0] = '\0';
    }
    if (!gravando) {
        s_evt_count = 0;
        s_evt_dirty = true;
    }
    xSemaphoreGive(s_mtx);
}

void ui_set_baseline(bool pronto, float td_baseline)
{
    if (s_mtx == NULL) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_base_pronto = pronto;
    s_base_td = td_baseline;
    xSemaphoreGive(s_mtx);
}

void ui_set_alerta(bool alerta)
{
    if (s_mtx == NULL) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_alerta = alerta;
    xSemaphoreGive(s_mtx);
}
