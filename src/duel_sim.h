/* XWADuelVR simulation: player X-Wing, TIE AI, lasers, collisions, reset.
 * Pure C, no engine/render dependencies. Ship convention: forward=-Z. */
#ifndef XWADUELVR_SIM_H
#define XWADUELVR_SIM_H

#include "duel_math.h"
#include "flight_config.h"

/* MP-A: DUEL_SHIP_PEER is the second human (PvP duel): flies the
 * X-Wing set (muzzles/geometry/radius) but counts as imperial team
 * for laser collisions, so the two humans can hit each other. */
typedef enum DuelShipKind { DUEL_SHIP_PLAYER = 0, DUEL_SHIP_TIE = 1,
                            DUEL_SHIP_ALLY = 2, DUEL_SHIP_PEER = 3
} DuelShipKind;

/* M28: flyable craft (XWA skirmish setup). Kind stays the ROLE/team
 * (who can hit whom); craft is the airframe (stats/geometry/muzzles).
 * The cockpit is always the X-Wing for now; the pick changes flight
 * model everywhere and the exterior model for AI/peer ships. */
typedef enum DuelCraft { DUEL_CRAFT_XWING = 0, DUEL_CRAFT_TIE = 1,
                         DUEL_CRAFT_TIEIN = 2
} DuelCraft;
#define DUEL_CRAFT_COUNT 3

/* M5 skirmish: up to 5 TIEs vs player + up to 4 X-wing allies.
 * Bolts only ever hit the ENEMY team (rebel bolts -> ties; imperial
 * bolts -> rebels), so no friendly fire in either direction and no
 * shooter-id bookkeeping. M10: battle size 1v1..5v5 (menu); inactive
 * ships spawn parked and dead. */
#define DUEL_TIE_COUNT 5
#define DUEL_ALLY_COUNT 4
/* M31: asymmetric forces (1v3, 4v1, 1v0 vs the cruiser...). */
#define DUEL_FOES_MIN 0
#define DUEL_FOES_MAX 5
#define DUEL_FOES_DEFAULT 3     /* +2 allies = the frozen 3v3 battle */
#define DUEL_ALLIES_MIN 0
#define DUEL_ALLIES_MAX 4
#define DUEL_ALLIES_DEFAULT 2

typedef struct DuelShip {
    DuelVec3 pos;
    DuelQuat orient;
    float speed;      /* current scalar speed along forward */
    float throttle;   /* 0..1 lever (player) / auto (TIE) */
    float hp;
    float fire_cd;    /* seconds until can fire again */
    int alive;
    int muzzle;       /* alternating muzzle index */
    int craft;        /* M28: DuelCraft airframe (stats/geometry) */
    float shield_t;   /* M36: damage-free seconds left (respawn mercy) */
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

/* M30: Star Destroyer (stationary; hull = 6 world spheres, computed
 * once at reset from the ship-frame table). Turrets cycle volleys at
 * the nearest rebel with lead + deterministic wobble. */
typedef struct DuelCruiser {
    DuelVec3 pos;
    DuelQuat orient;
    float hp;
    int alive;
    float fire_cd;
    int turret_next;
    DuelVec3 sph[DUEL_CRUISER_SPHERES];
    float sub_hp[DUEL_CRUISER_SUBS]; /* M32: 0 = destroyed */
} DuelCruiser;

typedef struct DuelInput {
    float yaw;      /* -1..1, left stick X (M8 axes[0]) */
    float pitch;    /* -1..1, -left stick Y (M8 axes[1]); + = nose up */
    float roll;     /* -1..1, right stick X (M8 axes[3]) */
    float thr_axis; /* -1..1, right stick Y: integrates throttle lever */
    int fire;       /* right trigger past hysteresis */
    int menu;       /* M7: left menu button level (pause/menu select) */
    /* P0.4: raw sticks (telemetry; proves the live mapping on-device) */
    float raw_lx, raw_ly, raw_rx, raw_ry;
    /* M35 XWAQuestVR parity: right A = confirm, right B = back (levels;
     * main edge-detects). aim_* = controller aim poses (XR LOCAL space,
     * 0 left / 1 right; valid = tracked this poll). */
    int btn_a;
    int btn_b;
    int aim_valid[2];
    DuelVec3 aim_pos[2];
    DuelQuat aim_quat[2];
} DuelInput;

typedef struct DuelState {
    DuelPhase phase;
    float phase_t;      /* time in current phase */
    float time;         /* total sim time */
    DuelShip player;
    DuelShip ties[DUEL_TIE_COUNT];
    DuelShip allies[DUEL_ALLY_COUNT];
    /* MP-A PvP duel: the peer (second human). Parked dead in skirmish.
     * duel_mode 0 = vs AI, 1 = duel; duel_winner 0 none, 1 player,
     * 2 peer (latched until Duel_ResetDuel; 0 also on double KO). */
    DuelShip peer;
    int duel_mode;
    int duel_winner;
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
    /* M31: asymmetric forces (menu-owned, survive resets).
     * Wanted TIEs = foes_n (skirmish clamps to >= 1; cruiser
     * allows 0 = guns-only ISD), wanted allies = allies_n. */
    int allies_n;
    int foes_n;
    /* M28: XWA setup picks (DuelCraft, menu-owned, survive resets).
     * craft_player drives the local ship + allies; craft_enemy drives
     * the duel peer + skirmish ties (M29 mirror). */
    int craft_player;
    int craft_enemy;
    /* M30 cruiser battle: stationary ISD gun platform (mode 2). */
    int cruiser_mode;
    DuelCruiser cruiser;
    /* M34 CONTINUE?: while set, DESTROYED skips the auto-reset so the
     * lost battle survives behind the RESULT screen (CONTINUAR
     * respawns the player into it; cleared by resets + respawn). */
    int result_hold;
} DuelState;

void Duel_Init(DuelState *s);
void Duel_ResetCombat(DuelState *s);
void Duel_Update(DuelState *s, const DuelInput *in, float dt);
/* M34 CONTINUE?, M35 in place, M36 shielded: the player wakes where
 * they died with full HP + DUEL_RESPAWN_SHIELD_S damage-free seconds.
 * Everyone and everything else untouched, COMBAT again. */
void Duel_RespawnPlayer(DuelState *s);
/* MP-A PvP duel: face-to-face spawn (AI parked dead) + two-human step.
 * No auto-reset: the winner latches until Duel_ResetDuel. */
void Duel_ResetDuel(DuelState *s);
void Duel_UpdateDuel(DuelState *s, const DuelInput *in1,
                     const DuelInput *in2, float dt);
/* M30 cruiser battle: player + 2 allies vs the ISD (offline, no net).
 * Skirmish-style flow: DESTROYED + delay auto-resets, main records
 * the round on the reset edge (win = player alive). */
void Duel_ResetCruiser(DuelState *s);
void Duel_UpdateCruiser(DuelState *s, const DuelInput *in, float dt);
/* M30: hull sphere radii (ship-frame table lives in duel_sim.c). */
extern const float DUEL_CRUISER_SPH_R[DUEL_CRUISER_SPHERES];
/* M32: subsystem world position (diamonds) + max HP (bars). */
DuelVec3 Duel_CruiserSubPos(const DuelState *s, int i);
float Duel_CruiserSubMaxHP(int i);
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
/* M31 menu: forces (allies 0..4, foes 0..5, survive resets). */
void Duel_SetForces(DuelState *s, int allies, int foes);
/* M28 menu: craft picks (clamped 0..DUEL_CRAFT_COUNT-1, survive resets). */
void Duel_SetCrafts(DuelState *s, int player_craft, int enemy_craft);
/* M28: per-craft HP + bounding radius (single source of truth for
 * resets, collisions and the render-side hull bars). */
float Duel_CraftHP(int craft);
float Duel_CraftRadius(int craft);

/* Steering helper (also used by tests): turn 'orient' toward 'dir'. */
DuelQuat Duel_SteerToward(DuelQuat orient, DuelVec3 dir, float max_rad, float dt);
/* M33 stick response curve (also used by tests): deadzone rescale +
 * expo blend. 0 inside the deadzone, +-1 at the stops, attenuated
 * mid-travel for fine aim. */
float Duel_StickCurve(float x, float dead);

/* M5: nearest living tie to the player (-1 = wave cleared). Drives the
 * target diamond, lead pip, head arrow and HP bar. */
int Duel_AutoTarget(const DuelState *s);
/* M5: living ships per side (wave/pacing logic). */
int Duel_TiesAlive(const DuelState *s);
int Duel_AlliesAlive(const DuelState *s);
/* M10: wanted ships per side for the current battle size (fresh-wave
 * reference: a full wave has exactly these alive). */
int Duel_TiesWanted(const DuelState *s);
int Duel_AlliesWanted(const DuelState *s);

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
