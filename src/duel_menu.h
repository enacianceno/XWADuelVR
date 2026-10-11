/* XWADuelVR M7 menu: app states, panel layout, selection logic.
 *
 * SDL-free and render-free: pure layout + state machine + input mapping.
 * The renderer draws what DuelMenu describes; main executes actions.
 * Panel is world-locked (placed once per screen, never head-chasing).
 */
#ifndef XWADUELVR_MENU_H
#define XWADUELVR_MENU_H

#include "duel_math.h"
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
    DUEL_ACT_CONTINUE,   /* M34 result-defeat -> respawn into the same battle */
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
    int allies_n;             /* M31: wingmen 0..4 (default 2) */
    int foes_n;               /* M31: enemies 0..5 (default 3) */
    int duel_mode;            /* 0 skirmish vs AI, 1 PvP duel, 2 cruiser (M30) */
    int craft_player;         /* M28: DuelCraft MI NAVE (default X-Wing) */
    int craft_enemy;          /* M28: DuelCraft ENEMIGA (default TIE INT) */
    float vol_master, vol_music, vol_sfx; /* 0..1 */
    float sens;               /* 0.5..1.5, 1 = baseline */
    float dead;               /* 0.05..0.30, 0.15 = baseline */
    int ray_on;               /* M35: controller ray selects in menus (default 1) */
    int hand_left;            /* M36: 0 right-handed (right stick flies),
                                 * 1 left-handed (sticks mirrored) */
    int result_win;           /* RESULT screen: 1 victory, 0 defeat */
    int result_kills;         /* player kills this combat */
    /* M19 series: best-of length (1/3/5, default 3), current round
     * (1-based, 0 = no series yet), rounds won, RESULT line. */
    int series_best;
    int series_round;
    int series_won;
    char series_line[40];
    /* Pilot context (main owns the files; menu formats the rows). */
    char pilot_names[DUEL_PILOT_MAX][DUEL_PILOT_NAME_MAX + 1];
    int npilots;
    int pilot_idx;            /* current pilot, -1 none */
    DuelPilot pilot;          /* loaded stats of pilot_names[pilot_idx] */
    /* Dynamic label lines: [0..1] pilots screen, [2] brief pilot,
     * [3] result pilot, [4] brief enemy line, [5] brief ally line. */
    char dyn[6][40];
    char rlab[DUEL_PILOT_MAX][DUEL_PILOT_NAME_MAX + 4]; /* pilot rows */
} DuelMenu;

/* Panel geometry (meters, panel-local: x right, y up, origin center).
 * M11: near touch panel 0.5 m from the eyes, cockpit-fixed (same frame
 * as the ship: centered where the ship is, never runs away). Geometry
 * keeps the 1.1 m design's angular size (x0.4545), so text/rows read
 * identically up close.
 * M16: the whole panel is 30% smaller (user order): one scale knob
 * over the M11 design values. DIST stays 0.5 m.
 * M19: 40% smaller still (x0.6 of M16, user order). */
#define DUEL_MENU_SCALE 0.42f
#define DUEL_MENU_DIST 0.5f
#define DUEL_MENU_PANEL_W (0.773f * DUEL_MENU_SCALE)
#define DUEL_MENU_ROW_H (0.0477f * DUEL_MENU_SCALE)
#define DUEL_MENU_ROW_GAP (0.0136f * DUEL_MENU_SCALE)
#define DUEL_MENU_TITLE_H (0.0591f * DUEL_MENU_SCALE)
#define DUEL_MENU_PAD (0.0409f * DUEL_MENU_SCALE)
/* M18: the M16 top handle strip is gone (user order: grab from the
 * sides only). Grip ticks ride on the side borders (render-side). */

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
/* M35 controller ray: intersect (ro + t*rd) with the panel plane
 * (center c, right/up axes, user-side normal n); on a hit inside the
 * panel write (lx, ly) and return the row, else -1. Pure/testable.
 * M38: RayPlane splits the plane hit (lx, ly, t) from the row lookup
 * so the beam can die exactly where the dot lands. */
int DuelMenu_RayPlane(const DuelMenu *m, DuelVec3 ro, DuelVec3 rd,
                       DuelVec3 c, DuelVec3 right, DuelVec3 up, DuelVec3 n,
                       float *lx, float *ly, float *t);
int DuelMenu_RayHit(const DuelMenu *m, DuelVec3 ro, DuelVec3 rd,
                     DuelVec3 c, DuelVec3 right, DuelVec3 up, DuelVec3 n,
                     float *lx, float *ly);
/* Which selectable row contains a panel-local point (-1 = none). */
int DuelMenu_HitRow(const DuelMenu *m, float x, float y);
/* Controller nav: move selection (+1/-1), skipping labels. */
void DuelMenu_Nav(DuelMenu *m, int dir);
/* Visible highlight: the hand-hovered row when pointing, else the
 * controller-selected row. Controllers AND hands activate this row so
 * the highlight always matches the action (M9 selection fix). */
int DuelMenu_Highlighted(const DuelMenu *m);
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
