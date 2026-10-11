/* XWADuelVR simulation implementation. */
#include "duel_sim.h"

#include <string.h>

DuelQuat Duel_SteerToward(DuelQuat orient, DuelVec3 dir, float max_rad, float dt) {
    DuelVec3 fwd = duel_q_forward(orient);
    DuelVec3 want = duel_v3_norm(dir);
    if (duel_v3_len(want) < 1e-6f) return orient;
    float d = duel_clampf(duel_v3_dot(fwd, want), -1.0f, 1.0f);
    float angle = acosf(d);
    if (angle < 1e-4f) return orient;
    float step = max_rad * dt;
    float t = step >= angle ? 1.0f : step / angle;
    DuelQuat full = duel_q_from_to(fwd, want);
    DuelQuat part = duel_q_slerp(duel_q_identity(), full, t);
    return duel_q_norm(duel_q_mul(part, orient));
}

DuelVec3 duel_lead_point(const DuelShip *shooter, const DuelShip *target, float bolt_base_speed) {
    DuelVec3 to_t = duel_v3_sub(target->pos, shooter->pos);
    float dist = duel_v3_len(to_t);
    float bolt_spd = bolt_base_speed + shooter->speed;
    float t = (bolt_spd > 1.0f && dist > 1e-3f) ? dist / bolt_spd : 0.0f;
    DuelVec3 tgt_vel = duel_v3_scale(duel_q_forward(target->orient), target->speed);
    return duel_v3_add(target->pos, duel_v3_scale(tgt_vel, t));
}

static void duel_ship_reset(DuelShip *s, DuelVec3 pos, DuelQuat orient,
                            float hp, int craft) {
    s->pos = pos;
    s->orient = duel_q_norm(orient);
    s->speed = 0.0f;
    s->throttle = 0.5f;
    s->hp = hp;
    s->fire_cd = 0.5f;
    s->alive = 1;
    s->muzzle = 0;
    s->craft = craft;
    s->shield_t = 0.0f;
}

void Duel_Init(DuelState *s) {
    memset(s, 0, sizeof(*s));
    s->diff = 1; /* M7: pilot = frozen baseline (reset preserves it) */
    /* M31: 3v3 by default (the frozen M5/M6/M9 battle). */
    s->allies_n = DUEL_ALLIES_DEFAULT;
    s->foes_n = DUEL_FOES_DEFAULT;
    /* M28: the requested duel (X-Wing vs TIE Interceptor). */
    s->craft_player = DUEL_CRAFT_XWING;
    s->craft_enemy = DUEL_CRAFT_TIEIN;
    Duel_ResetCombat(s);
    s->time = 0.0f;
}

void Duel_HandThrottle(DuelState *s, int active, float value) {
    s->hand_thr_active = active ? 1 : 0;
    s->hand_thr_value = duel_clampf(value, 0.0f, 1.0f);
}

void Duel_HandStick(DuelState *s, int active, float pitch, float yaw) {
    s->hand_stk_active = active ? 1 : 0;
    s->hand_pitch = duel_clampf(pitch, -1.0f, 1.0f);
    s->hand_yaw = duel_clampf(yaw, -1.0f, 1.0f);
}

void Duel_HandLever(DuelState *s, int active, float roll) {
    s->hand_lev_active = active ? 1 : 0;
    s->hand_lev_roll = duel_clampf(roll, -1.0f, 1.0f);
}

/* M7 difficulty multipliers: hp, speed, turn, fire-cooldown.
 * Pilot (1) is the identity = frozen M5/M6 baseline. */
static void duel_diff_mults(int diff, float *hp, float *spd,
                            float *turn, float *cd) {
    /* M9 EASY: wider turns (overshoots cross the nose) + slower refire.
     * HP/speed unchanged from M3.8c/M5 (fair fight, not target practice). */
    if (diff <= 0) { *hp = 0.7f; *spd = 0.85f; *turn = 0.65f; *cd = 1.6f; }
    else if (diff >= 2) { *hp = 1.3f; *spd = 1.1f; *turn = 1.15f; *cd = 0.8f; }
    else { *hp = 1.0f; *spd = 1.0f; *turn = 1.0f; *cd = 1.0f; }
}

void Duel_SetDifficulty(DuelState *s, int diff) {
    if (diff < 0) diff = 0;
    if (diff > 2) diff = 2;
    s->diff = diff;
}

void Duel_SetForces(DuelState *s, int allies, int foes) {
    if (allies < DUEL_ALLIES_MIN) allies = DUEL_ALLIES_MIN;
    if (allies > DUEL_ALLIES_MAX) allies = DUEL_ALLIES_MAX;
    if (foes < DUEL_FOES_MIN) foes = DUEL_FOES_MIN;
    if (foes > DUEL_FOES_MAX) foes = DUEL_FOES_MAX;
    s->allies_n = allies;
    s->foes_n = foes;
}

void Duel_SetCrafts(DuelState *s, int player_craft, int enemy_craft) {
    if (player_craft < 0) player_craft = 0;
    if (player_craft >= DUEL_CRAFT_COUNT) player_craft = DUEL_CRAFT_COUNT - 1;
    if (enemy_craft < 0) enemy_craft = 0;
    if (enemy_craft >= DUEL_CRAFT_COUNT) enemy_craft = DUEL_CRAFT_COUNT - 1;
    s->craft_player = player_craft;
    s->craft_enemy = enemy_craft;
}

float Duel_CraftHP(int craft) {
    if (craft == DUEL_CRAFT_TIE) return DUEL_TIE_HP;
    if (craft == DUEL_CRAFT_TIEIN) return DUEL_IN_HP;
    return DUEL_PLAYER_HP;
}

float Duel_CraftRadius(int craft) {
    if (craft == DUEL_CRAFT_TIE) return DUEL_TIE_RADIUS;
    if (craft == DUEL_CRAFT_TIEIN) return DUEL_IN_RADIUS;
    return DUEL_PLAYER_RADIUS;
}

int Duel_TiesWanted(const DuelState *s) {
    int lo = (s && s->cruiser_mode) ? DUEL_FOES_MIN : 1;
    int n = s ? s->foes_n : DUEL_FOES_DEFAULT;
    if (n < lo) n = lo;
    if (n > DUEL_FOES_MAX) n = DUEL_FOES_MAX;
    return n;
}

int Duel_AlliesWanted(const DuelState *s) {
    int n = s ? s->allies_n : DUEL_ALLIES_DEFAULT;
    if (n < DUEL_ALLIES_MIN) n = DUEL_ALLIES_MIN;
    if (n > DUEL_ALLIES_MAX) n = DUEL_ALLIES_MAX;
    return n;
}

void Duel_SetHandDead(DuelState *s, float dead) {
    s->hand_dead = duel_clampf(dead, 0.05f, 0.30f);
}

static DuelQuat duel_face_quat(DuelVec3 from, DuelVec3 to);

void Duel_ResetCombat(DuelState *s) {
    int kp = s->kills_player, kt = s->kills_tie;
    float t = s->time;
    /* M3.2a: a hand still holding the lever keeps its throttle across
     * death (the lever physically stays under the hand). */
    int hta = s->hand_thr_active;
    float htv = s->hand_thr_value;
    int df = s->diff;
    float hd = s->hand_dead;
    int al = s->allies_n, fo = s->foes_n;
    int cpl = s->craft_player, cen = s->craft_enemy;
    memset(s, 0, sizeof(*s));
    s->kills_player = kp;
    s->kills_tie = kt;
    s->time = t;
    s->hand_thr_active = hta;
    s->hand_thr_value = htv;
    /* M7: difficulty + hand deadzone survive resets (menu-owned). */
    s->diff = df;
    s->hand_dead = hd > 0.0f ? hd : DUEL_DEADZONE;
    /* M31: forces survive resets (menu-owned). */
    s->allies_n = (al >= DUEL_ALLIES_MIN && al <= DUEL_ALLIES_MAX) ? al
        : DUEL_ALLIES_DEFAULT;
    s->foes_n = (fo >= DUEL_FOES_MIN && fo <= DUEL_FOES_MAX) ? fo
        : DUEL_FOES_DEFAULT;
    /* M28: craft picks survive resets (menu-owned). */
    s->craft_player = (cpl >= 0 && cpl < DUEL_CRAFT_COUNT) ? cpl
        : DUEL_CRAFT_XWING;
    s->craft_enemy = (cen >= 0 && cen < DUEL_CRAFT_COUNT) ? cen
        : DUEL_CRAFT_TIEIN;
    s->phase = DUEL_PHASE_COMBAT;
    duel_ship_reset(&s->player, duel_v3(0, 0, 0), duel_q_identity(),
                    Duel_CraftHP(s->craft_player), s->craft_player);
    if (hta) s->player.throttle = duel_clampf(htv, 0.0f, 1.0f);
    /* M5: TIE formation ahead (loose vee, staggered), each facing the
     * player; allies echeloned on the player's flanks, facing forward.
     * M10: all slots spawned (positions deterministic), only the wanted
     * ships alive; extras parked dead. First 3 + 2 slots = the frozen
     * M5 formation, verbatim. */
    {
        static const float tox[DUEL_TIE_COUNT] =
            { 0.0f, -420.0f, 380.0f, -700.0f, 700.0f };
        static const float toy[DUEL_TIE_COUNT] =
            { 0.0f, -140.0f, 220.0f, 300.0f, -260.0f };
        static const float toz[DUEL_TIE_COUNT] =
            { 0.0f, -160.0f, -320.0f, -500.0f, -660.0f };
        static const float aox[DUEL_ALLY_COUNT] =
            { -170.0f, 170.0f, -340.0f, 340.0f };
        static const float aoy[DUEL_ALLY_COUNT] =
            { 30.0f, -30.0f, 60.0f, -60.0f };
        int i;
        float dhp, dsp, dtn, dcd;
        duel_diff_mults(s->diff, &dhp, &dsp, &dtn, &dcd);
        (void)dsp; (void)dtn; (void)dcd; /* used by AI/fire, not spawn */
        for (i = 0; i < DUEL_TIE_COUNT; i++) {
            DuelVec3 tp = duel_v3(DUEL_SPAWN_TIE_RIGHT + tox[i],
                                  DUEL_SPAWN_TIE_UP + toy[i],
                                  -DUEL_SPAWN_TIE_FWD + toz[i]);
            duel_ship_reset(&s->ties[i], tp,
                            duel_face_quat(tp, duel_v3(0, 0, 0)),
                            DUEL_TIE_HP * dhp, s->craft_enemy);
            if (i >= Duel_TiesWanted(s)) {
                s->ties[i].alive = 0;
                s->ties[i].hp = 0.0f;
            }
        }
        for (i = 0; i < DUEL_ALLY_COUNT; i++) {
            duel_ship_reset(&s->allies[i],
                            duel_v3(aox[i], aoy[i], 190.0f + 60.0f * i),
                            duel_q_identity(), DUEL_ALLY_HP, s->craft_player);
            if (i >= Duel_AlliesWanted(s)) {
                s->allies[i].alive = 0;
                s->allies[i].hp = 0.0f;
            }
        }
    }
    /* ai[]/ai_t[] zeroed by the memset above = ACQUIRE/0 for all. */
}

/* M34 CONTINUE?, M35 in place: the player wakes where they died
 * (pose/speed/throttle kept) with full HP and 0.5 s gun grace:
 * continue, not restart. Everyone and everything else untouched,
 * back to COMBAT, hold released. */
void Duel_RespawnPlayer(DuelState *s) {
    s->player.hp = Duel_CraftHP(s->craft_player);
    s->player.alive = 1;
    s->player.fire_cd = 0.5f;
    s->player.muzzle = 0;
    s->player.shield_t = DUEL_RESPAWN_SHIELD_S;
    s->phase = DUEL_PHASE_COMBAT;
    s->phase_t = 0.0f;
    s->result_hold = 0;
}

/* M5: orientation with -Z along (to - from), +Y up (basis-to-quat,
 * factored out of the old single-TIE spawn). */
static DuelQuat duel_face_quat(DuelVec3 from, DuelVec3 to) {
    DuelVec3 dir = duel_v3_norm(duel_v3_sub(to, from));
    DuelVec3 up = duel_v3(0, 1, 0);
    DuelVec3 right = duel_v3_norm(duel_v3_cross(dir, up));
    DuelVec3 real_up = duel_v3_cross(right, dir);
    DuelVec3 zax = duel_v3_scale(dir, -1.0f);
    float m00 = right.x, m01 = right.y, m02 = right.z;
    float m10 = real_up.x, m11 = real_up.y, m12 = real_up.z;
    float m20 = zax.x, m21 = zax.y, m22 = zax.z;
    float tr = m00 + m11 + m22;
    DuelQuat q;
    if (tr > 0.0f) {
        float s4 = sqrtf(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s4;
        q.x = (m12 - m21) / s4; q.y = (m20 - m02) / s4; q.z = (m01 - m10) / s4;
    } else if (m00 > m11 && m00 > m22) {
        float s4 = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m12 - m21) / s4;
        q.x = 0.25f * s4;
        q.y = (m01 + m10) / s4; q.z = (m02 + m20) / s4;
    } else if (m11 > m22) {
        float s4 = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m20 - m02) / s4;
        q.x = (m01 + m10) / s4;
        q.y = 0.25f * s4;
        q.z = (m12 + m21) / s4;
    } else {
        float s4 = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m01 - m10) / s4;
        q.x = (m02 + m20) / s4;
        q.y = (m12 + m21) / s4;
        q.z = 0.25f * s4;
    }
    return q;
}

/* Muzzle offsets (ship frame). Player: 4 wingtip cannons alternate.
 * Measured from the XwingExterior glTF wingtip clusters (all 4 quads
 * agree): OPT/model frame (+-5.35, +-1.30, +1.82 muzzle face);
 * R_y(PI) maps the symmetric set to ship (+/-5.35, +-1.30), 1.82 fwd.
 * (M2.2c: old guess (+-5.0, +-0.8, 4.0 fwd) spawned bolts 2.2 m ahead
 * of the real muzzles -- user-visible.) The muzzle face is the
 * foremost point, so no extra push is needed.
 * TIE: 2 chin cannons from measured hardpoints, model frame
 * (x=+-0.33, y=-0.80, z=+1.06); R_y(PI) keeps the symmetric pair
 * at ship (+/-0.33, -0.80), 1.06 fwd. Spawn a bit further ahead so
 * bolts start clear of the ball (r=1.31). */
DuelVec3 Duel_MuzzleWorldAt(const DuelShip *sh, DuelShipKind owner,
                            int muzzle_index) {
    DuelVec3 fwd = duel_q_forward(sh->orient);
    DuelVec3 right = duel_q_right(sh->orient);
    DuelVec3 up = duel_q_up(sh->orient);
    DuelVec3 off = duel_v3(0, 0, 0);
    float fwd_dist = 1.82f;
    /* M29: geometry strictly follows the airframe (the skirmish
     * mirror puts X-Wings/Interceptors on AI rails; chin guns would
     * float in the wrong place). */
    int craft = sh->craft;
    (void)owner;
    if (craft == DUEL_CRAFT_TIE) {
        float sx = (muzzle_index % 2 == 0) ? 0.35f : -0.35f;
        off = duel_v3_add(duel_v3_scale(right, sx), duel_v3_scale(up, -0.80f));
        fwd_dist = 1.5f;
    } else if (craft == DUEL_CRAFT_TIEIN) {
        int m = muzzle_index % 4;
        float sx = (m == 0 || m == 2) ? DUEL_IN_MUZZ_X : -DUEL_IN_MUZZ_X;
        float sy = (m < 2) ? DUEL_IN_MUZZ_Y : -DUEL_IN_MUZZ_Y;
        off = duel_v3_add(duel_v3_scale(right, sx), duel_v3_scale(up, sy));
        fwd_dist = DUEL_IN_MUZZ_FWD;
    } else { /* M5: allies fly the X-wing set */
        static const float mx[4] = { 5.35f, -5.35f, 5.35f, -5.35f };
        static const float my[4] = { 1.30f, 1.30f, -1.30f, -1.30f };
        int m = muzzle_index % 4;
        off = duel_v3_add(duel_v3_scale(right, mx[m]), duel_v3_scale(up, my[m]));
    }
    return duel_v3_add(duel_v3_add(sh->pos, off), duel_v3_scale(fwd, fwd_dist));
}

int Duel_GuideDir(const DuelShip *player, DuelVec3 target_pos,
                  float *dx, float *dy) {
    DuelVec3 rel = duel_v3_sub(target_pos, player->pos);
    float len = duel_v3_len(rel);
    DuelVec3 l;
    float r;
    if (len < 1e-3f) { *dx = 0.0f; *dy = -1.0f; return 1; }
    /* ship-local bearing (convention: -Z forward) */
    {
        DuelQuat q = player->orient;
        DuelQuat qc = duel_q(-q.x, -q.y, -q.z, q.w);
        l = duel_q_rot(qc, rel);
    }
    if (-l.z / len > 0.9781f) return 0; /* inside 12-deg nose cone */
    r = sqrtf(l.x * l.x + l.y * l.y);
    if (r < 1e-3f * len) { *dx = 0.0f; *dy = -1.0f; return 1; } /* astern */
    *dx = l.x / r;
    *dy = l.y / r;
    return 1;
}

int Duel_HeadGuide(DuelQuat head_q, DuelVec3 eye_pos, DuelVec3 target_pos,
                   float *dx, float *dy) {
    DuelVec3 rel = duel_v3_sub(target_pos, eye_pos);
    float len = duel_v3_len(rel);
    DuelVec3 l;
    float r;
    DuelQuat qc;
    if (len < 1e-3f) { *dx = 0.0f; *dy = -1.0f; return 1; }
    /* head-local bearing (convention: -Z forward) */
    qc = duel_q(-head_q.x, -head_q.y, -head_q.z, head_q.w);
    l = duel_q_rot(qc, rel);
    if (-l.z / len > 0.9781f) return 0; /* inside 12-deg view cone */
    r = sqrtf(l.x * l.x + l.y * l.y);
    if (r < 1e-3f * len) { *dx = 0.0f; *dy = -1.0f; return 1; } /* astern */
    *dx = l.x / r;
    *dy = l.y / r;
    return 1;
}

static int duel_fire(DuelState *s, DuelShipKind owner, int idx) {
    DuelShip *sh = owner == DUEL_SHIP_PLAYER ? &s->player
        : owner == DUEL_SHIP_PEER ? &s->peer
        : owner == DUEL_SHIP_TIE ? &s->ties[idx] : &s->allies[idx];
    if (!sh->alive || sh->fire_cd > 0.0f) return 0;
    for (int i = 0; i < DUEL_LASER_POOL; i++) {
        if (s->lasers[i].active) continue;
        DuelLaser *l = &s->lasers[i];
        l->active = 1;
        l->owner = owner;
        float spd = owner == DUEL_SHIP_TIE ? DUEL_TIE_LASER_SPEED
                                           : DUEL_PLAYER_LASER_SPEED;
        DuelVec3 fwd = duel_q_forward(sh->orient);
        l->pos = Duel_MuzzleWorldAt(sh, owner, sh->muzzle);
        sh->muzzle++;
        l->vel = duel_v3_scale(fwd, spd + sh->speed);
        l->life = DUEL_LASER_LIFETIME;
        { /* M7: difficulty scales the TIE refire rate (humans/allies fixed).
           * MP-A: the peer fires the player gun (X-Wing set). */
            float dhp, dsp, dtn, dcd;
            duel_diff_mults(s->diff, &dhp, &dsp, &dtn, &dcd);
            sh->fire_cd = owner == DUEL_SHIP_TIE ? DUEL_TIE_LASER_COOLDOWN * dcd
                : owner == DUEL_SHIP_ALLY ? DUEL_ALLY_LASER_COOLDOWN
                                          : DUEL_PLAYER_LASER_COOLDOWN;
        }
        return 1;
    }
    return 0;
}

/* MP-A: shared human flight integrator. The local player (use_hands)
 * keeps the hand-lever overrides; the duel peer flies the same ship
 * on raw input only (its hands live on the other visor). */
/* M33 stick response: rescale past the deadzone (no edge jump:
 * the old hard cut leapt 0 -> dead*rate) then blend linear with
 * cubic (expo). Odd-symmetric, 0 at center, +-1 at the stops. */
float Duel_StickCurve(float x, float dead) {
    float ax, t, s;
    if (dead < 0.0f) dead = 0.0f;
    if (dead > 0.9f) dead = 0.9f;
    ax = fabsf(x);
    if (ax <= dead) return 0.0f;
    if (ax > 1.0f) ax = 1.0f;
    s = x > 0.0f ? 1.0f : -1.0f;
    t = (ax - dead) / (1.0f - dead);
    return s * ((1.0f - DUEL_STICK_EXPO) * t + DUEL_STICK_EXPO * t * t * t);
}

static void duel_update_human(DuelState *s, DuelShip *p, const DuelInput *in,
                              DuelShipKind kind, int use_hands, float dt) {
    if (!p->alive) return;
    if (p->fire_cd > 0.0f) p->fire_cd -= dt;
    if (p->shield_t > 0.0f) { p->shield_t -= dt; if (p->shield_t < 0.0f) p->shield_t = 0.0f; }
    /* Throttle: M3.2 hand lever owns it absolutely while held; otherwise
     * the lever integrates (M8: right-stick-Y lever). The integrator
     * resumes from the hand-set value: release never jumps. */
    if (use_hands && s->hand_thr_active) {
        p->throttle = duel_clampf(s->hand_thr_value, 0.0f, 1.0f);
    } else if (in->thr_axis > DUEL_DEADZONE || in->thr_axis < -DUEL_DEADZONE) {
        p->throttle = duel_clampf(p->throttle + in->thr_axis * DUEL_THROTTLE_RATE * dt, 0.0f, 1.0f);
    }
    /* M28: the airframe sets the envelope (one shared human
     * integrator: player and duel peer). X-Wing = legacy numbers. */
    float min_spd = DUEL_PLAYER_MIN_SPEED, max_spd = DUEL_PLAYER_MAX_SPEED;
    float accel = DUEL_PLAYER_ACCEL;
    float pitch_rate = DUEL_PLAYER_PITCH_RATE, yaw_rate = DUEL_PLAYER_YAW_RATE;
    float roll_rate = DUEL_PLAYER_ROLL_RATE;
    if (p->craft == DUEL_CRAFT_TIEIN) {
        min_spd = DUEL_IN_MIN_SPEED; max_spd = DUEL_IN_MAX_SPEED;
        accel = DUEL_IN_ACCEL; pitch_rate = DUEL_IN_PITCH_RATE;
        yaw_rate = DUEL_IN_YAW_RATE; roll_rate = DUEL_IN_ROLL_RATE;
    } else if (p->craft == DUEL_CRAFT_TIE) {
        min_spd = DUEL_TIE_MIN_SPEED; max_spd = DUEL_TIE_MAX_SPEED;
        accel = DUEL_TIE_ACCEL; pitch_rate = DUEL_TIE_PITCH_RATE;
        yaw_rate = DUEL_TIE_TURN_RATE; roll_rate = DUEL_TIE_ROLL_RATE;
    }
    float target = min_spd + (max_spd - min_spd) * p->throttle;
    float ds = target - p->speed;
    float step = accel * dt;
    p->speed += ds > step ? step : (ds < -step ? -step : ds);
    /* Orientation: BODY-LOCAL (intrinsic). Convention is Hamilton with
     * v' = q v q*, so a delta must POST-multiply: (orient*dq) applies dq
     * about the CURRENT body axes, then carries it to world. P0.4
     * pre-multiplied (qyaw*(qpitch*(qroll*orient))) = WORLD axes: correct
     * at identity, wrong after any maneuver (the wandering roll axis).
     * M8 mapping/signs unchanged: yaw=lx pitch=-ly roll=rx thr=ry. */
    /* M33: every turn axis goes through the response curve (stick
     * and hand levers alike): deadzone rescale + expo for fine aim. */
    float pitch = Duel_StickCurve(in->pitch, DUEL_DEADZONE);
    float yaw = Duel_StickCurve(in->yaw, DUEL_DEADZONE);
    float roll = Duel_StickCurve(in->roll, DUEL_DEADZONE);
    /* M6 axis split (user order): the right lever owns pitch + PURE
     * yaw (stick axes ignored while held); the left lever owns roll.
     * No bank coordination: yaw and roll are independent. Each hand
     * overrides only its own axes, so both levers work together. */
    if (use_hands && s->hand_stk_active) {
        pitch = Duel_StickCurve(s->hand_pitch, s->hand_dead);
        yaw = Duel_StickCurve(s->hand_yaw, s->hand_dead);
    }
    if (use_hands && s->hand_lev_active) {
        roll = Duel_StickCurve(s->hand_lev_roll, s->hand_dead);
    }
    DuelQuat qyaw = duel_q_axis_angle(duel_v3(0, 1, 0), -yaw * yaw_rate * dt);
    DuelQuat qpitch = duel_q_axis_angle(duel_v3(1, 0, 0), pitch * pitch_rate * dt);
    DuelQuat qroll = duel_q_axis_angle(duel_v3(0, 0, 1), -roll * roll_rate * dt);
    DuelQuat dq = duel_q_mul(qyaw, duel_q_mul(qpitch, qroll));
    p->orient = duel_q_norm(duel_q_mul(p->orient, dq));
    p->pos = duel_v3_add(p->pos, duel_v3_scale(duel_q_forward(p->orient), p->speed * dt));
    if (in->fire) duel_fire(s, kind, 0);
}

static void duel_update_player(DuelState *s, const DuelInput *in, float dt) {
    duel_update_human(s, &s->player, in, DUEL_SHIP_PLAYER, 1, dt);
}

/* M5: nearest living rebel (player + allies) to a position. Ties split
 * their attention across the furball instead of all gluing one tail. */
static const DuelShip *duel_nearest_rebel(const DuelState *s, DuelVec3 from) {
    const DuelShip *best = NULL;
    float bd = 1e30f;
    int i;
    if (s->player.alive) {
        best = &s->player;
        bd = duel_v3_len(duel_v3_sub(s->player.pos, from));
    }
    for (i = 0; i < DUEL_ALLY_COUNT; i++) {
        if (!s->allies[i].alive) continue;
        {
            float d = duel_v3_len(duel_v3_sub(s->allies[i].pos, from));
            if (d < bd) { bd = d; best = &s->allies[i]; }
        }
    }
    return best;
}

static int duel_nearest_tie_at(const DuelState *s, DuelVec3 from) {
    int best = -1;
    float bd = 1e30f;
    int i;
    for (i = 0; i < DUEL_TIE_COUNT; i++) {
        float d;
        if (!s->ties[i].alive) continue;
        d = duel_v3_len(duel_v3_sub(s->ties[i].pos, from));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

int Duel_AutoTarget(const DuelState *s) {
    if (!s->player.alive) return -1;
    return duel_nearest_tie_at(s, s->player.pos);
}

int Duel_TiesAlive(const DuelState *s) {
    int n = 0, i;
    for (i = 0; i < DUEL_TIE_COUNT; i++) n += s->ties[i].alive ? 1 : 0;
    return n;
}

int Duel_AlliesAlive(const DuelState *s) {
    int n = 0, i;
    for (i = 0; i < DUEL_ALLY_COUNT; i++) n += s->allies[i].alive ? 1 : 0;
    return n;
}

static void duel_update_tie(DuelState *s, int idx, float dt) {
    DuelShip *t = &s->ties[idx];
    const DuelShip *p;
    DuelVec3 to_player;
    float dist;
    DuelVec3 dir_player;
    DuelVec3 fwd;
    float wob;
    DuelVec3 side;
    float turn_rate, max_spd; /* M7: difficulty-scaled (pilot = consts) */
    float evade_dist, evade_time, fire_dot, fire_range; /* M9 easy envelope */
    if (!t->alive) return;
    if (t->fire_cd > 0.0f) t->fire_cd -= dt;
    s->ai_t[idx] += dt;
    p = duel_nearest_rebel(s, t->pos);
    if (!p) { t->throttle = 0.3f; return; } /* furball won: coast */
    to_player = duel_v3_sub(p->pos, t->pos);
    dist = duel_v3_len(to_player);
    dir_player = dist > 1e-3f ? duel_v3_scale(to_player, 1.0f / dist) : duel_q_forward(t->orient);
    fwd = duel_q_forward(t->orient);
    /* Deterministic wobble so it never flies a perfect orbit (per-ship
     * phase: the formation doesn't weave in lockstep). */
    wob = (sinf(s->time * 0.9f + (float)idx * 2.1f) * 0.12f
        + sinf(s->time * 2.3f + 1.7f + (float)idx * 1.3f) * 0.06f)
        * DUEL_TIE_WEAVE;
    side = duel_v3_norm(duel_v3_cross(dir_player, duel_v3(0, 1, 0)));
    /* M7: difficulty scales TIE turn rate (pilot = identity). */
    {
        float dhp, dsp, dtn, dcd;
        duel_diff_mults(s->diff, &dhp, &dsp, &dtn, &dcd);
        turn_rate = DUEL_TIE_TURN_RATE * dtn;
        max_spd = DUEL_TIE_MIN_SPEED + (DUEL_TIE_MAX_SPEED - DUEL_TIE_MIN_SPEED) * dsp;
    }
    /* M9 EASY envelope: earlier/longer breaks, tighter/shorter fire gate.
     * Pilot/ace use the consts verbatim (frozen M5/M6 behavior). */
    {
        int easy = (s->diff <= 0);
        evade_dist = easy ? 130.0f : DUEL_TIE_EVADE_DIST;
        evade_time = easy ? 3.0f : DUEL_TIE_EVADE_TIME;
        fire_dot = easy ? 0.980f : DUEL_TIE_FIRE_CONE_DOT;
        fire_range = easy ? 500.0f : DUEL_TIE_FIRE_RANGE;
    }

    switch (s->ai[idx]) {
    case DUEL_TIE_ACQUIRE:
        t->throttle = 0.8f;
        t->orient = Duel_SteerToward(t->orient, dir_player, turn_rate, dt);
        if (dist < DUEL_TIE_CHASE_DIST) { s->ai[idx] = DUEL_TIE_CHASE; s->ai_t[idx] = 0; }
        break;
    case DUEL_TIE_CHASE: {
        /* M9 easy: brisker close chase (overshoots, never sits behind). */
        float close_thr = (s->diff <= 0) ? 0.65f : 0.5f;
        t->throttle = dist > DUEL_TIE_CHASE_DIST ? 1.0f : close_thr; /* M5: was
            0.6 (looser tail when close: the player pulls away, more
            passes instead of glue) */
        DuelVec3 lead = duel_v3_add(dir_player, duel_v3_scale(side, wob));
        t->orient = Duel_SteerToward(t->orient, lead, turn_rate, dt);
        float facing = duel_v3_dot(duel_v3_norm(fwd), dir_player);
        if (facing > fire_dot && dist < fire_range) {
            s->ai[idx] = DUEL_TIE_ATTACK; s->ai_t[idx] = 0;
        } else if (dist < evade_dist) {
            s->ai[idx] = DUEL_TIE_EVADE; s->ai_t[idx] = 0;
            s->evade_dir[idx] = duel_v3_norm(duel_v3_add(side, duel_v3(0, wob > 0 ? 0.5f : -0.5f, 0)));
        }
        break;
    }
    case DUEL_TIE_ATTACK: {
        /* M9 easy: runs through the firing pass (crosses the nose). */
        t->throttle = (s->diff <= 0) ? 0.85f : 0.70f; /* M3.8b: was 0.55
            (overshoots instead of gluing to the tail) */
        t->orient = Duel_SteerToward(t->orient, dir_player, turn_rate * 0.8f, dt);
        float facing = duel_v3_dot(duel_v3_norm(duel_q_forward(t->orient)), dir_player);
        if (facing > fire_dot && dist < fire_range)
            duel_fire(s, DUEL_SHIP_TIE, idx);
        if (dist < evade_dist) {
            s->ai[idx] = DUEL_TIE_EVADE; s->ai_t[idx] = 0;
            s->evade_dir[idx] = duel_v3_norm(duel_v3_add(side, duel_v3(0, 0.6f, 0)));
        } else if (facing < fire_dot - 0.05f || dist > fire_range * 1.2f) {
            s->ai[idx] = DUEL_TIE_CHASE; s->ai_t[idx] = 0;
        }
        break;
    }
    case DUEL_TIE_EVADE:
        t->throttle = 1.0f;
        t->orient = Duel_SteerToward(t->orient, s->evade_dir[idx], turn_rate * 1.2f, dt);
        if (s->ai_t[idx] > evade_time || dist > DUEL_TIE_CHASE_DIST * 1.4f) {
            s->ai[idx] = DUEL_TIE_REACQUIRE; s->ai_t[idx] = 0;
        }
        break;
    case DUEL_TIE_REACQUIRE: {
        t->throttle = 0.7f;
        /* M9 easy: fly a crossing pass ahead of the target's nose (+/- a
         * per-ship lateral offset) instead of beelining at it: the TIE
         * sweeps across the player's view, giving a chase opening. */
        DuelVec3 goal = dir_player;
        if (s->diff <= 0) {
            DuelVec3 tfwd = duel_q_forward(p->orient);
            DuelVec3 cross = duel_v3_add(p->pos, duel_v3_scale(tfwd, 250.0f));
            float lat = (idx % 2 == 0) ? 150.0f : -150.0f;
            cross = duel_v3_add(cross, duel_v3_scale(side, lat));
            {
                DuelVec3 to_cross = duel_v3_sub(cross, t->pos);
                if (duel_v3_len(to_cross) > 1e-3f)
                    goal = duel_v3_norm(to_cross);
            }
        }
        t->orient = Duel_SteerToward(t->orient, goal, turn_rate, dt);
        if (duel_v3_dot(duel_v3_norm(duel_q_forward(t->orient)), dir_player) > 0.9f ||
                (s->diff <= 0 && (dist < 150.0f || s->ai_t[idx] > 6.0f))) {
            s->ai[idx] = DUEL_TIE_CHASE; s->ai_t[idx] = 0;
        }
        break;
    }
    }
    float target = DUEL_TIE_MIN_SPEED + (max_spd - DUEL_TIE_MIN_SPEED) * t->throttle;
    float ds = target - t->speed;
    float step = DUEL_TIE_ACCEL * dt;
    t->speed += ds > step ? step : (ds < -step ? -step : ds);
    t->pos = duel_v3_add(t->pos, duel_v3_scale(duel_q_forward(t->orient), t->speed * dt));
}

/* M5 ally: plain chase of the nearest tie (stateless, index-phase weave);
 * no ties left -> formate on the player. Gentler guns than the player. */
static void duel_update_ally(DuelState *s, int idx, float dt) {
    DuelShip *a = &s->allies[idx];
    int ti;
    DuelVec3 goal;
    DuelVec3 to;
    float dist;
    DuelVec3 dir;
    DuelVec3 side;
    float wob;
    float facing;
    if (!a->alive) return;
    if (a->fire_cd > 0.0f) a->fire_cd -= dt;
    /* M31: escorts first (dogfight), the ISD when the sky is clear.
     * Long gate: the hull fills the lane before the center is close. */
    ti = duel_nearest_tie_at(s, a->pos);
    if (s->cruiser_mode && s->cruiser.alive && ti < 0) {
        goal = s->cruiser.pos;
        to = duel_v3_sub(goal, a->pos);
        dist = duel_v3_len(to);
        dir = dist > 1e-3f ? duel_v3_scale(to, 1.0f / dist)
                           : duel_q_forward(a->orient);
        side = duel_v3_norm(duel_v3_cross(dir, duel_v3(0, 1, 0)));
        wob = sinf(s->time * 1.1f + (float)idx * 2.7f) * 0.10f;
        a->throttle = dist > 900.0f ? 1.0f : 0.6f;
        a->orient = Duel_SteerToward(a->orient,
            duel_v3_add(dir, duel_v3_scale(side, wob)),
            DUEL_ALLY_TURN_RATE, dt);
        facing = duel_v3_dot(duel_v3_norm(duel_q_forward(a->orient)), dir);
        if (facing > DUEL_TIE_FIRE_CONE_DOT && dist < DUEL_CRUISER_FIRE_RANGE)
            duel_fire(s, DUEL_SHIP_ALLY, idx);
        ti = -2; /* skip both legacy branches below */
    }
    if (ti >= 0) {
        goal = s->ties[ti].pos;
        to = duel_v3_sub(goal, a->pos);
        dist = duel_v3_len(to);
        dir = dist > 1e-3f ? duel_v3_scale(to, 1.0f / dist)
                           : duel_q_forward(a->orient);
        side = duel_v3_norm(duel_v3_cross(dir, duel_v3(0, 1, 0)));
        wob = sinf(s->time * 1.1f + (float)idx * 2.7f) * 0.10f;
        a->throttle = dist > DUEL_TIE_CHASE_DIST ? 1.0f : 0.6f;
        a->orient = Duel_SteerToward(a->orient,
            duel_v3_add(dir, duel_v3_scale(side, wob)),
            DUEL_ALLY_TURN_RATE, dt);
        facing = duel_v3_dot(duel_v3_norm(duel_q_forward(a->orient)), dir);
        if (facing > DUEL_TIE_FIRE_CONE_DOT && dist < DUEL_TIE_FIRE_RANGE)
            duel_fire(s, DUEL_SHIP_ALLY, idx);
    } else if (ti == -1) {
        /* Wave cleared: tuck onto the player's wing. (-2 = M30 cruiser
         * branch above already steered this frame.) */
        goal = duel_v3_add(s->player.pos, duel_v3(idx == 0 ? -60.0f : 60.0f,
                                                 15.0f, 80.0f));
        to = duel_v3_sub(goal, a->pos);
        dist = duel_v3_len(to);
        dir = dist > 1e-3f ? duel_v3_scale(to, 1.0f / dist)
                           : duel_q_forward(a->orient);
        a->throttle = 0.7f;
        a->orient = Duel_SteerToward(a->orient, dir,
                                    DUEL_ALLY_TURN_RATE * 0.7f, dt);
    }
    {
        float target = DUEL_ALLY_MIN_SPEED
            + (DUEL_ALLY_MAX_SPEED - DUEL_ALLY_MIN_SPEED) * a->throttle;
        float ds = target - a->speed;
        float step = DUEL_ALLY_ACCEL * dt;
        a->speed += ds > step ? step : (ds < -step ? -step : ds);
        a->pos = duel_v3_add(a->pos,
            duel_v3_scale(duel_q_forward(a->orient), a->speed * dt));
    }
}

/* Swept bounding-sphere collision: closest approach of the step
 * segment to the ship center (endpoint-only test tunnels at
 * 700 m/s vs a ~12 m target diameter). */
static int duel_swept_hit(DuelVec3 prev, DuelVec3 pos, DuelVec3 center,
                           float radius) {
    DuelVec3 seg = duel_v3_sub(pos, prev);
    DuelVec3 to_t = duel_v3_sub(center, prev);
    float seg_len2 = duel_v3_dot(seg, seg);
    float t = seg_len2 > 1e-9f ? duel_clampf(duel_v3_dot(to_t, seg) / seg_len2, 0.0f, 1.0f) : 0.0f;
    DuelVec3 closest = duel_v3_add(prev, duel_v3_scale(seg, t));
    DuelVec3 d = duel_v3_sub(closest, center);
    return duel_v3_dot(d, d) < radius * radius;
}

static void duel_damage(DuelShip *target, float dmg) {
    target->hp -= dmg;
    if (target->hp <= 0.0f) {
        target->hp = 0.0f;
        target->alive = 0;
    }
}

/* M30 cruiser tables (SHIP frame; measured from StarDestroyer.gltf
 * accessors, evidence/m30-cruiser; OPT x/z negated via R_y(PI)).
 * Spheres over-cover ~15%: misses become hits, never through-hull. */
static const DuelVec3 kCruiserSph[DUEL_CRUISER_SPHERES] = {
    { 0.0f, 0.0f, -700.0f },   /* bow */
    { 0.0f, 0.0f, -350.0f },   /* fwd hull */
    { 0.0f, 20.0f, 50.0f },    /* mid hull */
    { -230.0f, 0.0f, 420.0f }, /* aft port */
    { 230.0f, 0.0f, 420.0f },  /* aft starboard */
    { 0.0f, 250.0f, 565.0f },  /* bridge tower */
};
/* M30: hull sphere radii (render needs them for hit-burst placement). */
const float DUEL_CRUISER_SPH_R[DUEL_CRUISER_SPHERES] =
    { 170.0f, 250.0f, 320.0f, 300.0f, 300.0f, 170.0f };
static const DuelVec3 kCruiserTur[DUEL_CRUISER_TURRETS] = {
    { 234.0f, 41.0f, 295.0f }, { -234.0f, 41.0f, 295.0f },
    { 234.0f, 45.0f, 348.0f }, { -234.0f, 45.0f, 348.0f },
    { 234.0f, 49.0f, 401.0f }, { -234.0f, 49.0f, 401.0f },
    { 234.0f, 53.0f, 454.0f }, { -234.0f, 53.0f, 454.0f },
    { 8.8f, 1.9f, -819.0f }, { -8.8f, 1.9f, -819.0f },
};
/* M32: subsystem offsets (turrets 0..9 = kCruiserTur, domes 10..11,
 * bridge 12) + hit radii. Domes (±106, 293, 517) and bridge neck
 * (0, 270, 540) measured from the glTF (Mesh34/35, Mesh24/67). */
static const DuelVec3 kCruiserSub[DUEL_CRUISER_SUBS] = {
    { 234.0f, 41.0f, 295.0f }, { -234.0f, 41.0f, 295.0f },
    { 234.0f, 45.0f, 348.0f }, { -234.0f, 45.0f, 348.0f },
    { 234.0f, 49.0f, 401.0f }, { -234.0f, 49.0f, 401.0f },
    { 234.0f, 53.0f, 454.0f }, { -234.0f, 53.0f, 454.0f },
    { 8.8f, 1.9f, -819.0f }, { -8.8f, 1.9f, -819.0f },
    { 106.0f, 293.0f, 517.0f }, { -106.0f, 293.0f, 517.0f },
    { 0.0f, 270.0f, 540.0f },
};
static const float kCruiserSubR[DUEL_CRUISER_SUBS] = {
    DUEL_CRUISER_TURRET_R, DUEL_CRUISER_TURRET_R, DUEL_CRUISER_TURRET_R,
    DUEL_CRUISER_TURRET_R, DUEL_CRUISER_TURRET_R, DUEL_CRUISER_TURRET_R,
    DUEL_CRUISER_TURRET_R, DUEL_CRUISER_TURRET_R, DUEL_CRUISER_TURRET_R,
    DUEL_CRUISER_TURRET_R, DUEL_CRUISER_DOME_R, DUEL_CRUISER_DOME_R,
    DUEL_CRUISER_BRIDGE_R,
};

DuelVec3 Duel_CruiserSubPos(const DuelState *s, int i) {
    if (i < 0) i = 0;
    if (i >= DUEL_CRUISER_SUBS) i = DUEL_CRUISER_SUBS - 1;
    return duel_v3_add(s->cruiser.pos,
                       duel_q_rot(s->cruiser.orient, kCruiserSub[i]));
}

float Duel_CruiserSubMaxHP(int i) {
    if (i == DUEL_CRUISER_SUB_BRIDGE) return DUEL_CRUISER_BRIDGE_HP;
    if (i >= DUEL_CRUISER_SUB_DOME0) return DUEL_CRUISER_DOME_HP;
    return DUEL_CRUISER_TURRET_HP;
}

/* M32: each dead dome drops the shields a notch (hull takes +50%). */
static float duel_shield_mult(const DuelState *s) {
    float m = 1.0f;
    if (s->cruiser.sub_hp[DUEL_CRUISER_SUB_DOME0] <= 0.0f) m += 0.5f;
    if (s->cruiser.sub_hp[DUEL_CRUISER_SUB_DOME1] <= 0.0f) m += 0.5f;
    return m;
}

static void duel_cruiser_damage(DuelState *s, float dmg) {
    s->cruiser.hp -= dmg;
    if (s->cruiser.hp <= 0.0f) {
        s->cruiser.hp = 0.0f;
        s->cruiser.alive = 0;
    }
}

/* M30: one turret volley per cooldown at the nearest rebel (lead +
 * deterministic wobble). Imperial team (TIE branch, TIE damage). */
static void duel_cruiser_volley(DuelState *s, float dt) {
    DuelCruiser *c = &s->cruiser;
    const DuelShip *tgt;
    DuelVec3 tp;
    DuelVec3 to;
    float dist, tm;
    DuelVec3 aim, dir, side, up;
    float wob;
    int i;
    if (!c->alive) return;
    if (s->cruiser.sub_hp[DUEL_CRUISER_SUB_BRIDGE] <= 0.0f) return;
    if (c->fire_cd > 0.0f) { c->fire_cd -= dt; return; }
    /* M37: the guns respect difficulty (rookie guns MISS: wide wobble +
     * slow volleys; ace guns tighten up). Pilot = legacy numbers. */
    {
        float dhp, dsp, dtn, dcd;
        duel_diff_mults(s->diff, &dhp, &dsp, &dtn, &dcd);
        (void)dhp; (void)dsp; (void)dtn; /* volley rate only */
        c->fire_cd = DUEL_CRUISER_VOLLEY_CD * dcd;
    }
    /* M32: dead turrets are skipped in the rotation. */
    i = -1;
    {
        int n;
        for (n = 0; n < DUEL_CRUISER_TURRETS; n++) {
            int t = c->turret_next++ % DUEL_CRUISER_TURRETS;
            if (c->sub_hp[t] > 0.0f) { i = t; break; }
        }
    }
    if (i < 0) return; /* all guns dead */
    tp = duel_v3_add(c->pos, duel_q_rot(c->orient, kCruiserTur[i]));
    tgt = duel_nearest_rebel(s, tp);
    if (!tgt) return;
    to = duel_v3_sub(tgt->pos, tp);
    dist = duel_v3_len(to);
    if (dist < 1.0f) return;
    tm = dist / DUEL_CRUISER_BOLT_SPEED;
    aim = duel_v3_add(tgt->pos,
        duel_v3_scale(duel_q_forward(tgt->orient), tgt->speed * tm));
    dir = duel_v3_norm(duel_v3_sub(aim, tp));
    side = duel_v3_norm(duel_v3_cross(dir, duel_v3(0, 1, 0)));
    up = duel_v3_cross(side, dir);
    /* M37 wobble obedience: rookie x2.0 (sprays), ace x0.6 (snipes). */
    {
        float wob_m = s->diff <= 0 ? 2.0f : s->diff >= 2 ? 0.6f : 1.0f;
        wob = (sinf(s->time * 1.7f + (float)i * 2.4f) * 0.02f
            + sinf(s->time * 3.1f + (float)i * 1.1f) * 0.01f) * wob_m;
    }
    dir = duel_v3_norm(duel_v3_add(dir,
        duel_v3_add(duel_v3_scale(side, wob), duel_v3_scale(up, wob * 0.6f))));
    for (i = 0; i < DUEL_LASER_POOL; i++) {
        if (s->lasers[i].active) continue;
        s->lasers[i].active = 1;
        s->lasers[i].owner = DUEL_SHIP_TIE;
        s->lasers[i].pos = tp;
        s->lasers[i].vel = duel_v3_scale(dir, DUEL_CRUISER_BOLT_SPEED);
        s->lasers[i].life = DUEL_CRUISER_BOLT_LIFE;
        break;
    }
}

/* M30: hull scrape (overlap a sphere: burn + slow). M31: escorts
 * scrape too (same hazard for everyone near the hull). */
static void duel_cruiser_ram_one(DuelShip *sh, DuelState *s, float dt) {
    int i;
    if (!sh->alive) return;
    for (i = 0; i < DUEL_CRUISER_SPHERES; i++) {
        float d = duel_v3_len(duel_v3_sub(sh->pos, s->cruiser.sph[i]));
        if (d < DUEL_CRUISER_SPH_R[i]) {
            /* M36: shield skips the burn (the slow always applies). */
            if (sh->shield_t <= 0.0f)
                duel_damage(sh, DUEL_CRUISER_RAM_DPS * dt);
            if (sh->speed > 40.0f) sh->speed = 40.0f;
            break;
        }
    }
}

static void duel_cruiser_ram(DuelState *s, float dt) {
    int k;
    if (!s->cruiser.alive) return;
    duel_cruiser_ram_one(&s->player, s, dt);
    for (k = 0; k < DUEL_ALLY_COUNT; k++)
        duel_cruiser_ram_one(&s->allies[k], s, dt);
    for (k = 0; k < DUEL_TIE_COUNT; k++)
        duel_cruiser_ram_one(&s->ties[k], s, dt);
}

/* M32: which live sub (if any) ate a bolt that just hit hull.
 * Rebuilds the full path from the spawn (rebel bolts share one
 * speed/lifetime) and takes the nearest live sub the flight LINE
 * threads within its radius (unclamped projection: the mount sits
 * past the impact), provided the impact is near the mount (embed
 * depth peaks ~168 m at the trench turrets; 200 keeps far-side
 * hull hits on the hull). -1 = clean hull hit. */
static int duel_cruiser_sub_redirect(const DuelState *s, const DuelLaser *l) {
    DuelVec3 spawn, seg, to_t, closest, d, sp;
    float seg_len2, flown, best = 1e30f;
    int bi, best_i = -1;
    flown = DUEL_LASER_LIFETIME - l->life;
    if (flown < 0.0f) flown = 0.0f;
    spawn = duel_v3_sub(l->pos, duel_v3_scale(l->vel, flown));
    seg = duel_v3_sub(l->pos, spawn);
    seg_len2 = duel_v3_dot(seg, seg);
    if (seg_len2 < 1e-6f) return -1;
    for (bi = 0; bi < DUEL_CRUISER_SUBS; bi++) {
        float t, dp2, rr;
        if (s->cruiser.sub_hp[bi] <= 0.0f) continue;
        sp = Duel_CruiserSubPos(s, bi);
        if (duel_v3_len(duel_v3_sub(sp, l->pos)) > 200.0f) continue;
        to_t = duel_v3_sub(sp, spawn);
        t = duel_v3_dot(to_t, seg) / seg_len2;
        if (t < 0.0f) continue; /* mount behind the muzzle */
        closest = duel_v3_add(spawn, duel_v3_scale(seg, t));
        d = duel_v3_sub(closest, sp);
        dp2 = duel_v3_dot(d, d);
        rr = kCruiserSubR[bi] + DUEL_LASER_RADIUS;
        if (dp2 < rr * rr && dp2 < best) { best = dp2; best_i = bi; }
    }
    return best_i;
}

static void duel_update_lasers(DuelState *s, float dt) {
    for (int i = 0; i < DUEL_LASER_POOL; i++) {
        DuelLaser *l = &s->lasers[i];
        DuelVec3 prev;
        int k;
        if (!l->active) continue;
        l->life -= dt;
        if (l->life <= 0.0f) { l->active = 0; continue; }
        prev = l->pos;
        l->pos = duel_v3_add(l->pos, duel_v3_scale(l->vel, dt));
        /* M5 teams: rebel bolts (player + allies) test the ties only --
         * the player's shots can never hurt friendlies (user order) --
         * and imperial bolts test the player + allies.
         * MP-A: the peer flies imperial, so peer bolts join the TIE
         * branch (player gun damage) and rebel bolts also test the
         * peer (X-Wing radius). Parked peer in skirmish: no-op. */
        if (l->owner == DUEL_SHIP_TIE || l->owner == DUEL_SHIP_PEER) {
            float dmg = l->owner == DUEL_SHIP_TIE ? DUEL_TIE_LASER_DAMAGE
                                                  : DUEL_PLAYER_LASER_DAMAGE;
            if (s->player.alive && duel_swept_hit(prev, l->pos, s->player.pos,
                    Duel_CraftRadius(s->player.craft) + DUEL_LASER_RADIUS)) {
                l->active = 0;
                /* M36: shielded respawn fizzles bolts (no damage). */
                if (s->player.shield_t <= 0.0f)
                    duel_damage(&s->player, dmg);
                continue;
            }
            for (k = 0; k < DUEL_ALLY_COUNT; k++) {
                if (!s->allies[k].alive) continue;
                if (duel_swept_hit(prev, l->pos, s->allies[k].pos,
                        DUEL_ALLY_RADIUS + DUEL_LASER_RADIUS)) {
                    l->active = 0;
                    duel_damage(&s->allies[k], dmg);
                    break;
                }
            }
        } else {
            float dmg = l->owner == DUEL_SHIP_PLAYER ? DUEL_PLAYER_LASER_DAMAGE
                                                     : DUEL_ALLY_LASER_DAMAGE;
            for (k = 0; k < DUEL_TIE_COUNT; k++) {
                if (!s->ties[k].alive) continue;
                if (duel_swept_hit(prev, l->pos, s->ties[k].pos,
                        DUEL_TIE_RADIUS + DUEL_LASER_RADIUS)) {
                    l->active = 0;
                    duel_damage(&s->ties[k], dmg);
                    break;
                }
            }
            if (l->active && s->peer.alive && duel_swept_hit(prev, l->pos,
                    s->peer.pos, Duel_CraftRadius(s->peer.craft) + DUEL_LASER_RADIUS)) {
                l->active = 0;
                duel_damage(&s->peer, dmg);
            }
            /* M32: rebel bolts vs subsystems first, hull after. */
            if (l->active && s->cruiser_mode && s->cruiser.alive) {
                int bi;
                for (bi = 0; bi < DUEL_CRUISER_SUBS; bi++) {
                    DuelVec3 sp;
                    if (s->cruiser.sub_hp[bi] <= 0.0f) continue;
                    sp = Duel_CruiserSubPos(s, bi);
                    if (duel_swept_hit(prev, l->pos, sp,
                            kCruiserSubR[bi] + DUEL_LASER_RADIUS)) {
                        l->active = 0;
                        s->cruiser.sub_hp[bi] -= dmg;
                        if (s->cruiser.sub_hp[bi] < 0.0f)
                            s->cruiser.sub_hp[bi] = 0.0f;
                        break;
                    }
                }
            }
            /* M30: rebel bolts vs the cruiser hull spheres. M32: the
             * spheres over-cover ~15%, so every mount sits inside one
             * (bow turret: 119 m deep in a r170 sphere) and a bolt
             * aimed at a live sub crosses hull first: redirect it to
             * the mount when the full path says the sub ate it. */
            if (l->active && s->cruiser_mode && s->cruiser.alive) {
                for (k = 0; k < DUEL_CRUISER_SPHERES; k++) {
                    if (duel_swept_hit(prev, l->pos, s->cruiser.sph[k],
                            DUEL_CRUISER_SPH_R[k] + DUEL_LASER_RADIUS)) {
                        int bi = duel_cruiser_sub_redirect(s, l);
                        l->active = 0;
                        if (bi >= 0) {
                            s->cruiser.sub_hp[bi] -= dmg;
                            if (s->cruiser.sub_hp[bi] < 0.0f)
                                s->cruiser.sub_hp[bi] = 0.0f;
                        } else {
                            duel_cruiser_damage(s,
                                dmg * duel_shield_mult(s));
                        }
                        break;
                    }
                }
            }
        }
    }
}

void Duel_Update(DuelState *s, const DuelInput *in, float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f; /* clamp tab-switch spikes */
    s->time += dt;
    s->phase_t += dt;
    if (s->phase == DUEL_PHASE_COMBAT) {
        int k;
        duel_update_player(s, in, dt);
        for (k = 0; k < DUEL_TIE_COUNT; k++) duel_update_tie(s, k, dt);
        for (k = 0; k < DUEL_ALLY_COUNT; k++) duel_update_ally(s, k, dt);
        duel_update_lasers(s, dt);
        /* M5 wave: reset when the player dies OR the last tie dies
         * (allies stay dead until the wave resets: stakes). */
        if (!s->player.alive || Duel_TiesAlive(s) == 0) {
            s->phase = DUEL_PHASE_DESTROYED;
            s->phase_t = 0.0f;
            if (Duel_TiesAlive(s) == 0 && s->player.alive) s->kills_player++;
            if (!s->player.alive && Duel_TiesAlive(s) > 0) s->kills_tie++;
        }
    } else {
        /* Destroyed: keep world drifting (lasers fly, survivors coast;
         * the furball behind the death cam keeps fighting). */
        DuelInput empty;
        int k;
        memset(&empty, 0, sizeof(empty));
        duel_update_player(s, &empty, dt);
        for (k = 0; k < DUEL_TIE_COUNT; k++) duel_update_tie(s, k, dt);
        for (k = 0; k < DUEL_ALLY_COUNT; k++) duel_update_ally(s, k, dt);
        duel_update_lasers(s, dt);
        /* M34: defeat holds the wreck (CONTINUE? behind RESULT). */
        if (s->phase_t >= DUEL_RESET_DELAY && !s->result_hold)
            Duel_ResetCombat(s);
    }
}

/* MP-A PvP duel: same combat reset, then the AI is parked dead and
 * the peer spawns 1000 m ahead facing the player. Loser/winner and
 * mode survive until the next Duel_ResetDuel. */
void Duel_ResetDuel(DuelState *s) {
    int i;
    DuelVec3 pp;
    Duel_ResetCombat(s);
    s->duel_mode = 1;
    s->duel_winner = 0;
    for (i = 0; i < DUEL_TIE_COUNT; i++) {
        s->ties[i].alive = 0;
        s->ties[i].hp = 0.0f;
    }
    for (i = 0; i < DUEL_ALLY_COUNT; i++) {
        s->allies[i].alive = 0;
        s->allies[i].hp = 0.0f;
    }
    pp = duel_v3(0.0f, 0.0f, -DUEL_SPAWN_TIE_FWD);
    duel_ship_reset(&s->peer, pp, duel_face_quat(pp, duel_v3(0, 0, 0)),
                    Duel_CraftHP(s->craft_enemy), s->craft_enemy);
}

/* M30 cruiser battle: same combat reset (rebel spawns + setup picks),
 * then the AI furball is parked, 2 wingmen stay, and the ISD spawns
 * ahead, bow toward the player. Hull spheres precomputed (static). */
void Duel_ResetCruiser(DuelState *s) {
    int i;
    Duel_ResetCombat(s);
    s->cruiser_mode = 1;
    s->duel_mode = 0;
    s->duel_winner = 0;
    /* M31: escorts + wingmen from the force picks (0 foes = guns-only). */
    for (i = Duel_TiesWanted(s); i < DUEL_TIE_COUNT; i++) {
        s->ties[i].alive = 0;
        s->ties[i].hp = 0.0f;
    }
    for (i = Duel_AlliesWanted(s); i < DUEL_ALLY_COUNT; i++) {
        s->allies[i].alive = 0;
        s->allies[i].hp = 0.0f;
    }
    s->cruiser.pos = duel_v3(0.0f, 0.0f, -DUEL_CRUISER_SPAWN_FWD);
    s->cruiser.orient = duel_q(0.0f, 1.0f, 0.0f, 0.0f); /* R_y(PI) */
    s->cruiser.hp = DUEL_CRUISER_HP;
    s->cruiser.alive = 1;
    s->cruiser.fire_cd = 1.0f; /* first-volley grace */
    s->cruiser.turret_next = 0;
    for (i = 0; i < DUEL_CRUISER_SUBS; i++)
        s->cruiser.sub_hp[i] = Duel_CruiserSubMaxHP(i);
    for (i = 0; i < DUEL_CRUISER_SPHERES; i++)
        s->cruiser.sph[i] = duel_v3_add(s->cruiser.pos,
            duel_q_rot(s->cruiser.orient, kCruiserSph[i]));
}

void Duel_UpdateCruiser(DuelState *s, const DuelInput *in, float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f; /* clamp tab-switch spikes */
    s->time += dt;
    s->phase_t += dt;
    if (s->phase == DUEL_PHASE_COMBAT) {
        int k;
        duel_update_player(s, in, dt);
        for (k = 0; k < DUEL_ALLY_COUNT; k++) duel_update_ally(s, k, dt);
        duel_cruiser_volley(s, dt);
        duel_update_lasers(s, dt);
        duel_cruiser_ram(s, dt);
        if (!s->player.alive || !s->cruiser.alive) {
            s->phase = DUEL_PHASE_DESTROYED;
            s->phase_t = 0.0f;
            if (!s->cruiser.alive && s->player.alive) s->kills_player++;
            if (!s->player.alive && s->cruiser.alive) s->kills_tie++;
        }
    } else {
        /* Destroyed: drift the survivors; the guns fall silent. */
        DuelInput empty;
        int k;
        memset(&empty, 0, sizeof(empty));
        duel_update_player(s, &empty, dt);
        for (k = 0; k < DUEL_ALLY_COUNT; k++) duel_update_ally(s, k, dt);
        duel_update_lasers(s, dt);
        /* M34: defeat holds the wreck (CONTINUE? behind RESULT). */
        if (s->phase_t >= DUEL_RESET_DELAY && !s->result_hold)
            Duel_ResetCruiser(s);
    }
}

/* MP-A: two-human step. No AI, no auto-reset: on death the phase
 * flips to DESTROYED and the winner latches (0 = double KO) until
 * the menu calls Duel_ResetDuel for the next round. */
void Duel_UpdateDuel(DuelState *s, const DuelInput *in1,
                     const DuelInput *in2, float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f; /* clamp tab-switch spikes */
    s->time += dt;
    s->phase_t += dt;
    if (s->phase == DUEL_PHASE_COMBAT) {
        duel_update_human(s, &s->player, in1, DUEL_SHIP_PLAYER, 1, dt);
        duel_update_human(s, &s->peer, in2, DUEL_SHIP_PEER, 0, dt);
        duel_update_lasers(s, dt);
        if (!s->player.alive || !s->peer.alive) {
            s->phase = DUEL_PHASE_DESTROYED;
            s->phase_t = 0.0f;
            if (!s->peer.alive && s->player.alive) s->duel_winner = 1;
            else if (!s->player.alive && s->peer.alive) s->duel_winner = 2;
            else s->duel_winner = 0;
        }
    } else {
        DuelInput empty;
        memset(&empty, 0, sizeof(empty));
        duel_update_human(s, &s->player, &empty, DUEL_SHIP_PLAYER, 1, dt);
        duel_update_human(s, &s->peer, &empty, DUEL_SHIP_PEER, 0, dt);
        duel_update_lasers(s, dt);
    }
}
