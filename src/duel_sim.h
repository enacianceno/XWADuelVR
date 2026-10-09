/* XWADuelVR simulation: player X-Wing, TIE AI, lasers, collisions, reset.
 * Pure C, no engine/render dependencies. Ship convention: forward=-Z. */
#ifndef XWADUELVR_SIM_H
#define XWADUELVR_SIM_H

#include "duel_math.h"
#include "flight_config.h"

typedef enum DuelShipKind { DUEL_SHIP_PLAYER = 0, DUEL_SHIP_TIE = 1,
                            DUEL_SHIP_ALLY = 2 } DuelShipKind;

/* M5 skirmish: 3 TIEs vs player + 2 X-wing allies. Bolts only ever hit
 * the ENEMY team (rebel bolts -> ties; imperial bolts -> rebels), so no
 * friendly fire in either direction and no shooter-id bookkeeping. */
#define DUEL_TIE_COUNT 3
#define DUEL_ALLY_COUNT 2

typedef struct DuelShip {
    DuelVec3 pos;
    DuelQuat orient;
    float speed;      /* current scalar speed along forward */
    float throttle;   /* 0..1 lever (player) / auto (TIE) */
    float hp;
    float fire_cd;    /* seconds until can fire again */
    int alive;
    int muzzle;       /* alternating muzzle index */
} DuelShip;

typedef struct DuelLaser {
    int active;
    DuelShipKind owner;
    DuelVec3 pos;
    DuelVec3 vel;
    float life;
} DuelLaser;

typedef enum DuelTieState {
    DUEL_TIE_ACQUIRE = 0,
    DUEL_TIE_CHASE,
    DUEL_TIE_ATTACK,
    DUEL_TIE_EVADE,
    DUEL_TIE_REACQUIRE
} DuelTieState;

typedef enum DuelPhase { DUEL_PHASE_COMBAT = 0, DUEL_PHASE_DESTROYED } DuelPhase;

typedef struct DuelInput {
    float yaw;      /* -1..1, left stick X (M8 axes[0]) */
    float pitch;    /* -1..1, -left stick Y (M8 axes[1]); + = nose up */
    float roll;     /* -1..1, right stick X (M8 axes[3]) */
    float thr_axis; /* -1..1, right stick Y: integrates throttle lever */
    int fire;       /* right trigger past hysteresis */
    int menu;       /* M7: left menu button level (pause/menu select) */
    /* P0.4: raw sticks (telemetry; proves the live mapping on-device) */
    float raw_lx, raw_ly, raw_rx, raw_ry;
} DuelInput;

typedef struct DuelState {
    DuelPhase phase;
    float phase_t;      /* time in current phase */
    float time;         /* total sim time */
    DuelShip player;
    DuelShip ties[DUEL_TIE_COUNT];
    DuelShip allies[DUEL_ALLY_COUNT];
    DuelTieState ai[DUEL_TIE_COUNT];
    float ai_t[DUEL_TIE_COUNT]; /* time in current AI state */
    DuelVec3 evade_dir[DUEL_TIE_COUNT]; /* latched evade direction */
    DuelLaser lasers[DUEL_LASER_POOL];
    int kills_player;   /* times player destroyed TIE (session) */
    int kills_tie;
    /* M3.2: physical lever override. While active, the hand OWNS the
     * player throttle absolutely; the stick integrator resumes (from the
     * current value: no jump) when released. Cleared by reset memset. */
    int hand_thr_active;
    float hand_thr_value;
    /* M3.3: right-hand stick owns pitch/yaw absolutely while active
     * (spring-centered: decays on release). M6: lateral is PURE yaw
     * (bank coordination removed by user order); roll moved to the
     * left lever (hand_lev_*), throttle stays on the friction slide. */
    int hand_stk_active;
    float hand_pitch;
    float hand_yaw;
    int hand_lev_active;
    float hand_lev_roll;
    /* M7: difficulty index (0 rookie / 1 pilot / 2 ace; pilot = the
     * frozen baseline, multipliers all 1.0) + runtime hand deadzone
     * (default 0.15 = DUEL_DEADZONE; controllers keep the #define). */
    int diff;
    float hand_dead;
} DuelState;

void Duel_Init(DuelState *s);
void Duel_ResetCombat(DuelState *s);
void Duel_Update(DuelState *s, const DuelInput *in, float dt);
/* Main loop: publish the M3.1 grab lever state before Duel_Update. */
void Duel_HandThrottle(DuelState *s, int active, float value);
/* Main loop: publish the M3.3 stick deflection before Duel_Update. */
void Duel_HandStick(DuelState *s, int active, float pitch, float yaw);
/* Main loop: publish the M6 left-lever roll before Duel_Update. */
void Duel_HandLever(DuelState *s, int active, float roll);
/* M7 menu: difficulty (clamped 0..2, survives combat resets) and the
 * hand-lever deadzone (clamped 0.05..0.30, default 0.15). */
void Duel_SetDifficulty(DuelState *s, int diff);
void Duel_SetHandDead(DuelState *s, float dead);

/* Steering helper (also used by tests): turn 'orient' toward 'dir'. */
DuelQuat Duel_SteerToward(DuelQuat orient, DuelVec3 dir, float max_rad, float dt);

/* M5: nearest living tie to the player (-1 = wave cleared). Drives the
 * target diamond, lead pip, head arrow and HP bar. */
int Duel_AutoTarget(const DuelState *s);
/* M5: living ships per side (wave/pacing logic). */
int Duel_TiesAlive(const DuelState *s);
int Duel_AlliesAlive(const DuelState *s);

/* Muzzle world position for a shot with index 'muzzle_index' (pure; the
 * single source of truth for duel_fire AND the render-side muzzle flash). */
DuelVec3 Duel_MuzzleWorldAt(const DuelShip *sh, DuelShipKind owner,
                            int muzzle_index);

/* Gunsight lead point (XWA-style lead computer): where to aim so a bolt
 * fired now (base speed + shooter forward speed, no lateral inherit)
 * meets the target. First-order (no iteration); muzzle offsets ignored. */
DuelVec3 duel_lead_point(const DuelShip *shooter, const DuelShip *target, float bolt_base_speed);

/* Enemy guide arrow (M2.2c): steering cue around the reticle. Returns 1 +
 * the normalized SHIP-frame (right, up) direction to the target when the
 * arrow should show, 0 when the target is inside the 12-deg nose cone
 * (the diamond marker covers it). Dead astern yields (0,-1) by
 * convention ("behind you"). Pure; HMD-independent (steer, not look). */
int Duel_GuideDir(const DuelShip *player, DuelVec3 target_pos,
                  float *dx, float *dy);
/* M3.8b: head-frame bearing toward the target for the look-cue arrow.
 * Same contract (0 inside the 12-deg view cone, (0,-1) astern). */
int Duel_HeadGuide(DuelQuat head_q, DuelVec3 eye_pos, DuelVec3 target_pos,
                   float *dx, float *dy);

#endif /* XWADUELVR_SIM_H */
