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

static void duel_ship_reset(DuelShip *s, DuelVec3 pos, DuelQuat orient, float hp) {
    s->pos = pos;
    s->orient = duel_q_norm(orient);
    s->speed = 0.0f;
    s->throttle = 0.5f;
    s->hp = hp;
    s->fire_cd = 0.5f;
    s->alive = 1;
    s->muzzle = 0;
}

void Duel_Init(DuelState *s) {
    memset(s, 0, sizeof(*s));
    s->diff = 1; /* M7: pilot = frozen baseline (reset preserves it) */
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
    if (diff <= 0) { *hp = 0.7f; *spd = 0.85f; *turn = 0.8f; *cd = 1.3f; }
    else if (diff >= 2) { *hp = 1.3f; *spd = 1.1f; *turn = 1.15f; *cd = 0.8f; }
    else { *hp = 1.0f; *spd = 1.0f; *turn = 1.0f; *cd = 1.0f; }
}

void Duel_SetDifficulty(DuelState *s, int diff) {
    if (diff < 0) diff = 0;
    if (diff > 2) diff = 2;
    s->diff = diff;
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
    memset(s, 0, sizeof(*s));
    s->kills_player = kp;
    s->kills_tie = kt;
    s->time = t;
    s->hand_thr_active = hta;
    s->hand_thr_value = htv;
    /* M7: difficulty + hand deadzone survive resets (menu-owned). */
    s->diff = df;
    s->hand_dead = hd > 0.0f ? hd : DUEL_DEADZONE;
    s->phase = DUEL_PHASE_COMBAT;
    duel_ship_reset(&s->player, duel_v3(0, 0, 0), duel_q_identity(), DUEL_PLAYER_HP);
    if (hta) s->player.throttle = duel_clampf(htv, 0.0f, 1.0f);
    /* M5: TIE formation ahead (loose vee, staggered), each facing the
     * player; allies echeloned on the player's flanks, facing forward. */
    {
        static const float tox[DUEL_TIE_COUNT] = { 0.0f, -420.0f, 380.0f };
        static const float toy[DUEL_TIE_COUNT] = { 0.0f, -140.0f, 220.0f };
        static const float toz[DUEL_TIE_COUNT] = { 0.0f, -160.0f, -320.0f };
        static const float aox[DUEL_ALLY_COUNT] = { -170.0f, 170.0f };
        static const float aoy[DUEL_ALLY_COUNT] = { 30.0f, -30.0f };
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
                            DUEL_TIE_HP * dhp);
        }
        for (i = 0; i < DUEL_ALLY_COUNT; i++) {
            duel_ship_reset(&s->allies[i],
                            duel_v3(aox[i], aoy[i], 190.0f + 60.0f * i),
                            duel_q_identity(), DUEL_ALLY_HP);
        }
    }
    /* ai[]/ai_t[] zeroed by the memset above = ACQUIRE/0 for all. */
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
    if (owner != DUEL_SHIP_TIE) { /* M5: allies fly the X-wing set */
        static const float mx[4] = { 5.35f, -5.35f, 5.35f, -5.35f };
        static const float my[4] = { 1.30f, 1.30f, -1.30f, -1.30f };
        int m = muzzle_index % 4;
        off = duel_v3_add(duel_v3_scale(right, mx[m]), duel_v3_scale(up, my[m]));
    } else {
        float sx = (muzzle_index % 2 == 0) ? 0.35f : -0.35f;
        off = duel_v3_add(duel_v3_scale(right, sx), duel_v3_scale(up, -0.80f));
        fwd_dist = 1.5f;
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
        : owner == DUEL_SHIP_TIE ? &s->ties[idx] : &s->allies[idx];
    if (!sh->alive || sh->fire_cd > 0.0f) return 0;
    for (int i = 0; i < DUEL_LASER_POOL; i++) {
        if (s->lasers[i].active) continue;
        DuelLaser *l = &s->lasers[i];
        l->active = 1;
        l->owner = owner;
        float spd = owner == DUEL_SHIP_PLAYER ? DUEL_PLAYER_LASER_SPEED
            : owner == DUEL_SHIP_TIE ? DUEL_TIE_LASER_SPEED
                                     : DUEL_PLAYER_LASER_SPEED;
        DuelVec3 fwd = duel_q_forward(sh->orient);
        l->pos = Duel_MuzzleWorldAt(sh, owner, sh->muzzle);
        sh->muzzle++;
        l->vel = duel_v3_scale(fwd, spd + sh->speed);
        l->life = DUEL_LASER_LIFETIME;
        { /* M7: difficulty scales the TIE refire rate (player/allies fixed). */
            float dhp, dsp, dtn, dcd;
            duel_diff_mults(s->diff, &dhp, &dsp, &dtn, &dcd);
            sh->fire_cd = owner == DUEL_SHIP_PLAYER ? DUEL_PLAYER_LASER_COOLDOWN
                : owner == DUEL_SHIP_TIE ? DUEL_TIE_LASER_COOLDOWN * dcd
                                         : DUEL_ALLY_LASER_COOLDOWN;
        }
        return 1;
    }
    return 0;
}

static void duel_update_player(DuelState *s, const DuelInput *in, float dt) {
    DuelShip *p = &s->player;
    if (!p->alive) return;
    if (p->fire_cd > 0.0f) p->fire_cd -= dt;
    /* Throttle: M3.2 hand lever owns it absolutely while held; otherwise
     * the lever integrates (M8: right-stick-Y lever). The integrator
     * resumes from the hand-set value: release never jumps. */
    if (s->hand_thr_active) {
        p->throttle = duel_clampf(s->hand_thr_value, 0.0f, 1.0f);
    } else if (in->thr_axis > DUEL_DEADZONE || in->thr_axis < -DUEL_DEADZONE) {
        p->throttle = duel_clampf(p->throttle + in->thr_axis * DUEL_THROTTLE_RATE * dt, 0.0f, 1.0f);
    }
    float target = DUEL_PLAYER_MIN_SPEED +
        (DUEL_PLAYER_MAX_SPEED - DUEL_PLAYER_MIN_SPEED) * p->throttle;
    float ds = target - p->speed;
    float step = DUEL_PLAYER_ACCEL * dt;
    p->speed += ds > step ? step : (ds < -step ? -step : ds);
    /* Orientation: BODY-LOCAL (intrinsic). Convention is Hamilton with
     * v' = q v q*, so a delta must POST-multiply: (orient*dq) applies dq
     * about the CURRENT body axes, then carries it to world. P0.4
     * pre-multiplied (qyaw*(qpitch*(qroll*orient))) = WORLD axes: correct
     * at identity, wrong after any maneuver (the wandering roll axis).
     * M8 mapping/signs unchanged: yaw=lx pitch=-ly roll=rx thr=ry. */
    float pitch = fabsf(in->pitch) > DUEL_DEADZONE ? in->pitch : 0.0f;
    float yaw = fabsf(in->yaw) > DUEL_DEADZONE ? in->yaw : 0.0f;
    float roll = fabsf(in->roll) > DUEL_DEADZONE ? in->roll : 0.0f;
    /* M6 axis split (user order): the right lever owns pitch + PURE
     * yaw (stick axes ignored while held); the left lever owns roll.
     * No bank coordination: yaw and roll are independent. Each hand
     * overrides only its own axes, so both levers work together. */
    if (s->hand_stk_active) {
        pitch = fabsf(s->hand_pitch) > s->hand_dead ? s->hand_pitch : 0.0f;
        yaw = fabsf(s->hand_yaw) > s->hand_dead ? s->hand_yaw : 0.0f;
    }
    if (s->hand_lev_active) {
        roll = fabsf(s->hand_lev_roll) > s->hand_dead ? s->hand_lev_roll : 0.0f;
    }
    DuelQuat qyaw = duel_q_axis_angle(duel_v3(0, 1, 0), -yaw * DUEL_PLAYER_YAW_RATE * dt);
    DuelQuat qpitch = duel_q_axis_angle(duel_v3(1, 0, 0), pitch * DUEL_PLAYER_PITCH_RATE * dt);
    DuelQuat qroll = duel_q_axis_angle(duel_v3(0, 0, 1), -roll * DUEL_PLAYER_ROLL_RATE * dt);
    DuelQuat dq = duel_q_mul(qyaw, duel_q_mul(qpitch, qroll));
    p->orient = duel_q_norm(duel_q_mul(p->orient, dq));
    p->pos = duel_v3_add(p->pos, duel_v3_scale(duel_q_forward(p->orient), p->speed * dt));
    if (in->fire) duel_fire(s, DUEL_SHIP_PLAYER, 0);
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

    switch (s->ai[idx]) {
    case DUEL_TIE_ACQUIRE:
        t->throttle = 0.8f;
        t->orient = Duel_SteerToward(t->orient, dir_player, turn_rate, dt);
        if (dist < DUEL_TIE_CHASE_DIST) { s->ai[idx] = DUEL_TIE_CHASE; s->ai_t[idx] = 0; }
        break;
    case DUEL_TIE_CHASE: {
        t->throttle = dist > DUEL_TIE_CHASE_DIST ? 1.0f : 0.5f; /* M5: was
            0.6 (looser tail when close: the player pulls away, more
            passes instead of glue) */
        DuelVec3 lead = duel_v3_add(dir_player, duel_v3_scale(side, wob));
        t->orient = Duel_SteerToward(t->orient, lead, turn_rate, dt);
        float facing = duel_v3_dot(duel_v3_norm(fwd), dir_player);
        if (facing > DUEL_TIE_FIRE_CONE_DOT && dist < DUEL_TIE_FIRE_RANGE) {
            s->ai[idx] = DUEL_TIE_ATTACK; s->ai_t[idx] = 0;
        } else if (dist < DUEL_TIE_EVADE_DIST) {
            s->ai[idx] = DUEL_TIE_EVADE; s->ai_t[idx] = 0;
            s->evade_dir[idx] = duel_v3_norm(duel_v3_add(side, duel_v3(0, wob > 0 ? 0.5f : -0.5f, 0)));
        }
        break;
    }
    case DUEL_TIE_ATTACK: {
        t->throttle = 0.70f; /* M3.8b: was 0.55 (overshoots instead of
                                gluing to the tail) */
        t->orient = Duel_SteerToward(t->orient, dir_player, turn_rate * 0.8f, dt);
        float facing = duel_v3_dot(duel_v3_norm(duel_q_forward(t->orient)), dir_player);
        if (facing > DUEL_TIE_FIRE_CONE_DOT && dist < DUEL_TIE_FIRE_RANGE)
            duel_fire(s, DUEL_SHIP_TIE, idx);
        if (dist < DUEL_TIE_EVADE_DIST) {
            s->ai[idx] = DUEL_TIE_EVADE; s->ai_t[idx] = 0;
            s->evade_dir[idx] = duel_v3_norm(duel_v3_add(side, duel_v3(0, 0.6f, 0)));
        } else if (facing < DUEL_TIE_FIRE_CONE_DOT - 0.05f || dist > DUEL_TIE_FIRE_RANGE * 1.2f) {
            s->ai[idx] = DUEL_TIE_CHASE; s->ai_t[idx] = 0;
        }
        break;
    }
    case DUEL_TIE_EVADE:
        t->throttle = 1.0f;
        t->orient = Duel_SteerToward(t->orient, s->evade_dir[idx], turn_rate * 1.2f, dt);
        if (s->ai_t[idx] > DUEL_TIE_EVADE_TIME || dist > DUEL_TIE_CHASE_DIST * 1.4f) {
            s->ai[idx] = DUEL_TIE_REACQUIRE; s->ai_t[idx] = 0;
        }
        break;
    case DUEL_TIE_REACQUIRE:
        t->throttle = 0.7f;
        t->orient = Duel_SteerToward(t->orient, dir_player, turn_rate, dt);
        if (duel_v3_dot(duel_v3_norm(duel_q_forward(t->orient)), dir_player) > 0.9f) {
            s->ai[idx] = DUEL_TIE_CHASE; s->ai_t[idx] = 0;
        }
        break;
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
    ti = duel_nearest_tie_at(s, a->pos);
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
    } else {
        /* Wave cleared: tuck onto the player's wing. */
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
         * and imperial bolts test the player + allies. */
        if (l->owner == DUEL_SHIP_TIE) {
            float dmg = DUEL_TIE_LASER_DAMAGE;
            if (s->player.alive && duel_swept_hit(prev, l->pos, s->player.pos,
                    DUEL_PLAYER_RADIUS + DUEL_LASER_RADIUS)) {
                l->active = 0;
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
        if (s->phase_t >= DUEL_RESET_DELAY) Duel_ResetCombat(s);
    }
}
