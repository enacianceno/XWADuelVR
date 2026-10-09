/* XWADuelVR M7 menu: app states, panel layout, selection logic.
 *
 * SDL-free and render-free: pure layout + state machine + input mapping.
 * The renderer draws what DuelMenu describes; main executes actions.
 * Panel is world-locked (placed once per screen, never head-chasing).
 */
#ifndef XWADUELVR_MENU_H
#define XWADUELVR_MENU_H

#include "duel_profile.h"

typedef enum DuelAppState {
    DUEL_APP_MENU = 0, /* main menu */
    DUEL_APP_BRIEF,    /* scenario + difficulty + start */
    DUEL_APP_OPTIONS,  /* options (returns to options_from) */
    DUEL_APP_PILOTS,   /* pilot select/create/delete */
    DUEL_APP_COMBAT,
    DUEL_APP_PAUSED,
    DUEL_APP_RESULT
} DuelAppState;

typedef enum DuelRowKind {
    DUEL_ROW_ACTION = 0, /* button: activate runs it */
    DUEL_ROW_CHOICE,     /* cycles discrete values */
    DUEL_ROW_SLIDER,     /* continuous 0..1 */
    DUEL_ROW_LABEL       /* info only, never selectable */
} DuelRowKind;

/* Actions returned to main (DuelMenu_Activate / hand click). */
typedef enum DuelMenuAction {
    DUEL_ACT_NONE = 0,
    DUEL_ACT_ENTER_BRIEF,
    DUEL_ACT_ENTER_OPTIONS,
    DUEL_ACT_EXIT_APP,
    DUEL_ACT_START,      /* begin combat (brief -> combat) */
    DUEL_ACT_BACK,       /* brief/options -> previous */
    DUEL_ACT_RESUME,
    DUEL_ACT_RESTART,    /* restart combat now */
    DUEL_ACT_QUIT_TO_MENU,
    DUEL_ACT_RECENTER,
    DUEL_ACT_REPEAT,     /* result -> combat again */
    DUEL_ACT_ENTER_PILOTS,
    DUEL_ACT_SELECT_PILOT, /* row arg = pilot index */
    DUEL_ACT_NEW_PILOT,
    DUEL_ACT_DELETE_PILOT
} DuelMenuAction;

typedef enum DuelDifficulty {
    DUEL_DIFF_ROOKIE = 0,
    DUEL_DIFF_PILOT,     /* baseline: multipliers all 1.0 */
    DUEL_DIFF_ACE
} DuelDifficulty;

#define DUEL_MENU_MAXROWS 16

typedef struct DuelMenuRow {
    DuelRowKind kind;
    const char *label; /* static or menu-owned storage, CAPS-ASCII */
    int id;            /* DuelMenuAction for ACTION rows */
    int arg;           /* SELECT_PILOT pilot index, else 0 */
} DuelMenuRow;

typedef struct DuelMenu {
    DuelAppState state;
    DuelAppState options_from; /* where BACK in options returns */
    int sel;                  /* selected row (controllers) */
    int hover;                /* hand-ray row, -1 none (click target) */
    /* Live option values (main applies + persists via profile). */
    int difficulty;           /* DuelDifficulty */
    float vol_master, vol_music, vol_sfx; /* 0..1 */
    float sens;               /* 0.5..1.5, 1 = baseline */
    float dead;               /* 0.05..0.30, 0.15 = baseline */
    int result_win;           /* RESULT screen: 1 victory, 0 defeat */
    int result_kills;         /* player kills this combat */
    /* Pilot context (main owns the files; menu formats the rows). */
    char pilot_names[DUEL_PILOT_MAX][DUEL_PILOT_NAME_MAX + 1];
    int npilots;
    int pilot_idx;            /* current pilot, -1 none */
    DuelPilot pilot;          /* loaded stats of pilot_names[pilot_idx] */
    char dyn[4][40];          /* dynamic label lines (RefreshPilots) */
    char rlab[DUEL_PILOT_MAX][DUEL_PILOT_NAME_MAX + 4]; /* pilot rows */
} DuelMenu;

/* Panel geometry (meters, panel-local: x right, y up, origin center). */
#define DUEL_MENU_PANEL_W 1.70f
#define DUEL_MENU_ROW_H 0.105f
#define DUEL_MENU_ROW_GAP 0.030f
#define DUEL_MENU_TITLE_H 0.13f
#define DUEL_MENU_PAD 0.09f

void DuelMenu_Init(DuelMenu *m);
void DuelMenu_SetState(DuelMenu *m, DuelAppState s);
/* Rebuild dynamic pilot labels after main changes pilot data. */
void DuelMenu_RefreshPilots(DuelMenu *m);
/* Row arg (SELECT_PILOT pilot index; -1 when the row has none). */
int DuelMenu_RowArg(const DuelMenu *m, int row);
/* Rows of the current screen (never NULL labels). */
int DuelMenu_Rows(const DuelMenu *m, DuelMenuRow *out, int cap);
/* Panel height for the current screen (width is fixed). */
float DuelMenu_PanelH(const DuelMenu *m);
/* Selectable-row rect in panel-local coords (0/NULL when not found). */
int DuelMenu_RowRect(const DuelMenu *m, int row,
                     float *x0, float *y0, float *x1, float *y1);
/* Which selectable row contains a panel-local point (-1 = none). */
int DuelMenu_HitRow(const DuelMenu *m, float x, float y);
/* Controller nav: move selection (+1/-1), skipping labels. */
void DuelMenu_Nav(DuelMenu *m, int dir);
/* Activate the controller-selected row. */
DuelMenuAction DuelMenu_Activate(DuelMenu *m);
/* Activate an explicit (hand-hovered) row. */
DuelMenuAction DuelMenu_ActivateRow(DuelMenu *m, int row);
/* Left/right on the selected row (choice cycle / slider step). */
void DuelMenu_Adjust(DuelMenu *m, int dir);
/* Hand drag: set a slider row from a 0..1 fraction across its rect. */
void DuelMenu_SetSlider(DuelMenu *m, int row, float frac);
/* Choice/slider display value, e.g. "NORMAL", "80%". */
void DuelMenu_RowValue(const DuelMenu *m, int row, char *out, int cap);
/* Screen title (RESULT depends on result_win). */
const char *DuelMenu_Title(const DuelMenu *m);

#endif /* XWADUELVR_MENU_H */
