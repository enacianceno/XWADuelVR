/* XWADuelVR M2.2 vehicle motion feel (see duel_motion.h). */
#include "duel_motion.h"

void duel_motion_reset(DuelMotion *m) {
    m->off_p = duel_v3(0, 0, 0); m->vel_p = duel_v3(0, 0, 0);
    m->off_r = duel_v3(0, 0, 0); m->vel_r = duel_v3(0, 0, 0);
    m->last_q = duel_q_identity();
    m->last_speed = 0.0f;
    m->have_last = 0;
    m->accel_fwd = 0.0f;
    m->rate = duel_v3(0, 0, 0);
}

static void spring_axis(float *x, float *v, float target, float dt) {
    /* Critically damped: a = w^2 (t - x) - 2 w v, semi-implicit Euler. */
    float w = DUEL_MO_OMEGA;
    float a = w * w * (target - *x) - 2.0f * w * (*v);
    *v += a * dt;
    *x += *v * dt;
}

static DuelQuat q_conj(DuelQuat q) {
    /* Unit-quaternion conjugate (= inverse). */
    return duel_q(-q.x, -q.y, -q.z, q.w);
}

void duel_motion_update(DuelMotion *m, DuelQuat ship_q, float speed, float dt) {
    DuelQuat dq, qc;
    DuelVec3 axis;
    float ang, rate;
    DuelVec3 rate_w, rate_l;
    float accel;
    DuelVec3 tp, tr;
    float pl;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 1.0f / 20.0f) dt = 1.0f / 20.0f;
    if (!m->have_last) {
        m->last_q = ship_q;
        m->last_speed = speed;
        m->have_last = 1;
        return;
    }
    /* Ship-local angular rate from orientation delta. */
    qc = q_conj(m->last_q);
    dq = duel_q_norm(duel_q_mul(ship_q, qc));
    if (dq.w < 0.0f) { dq.x = -dq.x; dq.y = -dq.y; dq.z = -dq.z; dq.w = -dq.w; }
    ang = 2.0f * acosf(dq.w > 1.0f ? 1.0f : dq.w);
    if (ang < 1e-6f) {
        axis = duel_v3(0, 1, 0);
    } else {
        float s = sqrtf(dq.x * dq.x + dq.y * dq.y + dq.z * dq.z);
        if (s < 1e-9f) axis = duel_v3(0, 1, 0);
        else axis = duel_v3(dq.x / s, dq.y / s, dq.z / s);
    }
    rate = dt > 1e-6f ? ang / dt : 0.0f;
    if (rate > DUEL_MO_MAX_RATE) rate = DUEL_MO_MAX_RATE;
    rate_w = duel_v3_scale(axis, rate);
    rate_l = duel_q_rot(q_conj(ship_q), rate_w);
    /* Forward accel from scalar speed. */
    accel = dt > 1e-6f ? (speed - m->last_speed) / dt : 0.0f;
    if (accel > DUEL_MO_MAX_ACCEL) accel = DUEL_MO_MAX_ACCEL;
    if (accel < -DUEL_MO_MAX_ACCEL) accel = -DUEL_MO_MAX_ACCEL;
    m->accel_fwd = accel;
    m->rate = rate_l;
    /* Targets (ship frame): accel presses back (+z); yaw leans out (+x
     * with +yaw rate); pitch presses down/up; rotation lags input. */
    tp = duel_v3(DUEL_MO_GAIN * DUEL_MO_K_LAT * rate_l.y,
                 DUEL_MO_GAIN * DUEL_MO_K_VERT * rate_l.x,
                 DUEL_MO_GAIN * DUEL_MO_K_ACCEL * accel);
    tr = duel_v3_scale(rate_l, -DUEL_MO_GAIN * DUEL_MO_K_LAG);
    spring_axis(&m->off_p.x, &m->vel_p.x, tp.x, dt);
    spring_axis(&m->off_p.y, &m->vel_p.y, tp.y, dt);
    spring_axis(&m->off_p.z, &m->vel_p.z, tp.z, dt);
    spring_axis(&m->off_r.x, &m->vel_r.x, tr.x, dt);
    spring_axis(&m->off_r.y, &m->vel_r.y, tr.y, dt);
    spring_axis(&m->off_r.z, &m->vel_r.z, tr.z, dt);
    /* Comfort clamps. */
    pl = duel_v3_len(m->off_p);
    if (pl > DUEL_MO_MAX_POS) m->off_p = duel_v3_scale(m->off_p, DUEL_MO_MAX_POS / pl);
    if (m->off_r.x > DUEL_MO_MAX_ANG) m->off_r.x = DUEL_MO_MAX_ANG;
    if (m->off_r.x < -DUEL_MO_MAX_ANG) m->off_r.x = -DUEL_MO_MAX_ANG;
    if (m->off_r.y > DUEL_MO_MAX_ANG) m->off_r.y = DUEL_MO_MAX_ANG;
    if (m->off_r.y < -DUEL_MO_MAX_ANG) m->off_r.y = -DUEL_MO_MAX_ANG;
    if (m->off_r.z > DUEL_MO_MAX_ANG) m->off_r.z = DUEL_MO_MAX_ANG;
    if (m->off_r.z < -DUEL_MO_MAX_ANG) m->off_r.z = -DUEL_MO_MAX_ANG;
    m->last_q = ship_q;
    m->last_speed = speed;
}

DuelQuat duel_motion_quat(const DuelMotion *m) {
    DuelQuat qy = duel_q_axis_angle(duel_v3(0, 1, 0), m->off_r.y);
    DuelQuat qx = duel_q_axis_angle(duel_v3(1, 0, 0), m->off_r.x);
    DuelQuat qz = duel_q_axis_angle(duel_v3(0, 0, 1), m->off_r.z);
    return duel_q_norm(duel_q_mul(qy, duel_q_mul(qx, qz)));
}
