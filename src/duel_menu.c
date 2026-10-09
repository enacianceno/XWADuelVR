/* XWADuelVR M7 menu bodies. See duel_menu.h. */
#include "duel_menu.h"

#include <stdio.h>
#include <string.h>

static const char *kDiffName[3] = { "FACIL", "NORMAL", "DIFICIL" };

void DuelMenu_Init(DuelMenu *m) {
    memset(m, 0, sizeof(*m));
    m->state = DUEL_APP_MENU;
    m->options_from = DUEL_APP_MENU;
    m->sel = 0;
    m->hover = -1;
    m->difficulty = DUEL_DIFF_PILOT;
    m->vol_master = 0.8f;
    m->vol_music = 0.65f;
    m->vol_sfx = 1.0f;
    m->sens = 1.0f;
    m->dead = 0.15f;
    m->pilot_idx = -1;
    DuelPilot_Defaults(&m->pilot, "");
    DuelMenu_RefreshPilots(m);
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
    case DUEL_APP_RESULT: return m->result_win ? "VICTORIA" : "DERROTA";
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
        ROW(DUEL_ROW_LABEL, "X-WING VS 3 TIE", DUEL_ACT_NONE);
        ROW(DUEL_ROW_LABEL, "ALIADOS: 2 X-WING", DUEL_ACT_NONE);
        ROW(DUEL_ROW_CHOICE, "DIFICULTAD", DUEL_ACT_NONE);
        ROW(DUEL_ROW_ACTION, "INICIAR", DUEL_ACT_START);
        ROW(DUEL_ROW_ACTION, "VOLVER", DUEL_ACT_BACK);
        break;
    case DUEL_APP_OPTIONS:
        ROW(DUEL_ROW_SLIDER, "VOLUMEN", DUEL_ACT_NONE);
        ROW(DUEL_ROW_SLIDER, "MUSICA", DUEL_ACT_NONE);
        ROW(DUEL_ROW_SLIDER, "EFECTOS", DUEL_ACT_NONE);
        ROW(DUEL_ROW_SLIDER, "SENSIBILIDAD", DUEL_ACT_NONE);
        ROW(DUEL_ROW_SLIDER, "ZONA MUERTA", DUEL_ACT_NONE);
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
        ROW(DUEL_ROW_ACTION, "OPCIONES", DUEL_ACT_ENTER_OPTIONS);
        ROW(DUEL_ROW_ACTION, "MENU", DUEL_ACT_QUIT_TO_MENU);
        break;
    case DUEL_APP_RESULT:
        ROW(DUEL_ROW_LABEL, m->dyn[3], DUEL_ACT_NONE);
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
        /* Click cycles forward (same as Adjust +1). */
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

DuelMenuAction DuelMenu_Activate(DuelMenu *m) {
    return DuelMenu_ActivateRow(m, m->sel);
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
        snprintf(out, (size_t)cap, "%s", kDiffName[m->difficulty]);
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
