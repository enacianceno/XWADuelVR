/* XWADuelVR M7 menu bodies. See duel_menu.h. */
#include "duel_menu.h"

#include <stdio.h>
#include <string.h>

#include "duel_sim.h" /* M10: skirmish size range (pure header) */

static const char *kDiffName[3] = { "FACIL", "NORMAL", "DIFICIL" };

/* M28: XWA setup craft names (indexed by DuelCraft). */
static const char *kCraftName[DUEL_CRAFT_COUNT] =
    { "X-WING", "TIE", "TIE INT" };

/* M31: brief force lines follow the asymmetric picks (1 TU + n
 * ALIADOS vs m ENEMIGOS; skirmish needs >= 1 foe). Rebuilt on init,
 * screen change, and force cycle (rows point at these buffers). */
static void duel_refresh_brief(DuelMenu *m) {
    int foes = m->foes_n;
    int allies = m->allies_n;
    if (foes < DUEL_FOES_MIN || foes > DUEL_FOES_MAX)
        foes = DUEL_FOES_DEFAULT;
    if (allies < DUEL_ALLIES_MIN || allies > DUEL_ALLIES_MAX)
        allies = DUEL_ALLIES_DEFAULT;
    if (m->duel_mode == 2) {
        int cp = (m->craft_player >= 0 && m->craft_player < DUEL_CRAFT_COUNT) ?
            m->craft_player : DUEL_CRAFT_XWING;
        snprintf(m->dyn[4], sizeof(m->dyn[4]), "%s VS ISD", kCraftName[cp]);
        if (foes > 0)
            snprintf(m->dyn[5], sizeof(m->dyn[5]), "TORR:10+TIE:%d", foes);
        else
            snprintf(m->dyn[5], sizeof(m->dyn[5]), "TORRETAS: %d",
                     DUEL_CRUISER_TURRETS);
        return;
    }
    if (m->duel_mode) {
        int cp = (m->craft_player >= 0 && m->craft_player < DUEL_CRAFT_COUNT) ?
            m->craft_player : DUEL_CRAFT_XWING;
        int ce = (m->craft_enemy >= 0 && m->craft_enemy < DUEL_CRAFT_COUNT) ?
            m->craft_enemy : DUEL_CRAFT_TIEIN;
        snprintf(m->dyn[4], sizeof(m->dyn[4]), "%s VS %s",
                 kCraftName[cp], kCraftName[ce]);
        snprintf(m->dyn[5], sizeof(m->dyn[5]), "RIVAL: OTRO PILOTO");
        return;
    }
    {
        int cp = (m->craft_player >= 0 && m->craft_player < DUEL_CRAFT_COUNT) ?
            m->craft_player : DUEL_CRAFT_XWING;
        int ce = (m->craft_enemy >= 0 && m->craft_enemy < DUEL_CRAFT_COUNT) ?
            m->craft_enemy : DUEL_CRAFT_TIEIN;
        if (foes < 1) foes = 1; /* skirmish needs an enemy */
        snprintf(m->dyn[4], sizeof(m->dyn[4]), "%s+%d VS %d %s",
                 kCraftName[cp], allies, foes, kCraftName[ce]);
        if (allies > 0)
            snprintf(m->dyn[5], sizeof(m->dyn[5]), "ALIADOS: %d %s",
                     allies, kCraftName[cp]);
        else
            snprintf(m->dyn[5], sizeof(m->dyn[5]), "SIN ALIADOS");
    }
}

/* M19: RESULT series line follows round/won/best (clamped for display). */
static void duel_refresh_series(DuelMenu *m) {
    int best = (m->series_best == 1 || m->series_best == 5) ? m->series_best : 3;
    int round = m->series_round < 1 ? 1 : m->series_round;
    int won = m->series_won < 0 ? 0 : m->series_won;
    snprintf(m->series_line, sizeof(m->series_line), "RONDA %d DE %d GANADAS: %d",
             round, best, won);
}

void DuelMenu_Init(DuelMenu *m) {
    memset(m, 0, sizeof(*m));
    m->state = DUEL_APP_MENU;
    m->options_from = DUEL_APP_MENU;
    m->sel = 0;
    m->hover = -1;
    m->difficulty = DUEL_DIFF_ROOKIE; /* M9: EASY by default (user order) */
    /* M31: 3v3 by default (the frozen M5/M6/M9 battle). */
    m->allies_n = DUEL_ALLIES_DEFAULT;
    m->foes_n = DUEL_FOES_DEFAULT;
    m->duel_mode = 0; /* MP-B1: skirmish vs AI by default */
    m->craft_player = DUEL_CRAFT_XWING; /* M28 setup */
    m->craft_enemy = DUEL_CRAFT_TIEIN;
    m->series_best = 3; /* M19 rounds default */
    m->series_round = 0;
    m->series_won = 0;
    duel_refresh_series(m);
    m->vol_master = 0.8f;
    m->vol_music = 0.65f;
    m->vol_sfx = 1.0f;
    m->sens = 1.0f;
    m->ray_on = 1; /* M35 controller ray selects by default */
    m->hand_left = 0; /* M36 right-handed by default (right stick flies) */
    m->dead = 0.15f;
    m->pilot_idx = -1;
    DuelPilot_Defaults(&m->pilot, "");
    DuelMenu_RefreshPilots(m);
    duel_refresh_brief(m);
}

void DuelMenu_RefreshPilots(DuelMenu *m) {
    int i;
    if (m->pilot_idx >= 0 && m->pilot_idx < m->npilots) {
        snprintf(m->dyn[0], sizeof(m->dyn[0]), "PILOTO: %s",
                 m->pilot_names[m->pilot_idx]);
        snprintf(m->dyn[1], sizeof(m->dyn[1]), "V:%d D:%d BAJAS:%d",
                 m->pilot.wins, m->pilot.losses, m->pilot.kills);
    } else {
        snprintf(m->dyn[0], sizeof(m->dyn[0]), "SIN PILOTO");
        snprintf(m->dyn[1], sizeof(m->dyn[1]), "CREA UNO PARA GUARDAR");
    }
    /* BRIEF header + RESULT footer share the pilot line. */
    snprintf(m->dyn[2], sizeof(m->dyn[2]), "%s", m->dyn[0]);
    snprintf(m->dyn[3], sizeof(m->dyn[3]), "%s", m->dyn[0]);
    for (i = 0; i < m->npilots && i < DUEL_PILOT_MAX; i++) {
        if (i == m->pilot_idx)
            snprintf(m->rlab[i], sizeof(m->rlab[i]), "%s *",
                     m->pilot_names[i]);
        else
            snprintf(m->rlab[i], sizeof(m->rlab[i]), "%s",
                     m->pilot_names[i]);
    }
}

void DuelMenu_SetState(DuelMenu *m, DuelAppState s) {
    m->state = s;
    m->sel = 0;
    m->hover = -1;
    duel_refresh_brief(m); /* M10: brief lines track battle size */
    duel_refresh_series(m); /* M19: series line tracks round/won/best */
    /* Land selection on the first selectable row. */
    {
        DuelMenuRow rows[DUEL_MENU_MAXROWS];
        int n = DuelMenu_Rows(m, rows, DUEL_MENU_MAXROWS);
        int i;
        for (i = 0; i < n; i++) {
            if (rows[i].kind != DUEL_ROW_LABEL) { m->sel = i; break; }
        }
    }
}

const char *DuelMenu_Title(const DuelMenu *m) {
    switch (m->state) {
    case DUEL_APP_MENU: return "XWADUELVR";
    case DUEL_APP_BRIEF: return "COMBAT SIMULATION";
    case DUEL_APP_OPTIONS: return "OPCIONES";
    case DUEL_APP_PILOTS: return "PILOTOS";
    case DUEL_APP_PAUSED: return "PAUSA";
    /* M34: defeat vs AI asks CONTINUE? (the battle is held behind
     * the screen); the decided duel keeps DERROTA (no continue).
     * ASCII only: the stroke font blanks unknown glyphs. */
    case DUEL_APP_RESULT:
        if (m->result_win) return "VICTORIA";
        return m->duel_mode == 1 ? "DERROTA" : "CONTINUAR?";
    default: return "";
    }
}

int DuelMenu_Rows(const DuelMenu *m, DuelMenuRow *out, int cap) {
    int n = 0, i;
#define ROWA(k, lab, i, a) do { \
        if (n < cap) { out[n].kind = (k); out[n].label = (lab); out[n].id = (i); out[n].arg = (a); } \
        n++; \
    } while (0)
#define ROW(k, lab, i) ROWA(k, lab, i, 0)
    switch (m->state) {
    case DUEL_APP_MENU:
        ROW(DUEL_ROW_ACTION, "COMBAT SIMULATION", DUEL_ACT_ENTER_BRIEF);
        ROW(DUEL_ROW_ACTION, "PILOTO", DUEL_ACT_ENTER_PILOTS);
        ROW(DUEL_ROW_ACTION, "OPCIONES", DUEL_ACT_ENTER_OPTIONS);
        ROW(DUEL_ROW_ACTION, "SALIR", DUEL_ACT_EXIT_APP);
        break;
    case DUEL_APP_BRIEF:
        ROW(DUEL_ROW_LABEL, m->dyn[2], DUEL_ACT_NONE);
        ROW(DUEL_ROW_LABEL, m->dyn[4], DUEL_ACT_NONE);
        ROW(DUEL_ROW_LABEL, m->dyn[5], DUEL_ACT_NONE);
        ROW(DUEL_ROW_CHOICE, "DIFICULTAD", DUEL_ACT_NONE);
        ROW(DUEL_ROW_CHOICE, "ALIADOS", DUEL_ACT_NONE);
        ROW(DUEL_ROW_CHOICE, "ENEMIGOS", DUEL_ACT_NONE);
        ROW(DUEL_ROW_CHOICE, "RONDAS", DUEL_ACT_NONE);
        ROW(DUEL_ROW_CHOICE, "MODO", DUEL_ACT_NONE);
        ROW(DUEL_ROW_CHOICE, "MI NAVE", DUEL_ACT_NONE);
        ROW(DUEL_ROW_CHOICE, "ENEMIGA", DUEL_ACT_NONE);
        ROW(DUEL_ROW_ACTION, "INICIAR", DUEL_ACT_START);
        ROW(DUEL_ROW_ACTION, "VOLVER", DUEL_ACT_BACK);
        break;
    case DUEL_APP_OPTIONS:
        ROW(DUEL_ROW_SLIDER, "VOLUMEN", DUEL_ACT_NONE);
        ROW(DUEL_ROW_SLIDER, "MUSICA", DUEL_ACT_NONE);
        ROW(DUEL_ROW_SLIDER, "EFECTOS", DUEL_ACT_NONE);
        ROW(DUEL_ROW_SLIDER, "SENSIBILIDAD", DUEL_ACT_NONE);
        ROW(DUEL_ROW_SLIDER, "ZONA MUERTA", DUEL_ACT_NONE);
        ROW(DUEL_ROW_CHOICE, "RAYO MANDO", DUEL_ACT_NONE);
        ROW(DUEL_ROW_CHOICE, "MANO", DUEL_ACT_NONE);
        ROW(DUEL_ROW_ACTION, "RECENTRAR", DUEL_ACT_RECENTER);
        ROW(DUEL_ROW_ACTION, "VOLVER", DUEL_ACT_BACK);
        break;
    case DUEL_APP_PILOTS:
        ROW(DUEL_ROW_LABEL, m->dyn[0], DUEL_ACT_NONE);
        ROW(DUEL_ROW_LABEL, m->dyn[1], DUEL_ACT_NONE);
        for (i = 0; i < m->npilots && i < DUEL_PILOT_MAX; i++)
            ROWA(DUEL_ROW_ACTION, m->rlab[i], DUEL_ACT_SELECT_PILOT, i);
        ROW(DUEL_ROW_ACTION, "NUEVO PILOTO", DUEL_ACT_NEW_PILOT);
        ROW(DUEL_ROW_ACTION, "ELIMINAR", DUEL_ACT_DELETE_PILOT);
        ROW(DUEL_ROW_ACTION, "VOLVER", DUEL_ACT_BACK);
        break;
    case DUEL_APP_PAUSED:
        ROW(DUEL_ROW_ACTION, "REANUDAR", DUEL_ACT_RESUME);
        ROW(DUEL_ROW_ACTION, "REINICIAR", DUEL_ACT_RESTART);
        ROW(DUEL_ROW_ACTION, "RECENTRAR", DUEL_ACT_RECENTER);
        ROW(DUEL_ROW_ACTION, "OPCIONES", DUEL_ACT_ENTER_OPTIONS);
        ROW(DUEL_ROW_ACTION, "MENU", DUEL_ACT_QUIT_TO_MENU);
        break;
    case DUEL_APP_RESULT:
        ROW(DUEL_ROW_LABEL, m->dyn[3], DUEL_ACT_NONE);
        ROW(DUEL_ROW_LABEL, m->series_line, DUEL_ACT_NONE);
        /* M34: defeat vs AI offers CONTINUAR first (respawn into the
         * held battle); victory and the decided duel keep REPETIR. */
        if (!m->result_win && m->duel_mode != 1)
            ROW(DUEL_ROW_ACTION, "CONTINUAR", DUEL_ACT_CONTINUE);
        ROW(DUEL_ROW_ACTION, "REPETIR", DUEL_ACT_REPEAT);
        ROW(DUEL_ROW_ACTION, "MENU", DUEL_ACT_QUIT_TO_MENU);
        break;
    default:
        break;
    }
#undef ROW
#undef ROWA
    return n;
}

int DuelMenu_RowArg(const DuelMenu *m, int row) {
    DuelMenuRow rows[DUEL_MENU_MAXROWS];
    int n = DuelMenu_Rows(m, rows, DUEL_MENU_MAXROWS);
    if (row < 0 || row >= n) return -1;
    if (rows[row].id != DUEL_ACT_SELECT_PILOT) return -1;
    return rows[row].arg;
}

float DuelMenu_PanelH(const DuelMenu *m) {
    DuelMenuRow rows[DUEL_MENU_MAXROWS];
    int n = DuelMenu_Rows(m, rows, DUEL_MENU_MAXROWS);
    /* RESULT shows a kills line under the title (same slot as a row). */
    if (m->state == DUEL_APP_RESULT) n++;
    return DUEL_MENU_PAD + DUEL_MENU_TITLE_H + DUEL_MENU_ROW_GAP +
        (float)n * (DUEL_MENU_ROW_H + DUEL_MENU_ROW_GAP) + DUEL_MENU_PAD;
}

/* Row i top edge, panel-local y (title block first, then rows). */
static float duel_row_top(const DuelMenu *m, int i) {
    float h = DuelMenu_PanelH(m);
    float y = h * 0.5f - DUEL_MENU_PAD - DUEL_MENU_TITLE_H - DUEL_MENU_ROW_GAP;
    if (m->state == DUEL_APP_RESULT)
        y -= DUEL_MENU_ROW_H + DUEL_MENU_ROW_GAP; /* kills line */
    return y - (float)i * (DUEL_MENU_ROW_H + DUEL_MENU_ROW_GAP);
}

int DuelMenu_RowRect(const DuelMenu *m, int row,
                     float *x0, float *y0, float *x1, float *y1) {
    DuelMenuRow rows[DUEL_MENU_MAXROWS];
    int n = DuelMenu_Rows(m, rows, DUEL_MENU_MAXROWS);
    float top;
    if (row < 0 || row >= n) return 0;
    if (rows[row].kind == DUEL_ROW_LABEL) return 0;
    top = duel_row_top(m, row);
    if (x0) *x0 = -DUEL_MENU_PANEL_W * 0.5f + DUEL_MENU_PAD;
    if (x1) *x1 = DUEL_MENU_PANEL_W * 0.5f - DUEL_MENU_PAD;
    if (y0) *y0 = top - DUEL_MENU_ROW_H;
    if (y1) *y1 = top;
    return 1;
}

int DuelMenu_RayPlane(const DuelMenu *m, DuelVec3 ro, DuelVec3 rd,
                       DuelVec3 c, DuelVec3 right, DuelVec3 up, DuelVec3 n,
                       float *lx, float *ly, float *t) {
    float denom, tt, hx, hy, hw, hh;
    DuelVec3 rel, hit;
    if (!m) return 0;
    denom = duel_v3_dot(rd, n);
    if (denom >= -1e-6f) return 0; /* parallel or facing away */
    rel = duel_v3_sub(c, ro);
    tt = duel_v3_dot(rel, n) / denom;
    if (tt <= 0.0f || tt > 5.0f) return 0;
    hit = duel_v3_add(ro, duel_v3_scale(rd, tt));
    rel = duel_v3_sub(hit, c);
    hx = duel_v3_dot(rel, right);
    hy = duel_v3_dot(rel, up);
    hw = DUEL_MENU_PANEL_W * 0.5f;
    hh = DuelMenu_PanelH(m) * 0.5f;
    if (fabsf(hx) > hw || fabsf(hy) > hh) return 0;
    if (lx) *lx = hx;
    if (ly) *ly = hy;
    if (t) *t = tt;
    return 1;
}

int DuelMenu_RayHit(const DuelMenu *m, DuelVec3 ro, DuelVec3 rd,
                     DuelVec3 c, DuelVec3 right, DuelVec3 up, DuelVec3 n,
                     float *lx, float *ly) {
    float hx, hy;
    if (!DuelMenu_RayPlane(m, ro, rd, c, right, up, n, &hx, &hy, NULL))
        return -1;
    if (lx) *lx = hx;
    if (ly) *ly = hy;
    return DuelMenu_HitRow(m, hx, hy);
}

int DuelMenu_HitRow(const DuelMenu *m, float x, float y) {
    DuelMenuRow rows[DUEL_MENU_MAXROWS];
    int n = DuelMenu_Rows(m, rows, DUEL_MENU_MAXROWS);
    int i;
    for (i = 0; i < n; i++) {
        float x0, y0, x1, y1;
        if (!DuelMenu_RowRect(m, i, &x0, &y0, &x1, &y1)) continue;
        if (x >= x0 && x <= x1 && y >= y0 && y <= y1) return i;
    }
    return -1;
}

void DuelMenu_Nav(DuelMenu *m, int dir) {
    DuelMenuRow rows[DUEL_MENU_MAXROWS];
    int n = DuelMenu_Rows(m, rows, DUEL_MENU_MAXROWS);
    int i, guard = 0;
    if (n == 0) return;
    if (dir == 0) return;
    i = m->sel;
    do {
        i += (dir > 0) ? 1 : -1;
        if (i < 0) i = n - 1;
        if (i >= n) i = 0;
        if (++guard > n + 1) return;
    } while (rows[i].kind == DUEL_ROW_LABEL);
    m->sel = i;
}

DuelMenuAction DuelMenu_ActivateRow(DuelMenu *m, int row) {
    DuelMenuRow rows[DUEL_MENU_MAXROWS];
    int n = DuelMenu_Rows(m, rows, DUEL_MENU_MAXROWS);
    if (row < 0 || row >= n) return DUEL_ACT_NONE;
    if (rows[row].kind == DUEL_ROW_LABEL) return DUEL_ACT_NONE;
    if (rows[row].kind == DUEL_ROW_CHOICE) {
        /* Click cycles forward (same as Adjust +1) on THIS row. */
        m->sel = row;
        DuelMenu_Adjust(m, 1);
        return DUEL_ACT_NONE;
    }
    if (rows[row].kind == DUEL_ROW_SLIDER) {
        /* Click alone does nothing on sliders (drag or adjust). */
        m->sel = row;
        return DUEL_ACT_NONE;
    }
    return (DuelMenuAction)rows[row].id;
}

int DuelMenu_Highlighted(const DuelMenu *m) {
    if (!m) return -1;
    return m->hover >= 0 ? m->hover : m->sel;
}

DuelMenuAction DuelMenu_Activate(DuelMenu *m) {
    return DuelMenu_ActivateRow(m, DuelMenu_Highlighted(m));
}

static float duel_clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

void DuelMenu_Adjust(DuelMenu *m, int dir) {
    DuelMenuRow rows[DUEL_MENU_MAXROWS];
    int n = DuelMenu_Rows(m, rows, DUEL_MENU_MAXROWS);
    float step = (dir > 0) ? 1.0f : -1.0f;
    if (m->sel < 0 || m->sel >= n) return;
    if (rows[m->sel].kind == DUEL_ROW_CHOICE) {
        /* M10: choice rows cycle by label (M31: + ALIADOS + ENEMIGOS). */
        if (!strcmp(rows[m->sel].label, "RONDAS")) {
            if (dir > 0)
                m->series_best = m->series_best >= 5 ? 1 :
                    (m->series_best >= 3 ? 5 : 3);
            else
                m->series_best = m->series_best <= 1 ? 5 :
                    (m->series_best <= 3 ? 1 : 3);
            duel_refresh_series(m);
            return;
        }
        if (!strcmp(rows[m->sel].label, "MODO")) {
            int step = dir > 0 ? 1 : -1; /* M30: 3 modes */
            m->duel_mode = (m->duel_mode + step + 3) % 3;
            duel_refresh_brief(m);
            return;
        }
        if (!strcmp(rows[m->sel].label, "MI NAVE")) {
            int step = dir > 0 ? 1 : -1;
            m->craft_player = (m->craft_player + step + DUEL_CRAFT_COUNT) %
                DUEL_CRAFT_COUNT;
            duel_refresh_brief(m);
            return;
        }
        if (!strcmp(rows[m->sel].label, "ENEMIGA")) {
            int step = dir > 0 ? 1 : -1;
            /* M32: locked in a guns-only cruiser (no enemy ship). */
            if (m->duel_mode == 2 && m->foes_n == 0) return;
            m->craft_enemy = (m->craft_enemy + step + DUEL_CRAFT_COUNT) %
                DUEL_CRAFT_COUNT;
            duel_refresh_brief(m);
            return;
        }
        if (!strcmp(rows[m->sel].label, "ALIADOS")) {
            int step = dir > 0 ? 1 : -1;
            m->allies_n = (m->allies_n + step +
                           (DUEL_ALLIES_MAX + 1)) % (DUEL_ALLIES_MAX + 1);
            duel_refresh_brief(m);
            return;
        }
        if (!strcmp(rows[m->sel].label, "ENEMIGOS")) {
            int step = dir > 0 ? 1 : -1;
            m->foes_n = (m->foes_n + step +
                         (DUEL_FOES_MAX + 1)) % (DUEL_FOES_MAX + 1);
            duel_refresh_brief(m);
            return;
        }
        /* M35: RAYO MANDO toggles controller-ray selection. */
        if (!strcmp(rows[m->sel].label, "RAYO MANDO")) {
            m->ray_on = m->ray_on ? 0 : 1;
            return;
        }
        /* M36: MANO picks the flying hand (sticks mirror). */
        if (!strcmp(rows[m->sel].label, "MANO")) {
            m->hand_left = m->hand_left ? 0 : 1;
            return;
        }
        m->difficulty += (dir > 0) ? 1 : -1;
        if (m->difficulty < DUEL_DIFF_ROOKIE) m->difficulty = DUEL_DIFF_ACE;
        if (m->difficulty > DUEL_DIFF_ACE) m->difficulty = DUEL_DIFF_ROOKIE;
        return;
    }
    if (rows[m->sel].kind != DUEL_ROW_SLIDER) return;
    switch (m->state) {
    case DUEL_APP_OPTIONS:
        if (!strcmp(rows[m->sel].label, "VOLUMEN"))
            m->vol_master = duel_clamp01(m->vol_master + step * 0.1f);
        else if (!strcmp(rows[m->sel].label, "MUSICA"))
            m->vol_music = duel_clamp01(m->vol_music + step * 0.1f);
        else if (!strcmp(rows[m->sel].label, "EFECTOS"))
            m->vol_sfx = duel_clamp01(m->vol_sfx + step * 0.1f);
        else if (!strcmp(rows[m->sel].label, "SENSIBILIDAD")) {
            m->sens += step * 0.1f;
            if (m->sens < 0.5f) m->sens = 0.5f;
            if (m->sens > 1.5f) m->sens = 1.5f;
        } else if (!strcmp(rows[m->sel].label, "ZONA MUERTA")) {
            m->dead += step * 0.025f;
            if (m->dead < 0.05f) m->dead = 0.05f;
            if (m->dead > 0.30f) m->dead = 0.30f;
        }
        break;
    default:
        break;
    }
}

void DuelMenu_SetSlider(DuelMenu *m, int row, float frac) {
    DuelMenuRow rows[DUEL_MENU_MAXROWS];
    int n = DuelMenu_Rows(m, rows, DUEL_MENU_MAXROWS);
    if (row < 0 || row >= n) return;
    if (rows[row].kind != DUEL_ROW_SLIDER) return;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    if (!strcmp(rows[row].label, "VOLUMEN")) m->vol_master = frac;
    else if (!strcmp(rows[row].label, "MUSICA")) m->vol_music = frac;
    else if (!strcmp(rows[row].label, "EFECTOS")) m->vol_sfx = frac;
    else if (!strcmp(rows[row].label, "SENSIBILIDAD")) m->sens = 0.5f + frac;
    else if (!strcmp(rows[row].label, "ZONA MUERTA")) m->dead = 0.05f + frac * 0.25f;
}

void DuelMenu_RowValue(const DuelMenu *m, int row, char *out, int cap) {
    DuelMenuRow rows[DUEL_MENU_MAXROWS];
    int n = DuelMenu_Rows(m, rows, DUEL_MENU_MAXROWS);
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (row < 0 || row >= n) return;
    if (rows[row].kind == DUEL_ROW_ACTION &&
            !strcmp(rows[row].label, "PILOTO")) {
        snprintf(out, (size_t)cap, "%s",
                 (m->pilot_idx >= 0 && m->pilot.name[0]) ? m->pilot.name : "-");
    } else if (rows[row].kind == DUEL_ROW_CHOICE) {
        /* M19: RONDAS shows the series length. */
        if (!strcmp(rows[row].label, "RONDAS")) {
            int b = (m->series_best == 1 || m->series_best == 5) ?
                m->series_best : 3;
            snprintf(out, (size_t)cap, "%d", b);
        /* MP-B1: MODO shows ESCARAMUZA/DUELO (+M30 CRUCERO). */
        } else if (!strcmp(rows[row].label, "MODO")) {
            snprintf(out, (size_t)cap, "%s",
                     m->duel_mode == 2 ? "CRUCERO" :
                     m->duel_mode == 1 ? "DUELO" : "ESCARAMUZA");
        } else if (!strcmp(rows[row].label, "MI NAVE")) {
            int c = (m->craft_player >= 0 && m->craft_player < DUEL_CRAFT_COUNT) ?
                m->craft_player : DUEL_CRAFT_XWING;
            snprintf(out, (size_t)cap, "%s", kCraftName[c]);
        } else if (!strcmp(rows[row].label, "ENEMIGA")) {
            int c = (m->craft_enemy >= 0 && m->craft_enemy < DUEL_CRAFT_COUNT) ?
                m->craft_enemy : DUEL_CRAFT_TIEIN;
            if (m->duel_mode == 2 && m->foes_n == 0)
                snprintf(out, (size_t)cap, "---");
            else
                snprintf(out, (size_t)cap, "%s", kCraftName[c]);
        /* M31: force counts. */
        } else if (!strcmp(rows[row].label, "ALIADOS")) {
            int w = m->allies_n;
            if (w < DUEL_ALLIES_MIN || w > DUEL_ALLIES_MAX)
                w = DUEL_ALLIES_DEFAULT;
            snprintf(out, (size_t)cap, "%d", w);
        } else if (!strcmp(rows[row].label, "ENEMIGOS")) {
            int w = m->foes_n;
            if (w < DUEL_FOES_MIN || w > DUEL_FOES_MAX)
                w = DUEL_FOES_DEFAULT;
            snprintf(out, (size_t)cap, "%d", w);
        } else if (!strcmp(rows[row].label, "RAYO MANDO")) {
            snprintf(out, (size_t)cap, "%s", m->ray_on ? "SI" : "NO");
        } else if (!strcmp(rows[row].label, "MANO")) {
            snprintf(out, (size_t)cap, "%s", m->hand_left ? "ZURDA" : "DIESTRA");
        } else {
            snprintf(out, (size_t)cap, "%s", kDiffName[m->difficulty]);
        }
    } else if (rows[row].kind == DUEL_ROW_SLIDER) {
        float v = 0.0f;
        if (!strcmp(rows[row].label, "VOLUMEN")) v = m->vol_master;
        else if (!strcmp(rows[row].label, "MUSICA")) v = m->vol_music;
        else if (!strcmp(rows[row].label, "EFECTOS")) v = m->vol_sfx;
        else if (!strcmp(rows[row].label, "SENSIBILIDAD")) v = m->sens - 0.5f;
        else if (!strcmp(rows[row].label, "ZONA MUERTA"))
            v = (m->dead - 0.05f) / 0.25f;
        /* Percent of the row's own range (sens shows 0-100% of 0.5-1.5). */
        snprintf(out, (size_t)cap, "%d%%", (int)(v * 100.0f + 0.5f));
    }
}
