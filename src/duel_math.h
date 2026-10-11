/* XWADuelVR math: vec3/quat/mat4, self-contained, no dependencies. */
#ifndef XWADUELVR_MATH_H
#define XWADUELVR_MATH_H

#include <math.h>

typedef struct DuelVec3 { float x, y, z; } DuelVec3;
typedef struct DuelQuat { float x, y, z, w; } DuelQuat;
typedef struct DuelMat4 { float m[16]; } DuelMat4; /* column-major */

#define DUEL_PI 3.14159265358979323846f
#define DUEL_DEG2RAD (DUEL_PI / 180.0f)

static inline DuelVec3 duel_v3(float x, float y, float z) {
    DuelVec3 v = { x, y, z }; return v;
}
static inline DuelVec3 duel_v3_add(DuelVec3 a, DuelVec3 b) {
    return duel_v3(a.x + b.x, a.y + b.y, a.z + b.z);
}
static inline DuelVec3 duel_v3_sub(DuelVec3 a, DuelVec3 b) {
    return duel_v3(a.x - b.x, a.y - b.y, a.z - b.z);
}
static inline DuelVec3 duel_v3_scale(DuelVec3 a, float s) {
    return duel_v3(a.x * s, a.y * s, a.z * s);
}
static inline float duel_v3_dot(DuelVec3 a, DuelVec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static inline DuelVec3 duel_v3_cross(DuelVec3 a, DuelVec3 b) {
    return duel_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float duel_v3_len(DuelVec3 a) { return sqrtf(duel_v3_dot(a, a)); }
static inline DuelVec3 duel_v3_norm(DuelVec3 a) {
    float l = duel_v3_len(a);
    return l > 1e-6f ? duel_v3_scale(a, 1.0f / l) : duel_v3(0, 0, 0);
}
static inline float duel_clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline DuelQuat duel_q(float x, float y, float z, float w) {
    DuelQuat q = { x, y, z, w }; return q;
}
static inline DuelQuat duel_q_identity(void) { return duel_q(0, 0, 0, 1); }
static inline DuelQuat duel_q_norm(DuelQuat q) {
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-6f) return duel_q_identity();
    float s = 1.0f / l;
    return duel_q(q.x * s, q.y * s, q.z * s, q.w * s);
}
/* Hamilton product: applies b first, then a (a * b). */
static inline DuelQuat duel_q_mul(DuelQuat a, DuelQuat b) {
    return duel_q(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
static inline DuelQuat duel_q_axis_angle(DuelVec3 axis, float angle_rad) {
    DuelVec3 a = duel_v3_norm(axis);
    float s = sinf(angle_rad * 0.5f);
    return duel_q(a.x * s, a.y * s, a.z * s, cosf(angle_rad * 0.5f));
}
static inline DuelVec3 duel_q_rot(DuelQuat q, DuelVec3 v) {
    DuelVec3 u = duel_v3(q.x, q.y, q.z);
    float s = q.w;
    /* v' = 2(u.v)u + (s^2 - u.u)v + 2s(u x v) */
    DuelVec3 t1 = duel_v3_scale(u, 2.0f * duel_v3_dot(u, v));
    DuelVec3 t2 = duel_v3_scale(v, s * s - duel_v3_dot(u, u));
    DuelVec3 t3 = duel_v3_scale(duel_v3_cross(u, v), 2.0f * s);
    return duel_v3_add(duel_v3_add(t1, t2), t3);
}
/* Spherical interpolation (normalized lerp is fine for small steps). */
static inline DuelQuat duel_q_slerp(DuelQuat a, DuelQuat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    float bs = 1.0f;
    if (d < 0.0f) { d = -d; bs = -1.0f; }
    float a0, a1;
    if (d > 0.9995f) { a0 = 1.0f - t; a1 = t * bs; }
    else {
        float th = acosf(duel_clampf(d, -1.0f, 1.0f));
        float st = sinf(th);
        a0 = sinf((1.0f - t) * th) / st;
        a1 = sinf(t * th) / st * bs;
    }
    return duel_q_norm(duel_q(a.x * a0 + b.x * a1, a.y * a0 + b.y * a1,
                              a.z * a0 + b.z * a1, a.w * a0 + b.w * a1));
}
/* Shortest-arc quaternion rotating unit vector 'from' to unit vector 'to'. */
static inline DuelQuat duel_q_from_to(DuelVec3 from, DuelVec3 to) {
    DuelVec3 f = duel_v3_norm(from), t = duel_v3_norm(to);
    float d = duel_v3_dot(f, t);
    if (d > 0.999999f) return duel_q_identity();
    if (d < -0.999999f) {
        DuelVec3 ortho = fabsf(f.x) < 0.9f ? duel_v3(1, 0, 0) : duel_v3(0, 1, 0);
        return duel_q_axis_angle(duel_v3_norm(duel_v3_cross(f, ortho)), DUEL_PI);
    }
    DuelVec3 c = duel_v3_cross(f, t);
    return duel_q_norm(duel_q(c.x, c.y, c.z, 1.0f + d));
}

/* Ship convention: forward = -Z, up = +Y, right = +X (OpenGL-style). */
static inline DuelVec3 duel_q_forward(DuelQuat q) { return duel_q_rot(q, duel_v3(0, 0, -1)); }
static inline DuelVec3 duel_q_up(DuelQuat q) { return duel_q_rot(q, duel_v3(0, 1, 0)); }
static inline DuelVec3 duel_q_right(DuelQuat q) { return duel_q_rot(q, duel_v3(1, 0, 0)); }

/* Column-major perspective (Vulkan NDC: y down after flip handled by viewport). */
static inline DuelMat4 duel_perspective(float fov_y_rad, float aspect, float znear, float zfar) {
    float f = 1.0f / tanf(fov_y_rad * 0.5f);
    DuelMat4 m = { { 0 } };
    m.m[0] = f / aspect; m.m[5] = f;
    m.m[10] = zfar / (znear - zfar); m.m[11] = -1.0f;
    m.m[14] = (zfar * znear) / (znear - zfar);
    return m;
}
static inline DuelMat4 duel_m4_mul(DuelMat4 a, DuelMat4 b) {
    DuelMat4 r = { { 0 } };
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++)
            r.m[c * 4 + rr] = a.m[rr] * b.m[c * 4] + a.m[4 + rr] * b.m[c * 4 + 1] +
                              a.m[8 + rr] * b.m[c * 4 + 2] + a.m[12 + rr] * b.m[c * 4 + 3];
    return r;
}
/* Rigid transform: rotation q + translation t. */
static inline DuelMat4 duel_m4_rigid(DuelQuat q, DuelVec3 t) {
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    DuelMat4 m = { {
        1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0,
        2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0,
        2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0,
        t.x, t.y, t.z, 1 } };
    return m;
}
/* Transform a point by a column-major matrix (w = 1). */
static inline DuelVec3 duel_m4_xform_point(DuelMat4 m, DuelVec3 v) {
    return duel_v3(
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12],
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13],
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14]);
}
/* Inverse of a rigid transform (for view matrices). */
static inline DuelMat4 duel_m4_rigid_inv(DuelQuat q, DuelVec3 t) {
    DuelQuat qi = duel_q(-q.x, -q.y, -q.z, q.w); /* conjugate = inverse (unit q) */
    DuelVec3 nt = duel_q_rot(qi, duel_v3(-t.x, -t.y, -t.z));
    return duel_m4_rigid(qi, nt);
}

/* Cooked OPT assets (glTF, meters, identity nodes): nose/front = +Z, up = +Y,
 * right-handed. Measured: X-wing nose z=+6.48, TIE chin guns z=+1.06,
 * cockpit instruments z~+0.16 (quest photos p0.1-build2 show the tail
 * facing the camera without this). Sim ships: forward = -Z. Every
 * OPT-derived mesh MUST go through duel_model_matrix, which appends the
 * half-turn R_y(PI): nose +Z -> ship -Z (forward), craft-right -X -> +X. */
static inline DuelQuat duel_q_opt2ship(void) { return duel_q(0, 1, 0, 0); }
static inline DuelMat4 duel_model_matrix(DuelQuat orient, DuelVec3 pos) {
    DuelMat4 ship = duel_m4_rigid(orient, pos);
    DuelMat4 basis = duel_m4_rigid(duel_q_opt2ship(), duel_v3(0, 0, 0));
    return duel_m4_mul(ship, basis);
}
/* Exact eye pose for cameraWorld = ship * seat * hmd, where seat is a pure
 * translation (the renderer never rotates the seat). Closed form:
 *   Q = ship_q (x) hmd_q ; P = ship_t + R(ship_q) * (seat_t + hmd_p).
 * Replaces the old matrix->quat extraction whose trace<=0 fallback
 * returned identity, snapping the view rotation whenever the combined
 * ship x hmd rotation exceeded ~120 deg (P0.2 head-glue root cause). */
static inline void duel_eye_pose(DuelQuat ship_q, DuelVec3 ship_t, DuelVec3 seat_t,
                                 DuelQuat hmd_q, DuelVec3 hmd_p,
                                 DuelQuat *out_q, DuelVec3 *out_p) {
    *out_q = duel_q_norm(duel_q_mul(ship_q, hmd_q));
    DuelVec3 local = duel_v3_add(seat_t, hmd_p);
    *out_p = duel_v3_add(ship_t, duel_q_rot(ship_q, local));
}
/* M2.2 motion feel: cameraWorld = ship * inertia * seat * hmd. The raw HMD
 * inputs are untouched; the inertia offset lives at the seat level.
 * With mo_p = 0 and mo_q = identity this is EXACTLY duel_eye_pose. */
static inline void duel_eye_pose_motion(DuelQuat ship_q, DuelVec3 ship_t, DuelVec3 seat_t,
                                        DuelVec3 mo_p, DuelQuat mo_q,
                                        DuelQuat hmd_q, DuelVec3 hmd_p,
                                        DuelQuat *out_q, DuelVec3 *out_p) {
    DuelQuat qi = duel_q_norm(duel_q_mul(ship_q, mo_q));
    DuelVec3 local = duel_v3_add(mo_p, duel_q_rot(mo_q, duel_v3_add(seat_t, hmd_p)));
    *out_q = duel_q_norm(duel_q_mul(qi, hmd_q));
    *out_p = duel_v3_add(ship_t, duel_q_rot(ship_q, local));
}

/* Asymmetric XR frustum -> SDL NDC (Y-UP: lower-left (-1,-1),
 * upper-right (+1,+1); Z in [0,1]). SDL converts to the backend clip
 * internally (Vulkan included), so the app outputs Y-up clip with NO
 * Y-flip of its own -- see SDL_gpu.h "Coordinate System", and use the
 * textbook glFrustum signs for the asymmetric offsets.
 * Angles in radians: left/right/up/down (OpenXR convention: left/down
 * negative, right/up positive).
 * P0.2b: m[5] was negated (vertical mirror => inverted pitch feel) and
 * m[8] was negated (horizontal shift on asymmetric FOVs); both fixed,
 * pinned by test_frustum_asymmetric_signs. */
static inline DuelMat4 duel_frustum_xr(float aL, float aR, float aU, float aD,
                                       float znear, float zfar) {
    float tanL = tanf(aL), tanR = tanf(aR), tanU = tanf(aU), tanD = tanf(aD);
    DuelMat4 m = { { 0 } };
    m.m[0] = 2.0f / (tanR - tanL);
    m.m[8] = (tanR + tanL) / (tanR - tanL);
    m.m[5] = 2.0f / (tanU - tanD);
    m.m[9] = (tanU + tanD) / (tanU - tanD);
    m.m[10] = zfar / (znear - zfar);
    m.m[11] = -1.0f;
    m.m[14] = (zfar * znear) / (znear - zfar);
    return m;
}

/* P0.4 seat calibration: neutral HMD reference captured at combat entry,
 * on reference-space recenter, and on combat reset. Corrected pose removes
 * the neutral yaw (orientation, yaw-only per task: pitch/roll stay live)
 * and the neutral position (full 3D, de-rotated into the seat frame so
 * leans point correctly whatever way the user faced at capture). Ship and
 * cockpit are NEVER touched by calibration. */
typedef enum DuelCalReason {
    DUEL_CAL_INIT = 0,   /* combat entry */
    DUEL_CAL_RECENTER,   /* Meta reference-space change */
    DUEL_CAL_RESET       /* combat-reset transition */
} DuelCalReason;

typedef struct DuelSeatCal {
    DuelVec3 neutral_pos; /* LOCAL head position at capture (mid-eye point) */
    float neutral_yaw;    /* LOCAL head yaw at capture (rad) */
    int valid;
    int generation;       /* P0.5 §12: capture count; a change mid-flight is a bug */
    int reason;           /* P0.5 §12: DuelCalReason of the last capture */
    /* M10 home view: the first reference (boot INIT, or the latest
     * explicit recenter). Death resets RESTORE it instead of capturing
     * the death head pose, so view + ship return to the start point. */
    DuelVec3 home_pos;
    float home_yaw;
    int home_valid;
} DuelSeatCal;

/* Yaw of a quaternion: the Y rotation mapping -Z to its forward. */
static inline float duel_yaw_of(DuelQuat q) {
    DuelVec3 f = duel_q_forward(q);
    float hx = -f.x, hz = -f.z;
    if (hx * hx + hz * hz < 1e-6f) return 0.0f; /* looking at zenith/nadir */
    return atan2f(hx, hz);
}

static inline void duel_cal_capture(DuelSeatCal *cal, DuelVec3 hmd_pos, DuelQuat hmd_q,
                                    int reason) {
    cal->neutral_pos = hmd_pos;
    cal->neutral_yaw = duel_yaw_of(hmd_q);
    cal->valid = 1;
    cal->generation++;
    cal->reason = reason;
}

/* M10: store the current neutral as the home view (call after an
 * INIT/RECENTER capture; a RESET capture only when no home exists yet). */
static inline void duel_cal_home_store(DuelSeatCal *cal) {
    if (!cal->valid) return;
    cal->home_pos = cal->neutral_pos;
    cal->home_yaw = cal->neutral_yaw;
    cal->home_valid = 1;
}

/* M10: restore the home view as the neutral (death reset). No-op without
 * a home. Bumps the generation (neutral changed) with RESET reason. */
static inline void duel_cal_home_restore(DuelSeatCal *cal) {
    if (!cal->home_valid) return;
    cal->neutral_pos = cal->home_pos;
    cal->neutral_yaw = cal->home_yaw;
    cal->valid = 1;
    cal->generation++;
    cal->reason = DUEL_CAL_RESET;
}

/* M12: one-shot explosion gate: fires once when a SEEN-ALIVE ship
 * dies. Ships parked dead by the battle size never fire (never seen),
 * so no ghost explosions and no stuck fullscreen flash. Returns 1 on
 * the firing frame (caller spawns FX + flash). Rearm snapshots
 * seen=alive + boom=0 on a fresh wave. */
static inline int duel_fx_boom(int *seen_alive, int alive, int *boom_done) {
    if (alive) { *seen_alive = 1; return 0; }
    if (*seen_alive && !*boom_done) { *boom_done = 1; return 1; }
    return 0;
}

/* M11: LOCAL anchor for a seat-frame panel point. The eye path maps
 * LOCAL -> neutral -> seat -> ship -> world; this inverts the first
 * step, so neutralizing the anchor returns the offset exactly (cockpit-
 * fixed panel). Yaw-only orientation facing the seat (same convention
 * as the head-placed panel: forward points INTO the screen). */
static inline void duel_home_anchor(const DuelSeatCal *cal, DuelVec3 seat_off,
                                    DuelVec3 *out_pos, DuelQuat *out_q) {
    DuelQuat yq = duel_q_axis_angle(duel_v3(0, 1, 0), cal->neutral_yaw);
    *out_pos = duel_v3_add(cal->neutral_pos, duel_q_rot(yq, seat_off));
    *out_q = yq;
}

/* Neutralize a LOCAL HMD pose into the seat frame. Uncalibrated (valid=0)
 * passes through untouched. */
static inline void duel_cal_neutralize(const DuelSeatCal *cal, DuelVec3 hmd_pos, DuelQuat hmd_q,
                                       DuelVec3 *out_pos, DuelQuat *out_q) {
    if (!cal->valid) { *out_pos = hmd_pos; *out_q = hmd_q; return; }
    DuelQuat yc = duel_q_axis_angle(duel_v3(0, 1, 0), -cal->neutral_yaw);
    *out_pos = duel_q_rot(yc, duel_v3_sub(hmd_pos, cal->neutral_pos));
    *out_q = duel_q_norm(duel_q_mul(yc, hmd_q));
}

/* M17 tablet follow: absolute game->LOCAL place (full inverse of
 * neutralize + eye path). The held panel is set from the hand every
 * frame: no drift, wrist rotation included. Pure. */
static inline DuelVec3 duel_panel_place(DuelQuat ship_q, DuelVec3 ship_t,
                                        DuelVec3 seat_t, DuelVec3 mo_p,
                                        DuelQuat mo_q, DuelVec3 neutral,
                                        float yaw, DuelVec3 gp) {
    DuelQuat qi = duel_q(-ship_q.x, -ship_q.y, -ship_q.z, ship_q.w);
    DuelQuat mi = duel_q(-mo_q.x, -mo_q.y, -mo_q.z, mo_q.w);
    DuelVec3 v = duel_q_rot(qi, duel_v3_sub(gp, ship_t));
    v = duel_q_rot(mi, duel_v3_sub(v, mo_p));
    v = duel_v3_sub(v, seat_t);
    v = duel_q_rot(duel_q_axis_angle(duel_v3(0, 1, 0), yaw), v);
    return duel_v3_add(neutral, v);
}

/* M17: a LOCAL hand orientation forward to game space (pairs with
 * unrot; the wrist steers the held panel). */
static inline DuelQuat duel_panel_handq(DuelQuat ship_q, DuelQuat mo_q,
                                        float neutral_yaw, DuelQuat hl) {
    DuelQuat yq = duel_q_axis_angle(duel_v3(0, 1, 0), -neutral_yaw);
    return duel_q_norm(duel_q_mul(ship_q, duel_q_mul(mo_q,
                                 duel_q_mul(yq, hl))));
}

/* M17 latch zone, M18 narrowed to the SIDES only (user order: no
 * top/bottom/corners). The side bands stop EDGE short of the panel
 * top/bottom (no corners) and short of the rows (rows start PAD=0.0286
 * inside > EDGE, so rows stay 100% tap-only). lx/ly/dist: pinch
 * midpoint in panel-local coords (+ in front). */
#define DUEL_PANEL_FRAME_EDGE 0.025f  /* side band inside the edges */
#define DUEL_PANEL_FRAME_OUT 0.03f    /* tolerance outside the panel */
#define DUEL_PANEL_FRAME_FRONT 0.12f  /* max pinch distance in front */
#define DUEL_PANEL_FRAME_BACK -0.08f  /* max pinch distance behind */
static inline int duel_panel_frame(float pw, float ph,
                                   float lx, float ly, float dist) {
    float ex = pw * 0.5f - fabsf(lx);
    int side = ex <= DUEL_PANEL_FRAME_EDGE &&
        fabsf(lx) <= pw * 0.5f + DUEL_PANEL_FRAME_OUT;
    int mid = ly >= -ph * 0.5f + DUEL_PANEL_FRAME_EDGE &&
        ly <= ph * 0.5f - DUEL_PANEL_FRAME_EDGE;
    int in_d = dist >= DUEL_PANEL_FRAME_BACK &&
        dist <= DUEL_PANEL_FRAME_FRONT;
    return side && mid && in_d;
}

/* M15/M17: a game orientation back to the LOCAL anchor (M17: the
 * live tablet orientation, set from the wrist every held frame). */
static inline DuelQuat duel_panel_unrot(DuelQuat ship_q, DuelQuat mo_q,
                                        float neutral_yaw, DuelQuat gq) {
    DuelQuat qi = duel_q(-ship_q.x, -ship_q.y, -ship_q.z, ship_q.w);
    DuelQuat mi = duel_q(-mo_q.x, -mo_q.y, -mo_q.z, mo_q.w);
    DuelQuat yq = duel_q_axis_angle(duel_v3(0, 1, 0), neutral_yaw);
    return duel_q_norm(duel_q_mul(yq, duel_q_mul(mi, duel_q_mul(qi, gq))));
}

/* M15 reach clamp: the dragged panel stays in a shell around the
 * neutral head point (never lost behind, never inside the face). */
#define DUEL_PANEL_MIN_D 0.30f
#define DUEL_PANEL_MAX_D 1.00f
static inline DuelVec3 duel_panel_clamp(DuelVec3 neutral, DuelVec3 c) {
    DuelVec3 d = duel_v3_sub(c, neutral);
    float l = duel_v3_len(d);
    if (l < 1e-6f)
        return duel_v3_add(neutral, duel_v3(0, 0, -DUEL_PANEL_MIN_D));
    return duel_v3_add(neutral, duel_v3_scale(d,
        duel_clampf(l, DUEL_PANEL_MIN_D, DUEL_PANEL_MAX_D) / l));
}

#endif /* XWADUELVR_MATH_H */
