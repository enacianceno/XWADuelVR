/* XWADuelVR M2.2 vehicle motion feel (SDL-free, unit-tested).
 * A VERY SMALL critically-damped inertia response applied at the
 * SEAT level (WORLD > SHIP > INERTIA > SEAT > HMD): the raw HMD pose is
 * never touched. Targets derive from measured ship accel/angular rates
 * (render keeps history); springs settle fast without overshoot.
 * Limits: |pos offset| <= 13 cm, |each lag angle| <= 0.8 deg. */
#ifndef XWADUELVR_MOTION_H
#define XWADUELVR_MOTION_H

#include "duel_math.h"

#define DUEL_MO_MAX_POS 0.13f    /* m */
#define DUEL_MO_GAIN 1.10f       /* M2.2a: user asked +10% motion feel */
#define DUEL_MO_MAX_ANG 0.014f   /* rad (~0.8 deg) */
#define DUEL_MO_OMEGA 7.0f       /* spring rate (settle ~0.6 s) */
#define DUEL_MO_K_ACCEL 0.0007f  /* m per (m/s^2): full accel ~8.4 cm */
#define DUEL_MO_K_LAT 0.030f     /* m per (rad/s) of yaw rate */
#define DUEL_MO_K_VERT 0.020f    /* m per (rad/s) of pitch rate */
#define DUEL_MO_K_LAG 0.0035f    /* lag rad per (rad/s): roll 2.2 -> 0.44 deg */
#define DUEL_MO_MAX_RATE 3.0f    /* rad/s clamp (teleport/reset safety) */
#define DUEL_MO_MAX_ACCEL 150.0f /* m/s^2 clamp */

typedef struct DuelMotion {
    DuelVec3 off_p, vel_p; /* ship-frame position offset + velocity */
    DuelVec3 off_r, vel_r; /* ship-frame lag angles (x=pitch,y=yaw,z=roll) */
    DuelQuat last_q;
    float last_speed;
    int have_last;
    /* telemetry latch (ship-local, per update) */
    float accel_fwd;
    DuelVec3 rate; /* x=pitch, y=yaw, z=roll rates (rad/s) */
} DuelMotion;

void duel_motion_reset(DuelMotion *m);

/* Feed current ship orientation + scalar speed; advances springs by dt
 * (dt clamped to [0, 1/20]). First call only latches history. */
void duel_motion_update(DuelMotion *m, DuelQuat ship_q, float speed, float dt);

/* Lag angles -> quaternion (yaw * pitch * roll; tiny angles). */
DuelQuat duel_motion_quat(const DuelMotion *m);

#endif /* XWADUELVR_MOTION_H */
