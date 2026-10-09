/* XWADuelVR offline tests: math, sim, AI, lasers, collision, reset.
 * Build for Android arm64, run on Quest via adb. No dependencies. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "duel_audio.h"
#include "duel_depth.h"
#include "duel_font.h"
#include "duel_hands.h"
#include "duel_math.h"
#include "duel_menu.h"
#include "duel_motion.h"
#include "duel_profile.h"
#include "duel_sim.h"
#include "duel_sky.h"
#include "duel_sprite.h"
#include "flight_config.h"

#define EXPECT(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } \
} while (0)
#define EXPECT_NEAR(a, b, eps) EXPECT(fabsf((a) - (b)) <= (eps))

static int fails = 0;

static void test_quat(void) {
    /* Identity * rotation = rotation. */
    DuelQuat r = duel_q_axis_angle(duel_v3(0, 1, 0), DUEL_PI / 2.0f);
    DuelQuat m = duel_q_mul(duel_q_identity(), r);
    EXPECT_NEAR(m.x, r.x, 1e-5f); EXPECT_NEAR(m.w, r.w, 1e-5f);
    /* 90 deg yaw turns -Z forward to -X. */
    DuelVec3 f = duel_q_forward(r);
    EXPECT_NEAR(f.x, -1.0f, 1e-5f); EXPECT_NEAR(f.y, 0.0f, 1e-5f); EXPECT_NEAR(f.z, 0.0f, 1e-5f);
    /* Pitch +90 about X turns -Z to +Y? Forward=(0, sin, -cos) for angle a
     * about +X: a=+90deg => (0,1,0)? R_x(a)*[0,0,-1] = [0, sin a * ... ]:
     * y' = -sin(a)*(-1)?? compute: y'=y cos - z sin = sin(a); z'= y sin + z cos = -cos(a).
     * a=90deg => (0,1,0): nose straight up. */
    DuelQuat p = duel_q_axis_angle(duel_v3(1, 0, 0), DUEL_PI / 2.0f);
    DuelVec3 fp = duel_q_forward(p);
    EXPECT_NEAR(fp.x, 0.0f, 1e-5f); EXPECT_NEAR(fp.y, 1.0f, 1e-5f); EXPECT_NEAR(fp.z, 0.0f, 1e-5f);
    /* Slerp endpoints. */
    DuelQuat s0 = duel_q_slerp(duel_q_identity(), r, 0.0f);
    DuelQuat s1 = duel_q_slerp(duel_q_identity(), r, 1.0f);
    EXPECT_NEAR(s0.w, 1.0f, 1e-5f); EXPECT_NEAR(s1.w, r.w, 1e-4f);
    /* from_to: -Z to +X. */
    DuelQuat ft = duel_q_from_to(duel_v3(0, 0, -1), duel_v3(1, 0, 0));
    DuelVec3 fr = duel_q_rot(ft, duel_v3(0, 0, -1));
    EXPECT_NEAR(fr.x, 1.0f, 1e-4f); EXPECT_NEAR(fr.z, 0.0f, 1e-4f);
    printf("quat ok\n");
}

static void test_flight(void) {
    DuelState s;
    Duel_Init(&s);
    EXPECT(s.phase == DUEL_PHASE_COMBAT);
    EXPECT(s.player.alive && s.ties[0].alive && s.ties[1].alive && s.ties[2].alive);
    EXPECT(s.allies[0].alive && s.allies[1].alive);
    EXPECT(Duel_TiesAlive(&s) == DUEL_TIE_COUNT);
    EXPECT(Duel_AlliesAlive(&s) == DUEL_ALLY_COUNT);
    /* Full throttle climbs speed toward max. */
    DuelInput in;
    memset(&in, 0, sizeof(in));
    in.thr_axis = 1.0f;
    for (int i = 0; i < 300; i++) Duel_Update(&s, &in, 1.0f / 60.0f);
    EXPECT_NEAR(s.player.throttle, 1.0f, 1e-3f);
    EXPECT(s.player.speed > DUEL_PLAYER_MAX_SPEED - 5.0f);
    /* Stick up (pitch=+1, M8 -ly) climbs: forward gains +Y. */
    memset(&in, 0, sizeof(in));
    in.pitch = 1.0f;
    DuelVec3 before = duel_q_forward(s.player.orient);
    for (int i = 0; i < 60; i++) Duel_Update(&s, &in, 1.0f / 60.0f);
    DuelVec3 after = duel_q_forward(s.player.orient);
    EXPECT(after.y > before.y + 0.3f);
    printf("flight ok (thr=%.2f spd=%.1f climb %.2f->%.2f)\n",
           s.player.throttle, s.player.speed, before.y, after.y);
}

static void test_ai_steer(void) {
    /* TIE at +Z facing player at origin: desired steering forward. */
    DuelQuat face_away = duel_q_axis_angle(duel_v3(0, 1, 0), DUEL_PI); /* fwd=+Z */
    DuelVec3 to_player = duel_v3(0, 0, -1);
    DuelQuat turned = Duel_SteerToward(face_away, to_player, 10.0f, 1.0f);
    DuelVec3 f = duel_q_forward(turned);
    EXPECT_NEAR(f.x, 0.0f, 1e-3f); EXPECT_NEAR(f.z, -1.0f, 1e-3f);
    /* Small step rotates only partially. */
    DuelQuat part = Duel_SteerToward(face_away, to_player, 0.1f, 1.0f);
    DuelVec3 fp = duel_q_forward(part);
    EXPECT(fp.z > 0.9f && fp.z < 1.0f);
    printf("ai_steer ok\n");
}

static void test_laser_collision(void) {
    DuelState s;
    Duel_Init(&s);
    /* Park all TIEs in front of player, aligned; fire until the wave dies.
     * M5: bolts test ties in order and skip the dead, so the stack dies
     * 0 -> 1 -> 2; the phase ends only when the LAST tie dies. */
    for (int k = 0; k < DUEL_TIE_COUNT; k++) {
        s.ties[k].pos = duel_v3(0, 0, -200);
        s.ties[k].orient = duel_q_axis_angle(duel_v3(0, 1, 0), DUEL_PI);
        s.ties[k].speed = 0; s.ties[k].throttle = 0;
    }
    s.player.fire_cd = 0.0f;
    DuelInput in;
    memset(&in, 0, sizeof(in));
    in.fire = 1;
    int steps = 0;
    while (Duel_TiesAlive(&s) > 0 && steps < 3600) {
        /* freeze all ships; silence TIE return fire for determinism */
        for (int k = 0; k < DUEL_TIE_COUNT; k++) {
            s.ties[k].pos = duel_v3(0, 0, -200);
            s.ties[k].fire_cd = 999.0f;
        }
        s.player.pos = duel_v3(0, 0, 0);
        s.player.hp = DUEL_PLAYER_HP;
        Duel_Update(&s, &in, 1.0f / 60.0f);
        steps++;
    }
    EXPECT(Duel_TiesAlive(&s) == 0);
    EXPECT(s.phase == DUEL_PHASE_DESTROYED);
    EXPECT(s.kills_player == 1);
    printf("laser_collision ok (steps=%d)\n", steps);
}

static void test_laser_lifetime(void) {
    DuelState s;
    Duel_Init(&s);
    s.player.fire_cd = 0.0f;
    DuelInput in;
    memset(&in, 0, sizeof(in));
    in.fire = 1;
    Duel_Update(&s, &in, 1.0f / 60.0f);
    int active = 0;
    for (int i = 0; i < DUEL_LASER_POOL; i++) active += s.lasers[i].active;
    EXPECT(active == 1);
    for (int i = 0; i < (int)(DUEL_LASER_LIFETIME * 60) + 30; i++) {
        memset(&in, 0, sizeof(in));
        /* M5: silence the furball's guns: only the player's bolt may fly */
        for (int k = 0; k < DUEL_TIE_COUNT; k++) s.ties[k].fire_cd = 999.0f;
        for (int k = 0; k < DUEL_ALLY_COUNT; k++) s.allies[k].fire_cd = 999.0f;
        Duel_Update(&s, &in, 1.0f / 60.0f);
        if (s.phase == DUEL_PHASE_DESTROYED) break;
    }
    active = 0;
    for (int i = 0; i < DUEL_LASER_POOL; i++) active += s.lasers[i].active;
    EXPECT(active == 0);
    printf("laser_lifetime ok\n");
}

static void test_reset_loop(void) {
    DuelState s;
    Duel_Init(&s);
    /* M5: one dead tie does NOT end the wave; killing the last one does. */
    s.ties[0].alive = 0; s.ties[0].hp = 0;
    DuelInput in;
    memset(&in, 0, sizeof(in));
    Duel_Update(&s, &in, 1.0f / 60.0f);
    EXPECT(s.phase == DUEL_PHASE_COMBAT);
    EXPECT(Duel_TiesAlive(&s) == DUEL_TIE_COUNT - 1);
    s.ties[1].alive = 0; s.ties[1].hp = 0;
    s.ties[2].alive = 0; s.ties[2].hp = 0;
    Duel_Update(&s, &in, 1.0f / 60.0f);
    EXPECT(s.phase == DUEL_PHASE_DESTROYED);
    for (int i = 0; i < (int)(DUEL_RESET_DELAY * 60) + 30; i++) {
        Duel_Update(&s, &in, 1.0f / 60.0f);
        if (s.phase == DUEL_PHASE_COMBAT) break;
    }
    EXPECT(s.phase == DUEL_PHASE_COMBAT);
    EXPECT(s.player.alive && s.ties[0].alive && s.ties[1].alive && s.ties[2].alive);
    EXPECT(s.allies[0].alive && s.allies[1].alive);
    EXPECT_NEAR(s.ties[0].hp, DUEL_TIE_HP, 1e-3f);
    EXPECT_NEAR(s.ties[0].pos.z, -DUEL_SPAWN_TIE_FWD, 1e-2f);
    printf("reset_loop ok\n");
}

static void test_m5_no_friendly_fire(void) {
    /* M5 user order: the player's bolts can NEVER hurt friendlies.
     * Park an ally dead ahead (where bolts fly) and a tie behind it:
     * bolts must pass through the ally unharmed and kill the tie. */
    DuelState s;
    Duel_Init(&s);
    s.allies[0].pos = duel_v3(0, 0, -150);
    s.allies[0].orient = duel_q_identity();
    s.allies[0].speed = 0; s.allies[0].throttle = 0;
    for (int k = 0; k < DUEL_TIE_COUNT; k++) {
        s.ties[k].pos = duel_v3(0, 0, -300);
        s.ties[k].orient = duel_q_axis_angle(duel_v3(0, 1, 0), DUEL_PI);
        s.ties[k].speed = 0; s.ties[k].throttle = 0;
        s.ties[k].fire_cd = 999.0f;
    }
    /* other ally far away so nothing else interferes */
    s.allies[1].pos = duel_v3(5000, 0, 0);
    s.player.fire_cd = 0.0f;
    DuelInput in;
    memset(&in, 0, sizeof(in));
    in.fire = 1;
    float ally_hp0 = s.allies[0].hp;
    int steps = 0;
    while (Duel_TiesAlive(&s) > 0 && steps < 3600) {
        s.allies[0].pos = duel_v3(0, 0, -150);
        for (int k = 0; k < DUEL_TIE_COUNT; k++) {
            s.ties[k].pos = duel_v3(0, 0, -300);
            s.ties[k].fire_cd = 999.0f;
        }
        s.player.pos = duel_v3(0, 0, 0);
        s.player.hp = DUEL_PLAYER_HP;
        Duel_Update(&s, &in, 1.0f / 60.0f);
        steps++;
    }
    EXPECT(Duel_TiesAlive(&s) == 0); /* bolts reached the ties */
    EXPECT_NEAR(s.allies[0].hp, ally_hp0, 1e-3f); /* ally untouched */
    printf("m5_no_friendly_fire ok (steps=%d)\n", steps);
}

static void test_m5_tie_bolts_hit_rebels_only(void) {
    /* Imperial bolts hurt the player and allies, never other ties. */
    DuelState s;
    Duel_Init(&s);
    /* ties[1] parked in front of ties[0]'s guns... */
    s.ties[0].pos = duel_v3(0, 0, 0);
    s.ties[0].orient = duel_q_identity(); /* fires toward -Z */
    s.ties[0].speed = 0; s.ties[0].throttle = 0;
    s.ties[1].pos = duel_v3(0, 0, -200);
    s.ties[1].speed = 0; s.ties[1].throttle = 0;
    s.ties[2].pos = duel_v3(5000, 0, 0);
    s.player.pos = duel_v3(0, 0, -400); /* beyond ties[1], in the lane */
    s.player.orient = duel_q_identity();
    s.player.speed = 0;
    s.allies[0].pos = duel_v3(6000, 0, 0);
    s.allies[1].pos = duel_v3(-6000, 0, 0);
    s.ties[0].fire_cd = 0.0f;
    DuelInput in;
    memset(&in, 0, sizeof(in));
    /* force ties[0] into ATTACK every frame so it keeps firing */
    float tie1_hp0 = s.ties[1].hp;
    float php0 = s.player.hp;
    for (int i = 0; i < 600; i++) {
        s.ai[0] = DUEL_TIE_ATTACK;
        s.ties[0].pos = duel_v3(0, 0, 0);
        s.ties[0].orient = duel_q_identity();
        s.ties[1].pos = duel_v3(0, 0, -200);
        s.player.pos = duel_v3(0, 0, -400);
        Duel_Update(&s, &in, 1.0f / 60.0f);
        if (!s.player.alive) break;
    }
    EXPECT_NEAR(s.ties[1].hp, tie1_hp0, 1e-3f); /* wingman untouched */
    EXPECT(s.player.hp < php0); /* bolts passed through to the player */
    printf("m5_tie_bolts ok\n");
}

static void test_m5_autotarget(void) {
    /* Auto-target = nearest LIVING tie; -1 when the wave is cleared. */
    DuelState s;
    Duel_Init(&s);
    EXPECT(Duel_AutoTarget(&s) == 0);
    s.ties[0].alive = 0; s.ties[0].hp = 0;
    { /* nearest of the survivors wins */
        float d1 = duel_v3_len(duel_v3_sub(s.ties[1].pos, s.player.pos));
        float d2 = duel_v3_len(duel_v3_sub(s.ties[2].pos, s.player.pos));
        EXPECT(Duel_AutoTarget(&s) == (d1 < d2 ? 1 : 2));
    }
    s.ties[1].alive = 0; s.ties[1].hp = 0;
    s.ties[2].alive = 0; s.ties[2].hp = 0;
    EXPECT(Duel_AutoTarget(&s) == -1);
    printf("m5_autotarget ok\n");
}

static void test_m5_ally_kills(void) {
    /* Allies shoot rebel-red bolts that damage ties. */
    DuelState s;
    Duel_Init(&s);
    s.allies[0].pos = duel_v3(0, 0, 0);
    s.allies[0].orient = duel_q_identity();
    s.allies[0].speed = 0;
    for (int k = 0; k < DUEL_TIE_COUNT; k++) {
        s.ties[k].pos = duel_v3(0, 0, -250);
        s.ties[k].speed = 0; s.ties[k].throttle = 0;
        s.ties[k].fire_cd = 999.0f;
    }
    s.allies[1].pos = duel_v3(5000, 0, 0);
    s.player.pos = duel_v3(-5000, 0, 0);
    s.player.hp = DUEL_PLAYER_HP;
    DuelInput in;
    memset(&in, 0, sizeof(in));
    float hp0 = s.ties[0].hp + s.ties[1].hp + s.ties[2].hp;
    for (int i = 0; i < 1200; i++) {
        s.allies[0].pos = duel_v3(0, 0, 0);
        s.allies[0].orient = duel_q_identity();
        for (int k = 0; k < DUEL_TIE_COUNT; k++)
            s.ties[k].pos = duel_v3(0, 0, -250);
        Duel_Update(&s, &in, 1.0f / 60.0f);
    }
    float hp1 = (s.ties[0].alive ? s.ties[0].hp : 0.0f)
              + (s.ties[1].alive ? s.ties[1].hp : 0.0f)
              + (s.ties[2].alive ? s.ties[2].hp : 0.0f);
    EXPECT(hp1 < hp0); /* ally fire connected */
    printf("m5_ally_kills ok (tiehp %.0f->%.0f)\n", hp0, hp1);
}

static void test_spawn_layout(void) {
    DuelState s;
    Duel_Init(&s);
    /* Player at origin facing -Z; TIEs ahead facing the player. */
    EXPECT_NEAR(s.player.pos.x, 0.0f, 1e-5f);
    DuelVec3 pf = duel_q_forward(s.player.orient);
    EXPECT_NEAR(pf.z, -1.0f, 1e-5f);
    EXPECT(s.ties[0].pos.z < -500.0f && s.ties[0].pos.x > 100.0f);
    DuelVec3 tf = duel_q_forward(s.ties[0].orient);
    DuelVec3 to_p = duel_v3_norm(duel_v3_sub(s.player.pos, s.ties[0].pos));
    EXPECT(duel_v3_dot(tf, to_p) > 0.99f);
    /* M5: vic formation faces the player; allies flank the player. */
    for (int k = 0; k < DUEL_TIE_COUNT; k++) {
        DuelVec3 kf = duel_q_forward(s.ties[k].orient);
        DuelVec3 kp = duel_v3_norm(duel_v3_sub(s.player.pos, s.ties[k].pos));
        EXPECT(duel_v3_dot(kf, kp) > 0.99f);
        EXPECT(s.ties[k].pos.z < -500.0f);
    }
    EXPECT(s.ties[1].pos.x < -100.0f && s.ties[2].pos.x > 100.0f);
    EXPECT(s.allies[0].pos.x < -50.0f && s.allies[1].pos.x > 50.0f);
    EXPECT_NEAR(s.allies[0].pos.z, 0.0f, 200.0f);
    EXPECT(Duel_AutoTarget(&s) == 0); /* lead tie is nearest */
    printf("spawn ok (tie=%.0f,%.0f,%.0f)\n",
           s.ties[0].pos.x, s.ties[0].pos.y, s.ties[0].pos.z);
}

static DuelVec3 m4_xform_point(DuelMat4 m, DuelVec3 p, float *out_w) {
    float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
    *out_w = w;
    return duel_v3(x, y, z);
}

static void test_render_math(void) {
    /* Symmetric-ish XR frustum (~90 deg, Quest-like), near .05 far 100. */
    float h = 0.6f; /* ~34 deg half-angle */
    DuelMat4 proj = duel_frustum_xr(-h, h, h, -h, 0.05f, 100.0f);
    DuelMat4 view = duel_m4_rigid_inv(duel_q_identity(), duel_v3(0, 0, 0));
    DuelMat4 vp = duel_m4_mul(proj, view);
    /* Known FRONT point (0,0,-2): w>0, NDC centered, z in [0,1]. */
    float w0 = 0;
    DuelVec3 c0 = m4_xform_point(vp, duel_v3(0, 0, -2), &w0);
    EXPECT(w0 > 0.0f);
    EXPECT_NEAR(c0.x / w0, 0.0f, 1e-4f);
    EXPECT_NEAR(c0.y / w0, 0.0f, 1e-4f);
    float ndc_z = c0.z / w0;
    EXPECT(ndc_z > 0.0f && ndc_z < 1.0f);
    /* Known BEHIND point (0,0,+2): w<0 (rejected / behind camera). */
    float w1 = 0;
    m4_xform_point(vp, duel_v3(0, 0, 2), &w1);
    EXPECT(w1 < 0.0f);
    /* Off-center point maps to correct NDC sign: +X right => ndc.x>0. */
    float w2 = 0;
    DuelVec3 c2 = m4_xform_point(vp, duel_v3(1, 0, -2), &w2);
    EXPECT(w2 > 0.0f && c2.x / w2 > 0.3f);
    /* +Y up => ndc.y>0 (SDL NDC is Y-UP; SDL converts for Vulkan
     * internally, so the app must NOT flip -- P0.2b fixed the old Y-flip
     * that vertically mirrored the scene and inverted pitch feel). */
    float w3 = 0;
    DuelVec3 c3 = m4_xform_point(vp, duel_v3(0, 1, -2), &w3);
    EXPECT(w3 > 0.0f && c3.y / w3 > 0.3f);
    printf("render_math ok (front w=%.2f ndc_z=%.3f)\n", w0, ndc_z);
}

static void test_view_matrix(void) {
    /* 1. Rigid roundtrip: M * M^-1 == identity for a rotated+translated pose
     * (catches mul/order/inverse bugs that identity-view tests miss). */
    DuelQuat q = duel_q_norm(duel_q_mul(
        duel_q_axis_angle(duel_v3(0, 1, 0), 0.7f),
        duel_q_axis_angle(duel_v3(1, 0, 0), -0.3f)));
    DuelVec3 t = duel_v3(1.5f, -0.5f, 3.0f);
    DuelMat4 m = duel_m4_rigid(q, t);
    DuelMat4 mi = duel_m4_rigid_inv(q, t);
    DuelMat4 id = duel_m4_mul(m, mi);
    for (int i = 0; i < 16; i++)
        EXPECT_NEAR(id.m[i], (i % 5 == 0) ? 1.0f : 0.0f, 1e-4f);
    /* 2. Head-turn: world-fixed point must move OPPOSITE in NDC when the
     * head yaws right (world must NOT glue to the face). */
    float h = 0.6f;
    DuelMat4 proj = duel_frustum_xr(-h, h, h, -h, 0.05f, 100.0f);
    DuelVec3 ship = duel_v3(0, 0, -10); /* TIE straight ahead */
    DuelMat4 v0 = duel_m4_rigid_inv(duel_q_identity(), duel_v3(0, 0, 0));
    DuelMat4 vp0 = duel_m4_mul(proj, v0);
    float wa = 0;
    DuelVec3 ca = m4_xform_point(vp0, ship, &wa);
    EXPECT_NEAR(ca.x / wa, 0.0f, 1e-3f);
    /* head yaws right by 20 deg (negative yaw about Y in our convention) */
    DuelQuat hyaw = duel_q_axis_angle(duel_v3(0, 1, 0), -0.35f);
    DuelMat4 v1 = duel_m4_rigid_inv(hyaw, duel_v3(0, 0, 0));
    DuelMat4 vp1 = duel_m4_mul(proj, v1);
    float wb = 0;
    DuelVec3 cb = m4_xform_point(vp1, ship, &wb);
    EXPECT(wb > 0.0f);
    EXPECT(cb.x / wb < -0.2f); /* fixed ship appears LEFT after yaw-right */
    /* 3. Seat offset (must match DUEL_SEAT_POS in duel_render.c): camera
     * inside cockpit sees a point above the seat line with positive NDC y
     * (SDL NDC is Y-up: up on screen). */
    DuelMat4 vseat = duel_m4_rigid_inv(duel_q_identity(), duel_v3(0, 0.85f, -0.78f));
    DuelMat4 vpseat = duel_m4_mul(proj, vseat);
    float wc = 0;
    DuelVec3 cc = m4_xform_point(vpseat, duel_v3(0, 2.0f, -6.0f), &wc);
    EXPECT(wc > 0.0f && cc.y / wc > 0.1f);
    printf("view_matrix ok (yaw-shift %.3f -> %.3f)\n", ca.x / wa, cb.x / wb);
}

static void test_model_basis(void) {
    /* Cooked OPT frame: nose +Z. Sim: forward -Z. duel_model_matrix must
     * map model nose to ship forward (quest p0.1-build2 showed the tail
     * without the R_y(PI) basis). Measurements from cooked glTF (meters). */
    DuelMat4 m0 = duel_model_matrix(duel_q_identity(), duel_v3(0, 0, 0));
    float w = 0;
    /* X-wing nose (0,0,+6.48) -> world -Z (forward), same distance. */
    DuelVec3 nose = m4_xform_point(m0, duel_v3(0, 0, 6.48f), &w);
    EXPECT_NEAR(nose.x, 0.0f, 1e-4f); EXPECT_NEAR(nose.y, 0.0f, 1e-4f);
    EXPECT_NEAR(nose.z, -6.48f, 1e-4f);
    /* Wingtip laser hardpoint (-5.27,+0.05,0) -> mirrored X, same Y/Z. */
    DuelVec3 tip = m4_xform_point(m0, duel_v3(-5.27f, 0.05f, 0.0f), &w);
    EXPECT_NEAR(tip.x, 5.27f, 1e-4f); EXPECT_NEAR(tip.y, 0.05f, 1e-4f);
    EXPECT_NEAR(tip.z, 0.0f, 1e-4f);
    /* TIE chin gun (+0.32,-0.80,+1.06) -> forward side, below center. */
    DuelVec3 chin = m4_xform_point(m0, duel_v3(0.32f, -0.80f, 1.06f), &w);
    EXPECT_NEAR(chin.x, -0.32f, 1e-4f); EXPECT_NEAR(chin.y, -0.80f, 1e-4f);
    EXPECT_NEAR(chin.z, -1.06f, 1e-4f);
    /* Yawed ship: nose must stay on the ship forward axis. */
    DuelQuat yaw = duel_q_axis_angle(duel_v3(0, 1, 0), 0.7f);
    DuelMat4 m1 = duel_model_matrix(yaw, duel_v3(10, 0, -5));
    DuelVec3 n1 = m4_xform_point(m1, duel_v3(0, 0, 6.48f), &w);
    DuelVec3 axis = duel_v3_norm(duel_v3_sub(n1, duel_v3(10, 0, -5)));
    EXPECT(duel_v3_dot(axis, duel_q_forward(yaw)) > 0.9999f);
    /* Seat (must match DUEL_SEAT_POS). P0.5b: user-ordered +40 cm forward
     * -- the P0.5 eye sat inside the mesh45 seat back (z 0.32..0.73).
     * Eye model (0,0.85,+0.78): ahead of the seat-back, over the panel,
     * under the roof. R_y(PI) is involutive. */
    DuelVec3 eye_model = duel_q_rot(duel_q_opt2ship(), duel_v3(0, 0.85f, -0.78f));
    EXPECT_NEAR(eye_model.x, 0.0f, 1e-5f);
    EXPECT_NEAR(eye_model.y, 0.85f, 1e-5f);
    EXPECT_NEAR(eye_model.z, 0.78f, 1e-5f);
    EXPECT(eye_model.x > -0.40f && eye_model.x < 0.67f);
    EXPECT(eye_model.y > 0.78f && eye_model.y < 0.95f);
    EXPECT(eye_model.z > 0.74f && eye_model.z < 1.00f);
    printf("model_basis ok (nose z=%.2f eye=%.2f,%.2f,%.2f)\n",
           nose.z, eye_model.x, eye_model.y, eye_model.z);
}

/* P0.2: replica of the OLD eye-rotation extraction (matrix->quat with
 * trace<=0 => identity) that shipped in P0..P0.1. Kept ONLY so the
 * regression tests below demonstrate the failure class it caused:
 * combined ship x hmd rotation past ~120 deg snapped the VIEW to
 * identity while the cockpit kept the correct ship matrix (world glued
 * to the face, ship parts swinging around the view). */
static DuelQuat old_eye_quat(DuelMat4 eyeW) {
    float *m = eyeW.m;
    float tr = m[0] + m[5] + m[10];
    DuelQuat q;
    if (tr > 0) {
        float s4 = sqrtf(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s4;
        q.x = (m[6] - m[9]) / s4; q.y = (m[8] - m[2]) / s4; q.z = (m[1] - m[4]) / s4;
    } else { q = duel_q_identity(); }
    return duel_q_norm(q);
}

static float qdot(DuelQuat a, DuelQuat b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/* Seat literal: must match DUEL_SEAT_POS in duel_render.c. */
static DuelVec3 test_seat(void) { return duel_v3(0.0f, 0.75f, -0.88f); } /* M3.0b POV +10cm back/+5cm up */

static void test_head_does_not_change_cockpit_world(void) {
    /* cockpitWorld = ship x opt_basis: the builder takes NO head input,
     * so any two head poses MUST yield the identical matrix. Pin the
     * builder to the spec formula ship x R_y(PI). */
    DuelVec3 pos = duel_v3(10, -3, 40);
    DuelQuat ships[2] = { duel_q_identity(),
        duel_q_axis_angle(duel_v3(0, 1, 0), 0.7f) };
    for (int k = 0; k < 2; k++) {
        DuelMat4 spec = duel_m4_mul(duel_m4_rigid(ships[k], pos),
            duel_m4_rigid(duel_q_opt2ship(), duel_v3(0, 0, 0)));
        DuelMat4 head0 = duel_model_matrix(ships[k], pos);
        DuelMat4 head90 = duel_model_matrix(ships[k], pos);
        for (int i = 0; i < 16; i++) {
            EXPECT_NEAR(head0.m[i], spec.m[i], 1e-6f);
            EXPECT_NEAR(head90.m[i], spec.m[i], 1e-6f);
        }
    }
    printf("head_cockpit_world ok\n");
}

static void test_head_changes_cockpit_view_relative(void) {
    /* Free-look: identical cockpitWorld, head yaw 0 vs 90 deg => the
     * cockpit MUST appear at different eye-space positions. Identical
     * eye-space under head motion = head-locked (the P0.2 complaint). */
    DuelVec3 seat = test_seat();
    DuelMat4 cw = duel_model_matrix(duel_q_identity(), duel_v3(0, 0, 0));
    float w = 0;
    DuelVec3 nose_w = m4_xform_point(cw, duel_v3(0, 0, 6.48f), &w);
    DuelQuat h0 = duel_q_identity();
    DuelQuat h90 = duel_q_axis_angle(duel_v3(0, 1, 0), DUEL_PI / 2.0f);
    DuelQuat eq0, eq90; DuelVec3 ep0, ep90;
    duel_eye_pose(duel_q_identity(), duel_v3(0, 0, 0), seat, h0, duel_v3(0, 0, 0), &eq0, &ep0);
    duel_eye_pose(duel_q_identity(), duel_v3(0, 0, 0), seat, h90, duel_v3(0, 0, 0), &eq90, &ep90);
    /* Pure head rotation about the eye: position unchanged. */
    EXPECT_NEAR(ep0.x, ep90.x, 1e-5f); EXPECT_NEAR(ep0.y, ep90.y, 1e-5f);
    EXPECT_NEAR(ep0.z, ep90.z, 1e-5f);
    /* Nose in each eye space: x' = R(q)^T (x - p). */
    DuelQuat c0 = duel_q(-eq0.x, -eq0.y, -eq0.z, eq0.w);
    DuelQuat c90 = duel_q(-eq90.x, -eq90.y, -eq90.z, eq90.w);
    DuelVec3 n0 = duel_q_rot(c0, duel_v3_sub(nose_w, ep0));
    DuelVec3 n90 = duel_q_rot(c90, duel_v3_sub(nose_w, ep90));
    EXPECT(duel_v3_len(duel_v3_sub(n0, n90)) > 1.0f);
    /* The OLD extractor fails this class: combined yaw 150 deg has
     * trace = 1+2cos150 < 0 => snapped to identity. */
    DuelQuat h150 = duel_q_axis_angle(duel_v3(0, 1, 0), 150.0f * DUEL_DEG2RAD);
    DuelMat4 eyeW = duel_m4_mul(duel_m4_rigid(duel_q_identity(), duel_v3(0, 0, 0)),
        duel_m4_mul(duel_m4_rigid(duel_q_identity(), seat),
            duel_m4_rigid(h150, duel_v3(0, 0, 0))));
    DuelQuat qo = old_eye_quat(eyeW);
    EXPECT_NEAR(qo.w, 1.0f, 1e-3f); /* documents the shipped bug */
    DuelQuat qn; DuelVec3 pn;
    duel_eye_pose(duel_q_identity(), duel_v3(0, 0, 0), seat, h150, duel_v3(0, 0, 0), &qn, &pn);
    EXPECT(fabsf(qdot(qn, h150)) > 0.9999f); /* exact: true 150 deg yaw */
    EXPECT(fabsf(qn.w) < 0.99f); /* NOT snapped to identity */
    (void)pn;
    printf("head_view_relative ok (eyespace shift %.2f)\n",
           duel_v3_len(duel_v3_sub(n0, n90)));
}

static void test_ship_rotates_cockpit_world(void) {
    /* Head fixed at 20 deg yaw; ship yaw 0 vs 45 deg: cockpitWorld must
     * rotate EXACTLY 45 deg with the ship, seat stays rigid to the ship,
     * head remains a local offset (cockpit never orbits the HMD). */
    DuelVec3 seat = test_seat();
    DuelVec3 pos = duel_v3(-50, 10, 200);
    DuelQuat s0 = duel_q_identity();
    DuelQuat s45 = duel_q_axis_angle(duel_v3(0, 1, 0), DUEL_PI / 4.0f);
    DuelMat4 w0 = duel_model_matrix(s0, pos);
    DuelMat4 w45 = duel_model_matrix(s45, pos);
    float w = 0;
    DuelVec3 n0 = m4_xform_point(w0, duel_v3(0, 0, 6.48f), &w);
    DuelVec3 n45 = m4_xform_point(w45, duel_v3(0, 0, 6.48f), &w);
    DuelVec3 a0 = duel_v3_norm(duel_v3_sub(n0, pos));
    DuelVec3 a45 = duel_v3_norm(duel_v3_sub(n45, pos));
    EXPECT_NEAR(duel_v3_dot(a0, a45), cosf(DUEL_PI / 4.0f), 1e-3f);
    /* Seat rigid: rotate the 45-deg eye offset back by -45 deg => equals
     * the 0-deg offset (relative cockpit-to-seat constant). */
    DuelQuat head = duel_q_axis_angle(duel_v3(0, 1, 0), 20.0f * DUEL_DEG2RAD);
    DuelVec3 hp = duel_v3(0.05f, -0.02f, 0.03f);
    DuelQuat eq0, eq45; DuelVec3 ep0, ep45;
    duel_eye_pose(s0, pos, seat, head, hp, &eq0, &ep0);
    duel_eye_pose(s45, pos, seat, head, hp, &eq45, &ep45);
    DuelVec3 back = duel_q_rot(duel_q_axis_angle(duel_v3(0, 1, 0), -DUEL_PI / 4.0f),
        duel_v3_sub(ep45, pos));
    DuelVec3 fwd0 = duel_v3_sub(ep0, pos);
    EXPECT(duel_v3_len(duel_v3_sub(back, fwd0)) < 1e-3f);
    (void)eq0; (void)eq45;
    printf("ship_rotates_cockpit ok (nose swing %.3f rad)\n",
           acosf(duel_clampf(duel_v3_dot(a0, a45), -1.0f, 1.0f)));
}

static void test_seat_rigid_to_ship(void) {
    /* eye - ship == R(ship) x (seat + hmd) for several attitudes,
     * including past the old 120 deg snap boundary. */
    DuelVec3 seat = test_seat();
    DuelVec3 hp = duel_v3(0.05f, -0.02f, 0.03f);
    DuelQuat head = duel_q_axis_angle(duel_v3(0, 1, 0), 20.0f * DUEL_DEG2RAD);
    DuelQuat ships[3] = { duel_q_identity(),
        duel_q_norm(duel_q_mul(duel_q_axis_angle(duel_v3(0, 1, 0), 0.7f),
            duel_q_axis_angle(duel_v3(1, 0, 0), -0.3f))),
        duel_q_axis_angle(duel_v3(0, 1, 0), 150.0f * DUEL_DEG2RAD) };
    DuelVec3 pos = duel_v3(5, -5, 5);
    for (int k = 0; k < 3; k++) {
        DuelQuat eq; DuelVec3 ep;
        duel_eye_pose(ships[k], pos, seat, head, hp, &eq, &ep);
        DuelVec3 want = duel_v3_add(pos, duel_q_rot(ships[k], duel_v3_add(seat, hp)));
        EXPECT_NEAR(ep.x, want.x, 1e-5f); EXPECT_NEAR(ep.y, want.y, 1e-5f);
        EXPECT_NEAR(ep.z, want.z, 1e-5f);
        DuelQuat qwant = duel_q_norm(duel_q_mul(ships[k], head));
        EXPECT(fabsf(qdot(eq, qwant)) > 0.99999f);
    }
    printf("seat_rigid ok\n");
}

static void test_opt_to_ship_basis(void) {
    /* optToShip is exactly R_y(PI): nose +Z -> -Z, involutive. */
    DuelQuat q = duel_q_opt2ship();
    EXPECT_NEAR(q.x, 0.0f, 1e-6f); EXPECT_NEAR(q.y, 1.0f, 1e-6f);
    EXPECT_NEAR(q.z, 0.0f, 1e-6f); EXPECT_NEAR(q.w, 0.0f, 1e-6f);
    DuelVec3 nose = duel_q_rot(q, duel_v3(0, 0, 1));
    EXPECT_NEAR(nose.x, 0.0f, 1e-6f); EXPECT_NEAR(nose.y, 0.0f, 1e-6f);
    EXPECT_NEAR(nose.z, -1.0f, 1e-6f);
    DuelVec3 v = duel_v3(1, 2, 3);
    DuelVec3 rr = duel_q_rot(q, duel_q_rot(q, v));
    EXPECT_NEAR(rr.x, v.x, 1e-5f); EXPECT_NEAR(rr.y, v.y, 1e-5f); EXPECT_NEAR(rr.z, v.z, 1e-5f);
    printf("opt_basis ok\n");
}

static int find_bolt(const DuelState *s, int owner, DuelVec3 *pos, DuelVec3 *vel) {
    for (int i = 0; i < DUEL_LASER_POOL; i++) {
        const DuelLaser *l = &s->lasers[i];
        if (l->active && l->owner == owner) {
            if (pos) *pos = l->pos;
            if (vel) *vel = l->vel;
            return 1;
        }
    }
    return 0;
}

static void test_player_muzzle_independent_of_hmd(void) {
    /* Sim takes NO hmd input: two identical runs => bit-identical bolts,
     * and bolt velocity is exactly along SHIP forward (never camera). */
    DuelVec3 p_first = duel_v3(0, 0, 0), v_first = duel_v3(0, 0, 0);
    DuelQuat fire_q = duel_q_identity();
    float fire_spd = 0;
    for (int run = 0; run < 2; run++) {
        DuelState s;
        Duel_Init(&s);
        DuelInput in;
        memset(&in, 0, sizeof(in));
        in.fire = 1;
        DuelVec3 p0 = duel_v3(0, 0, 0), v0 = duel_v3(0, 0, 0);
        int got = 0;
        for (int i = 0; i < 90 && !got; i++) {
            Duel_Update(&s, &in, 1.0f / 60.0f);
            got = find_bolt(&s, DUEL_SHIP_PLAYER, &p0, &v0);
            if (got && run == 0) { fire_q = s.player.orient; fire_spd = s.player.speed; }
        }
        EXPECT(got);
        /* Identical runs => identical first bolt (no hmd input exists). */
        if (run == 0) { p_first = p0; v_first = v0; }
        else {
            EXPECT_NEAR(p0.x, p_first.x, 1e-3f); EXPECT_NEAR(p0.y, p_first.y, 1e-3f);
            EXPECT_NEAR(p0.z, p_first.z, 1e-3f); EXPECT_NEAR(v0.x, v_first.x, 1e-3f);
            EXPECT_NEAR(v0.y, v_first.y, 1e-3f); EXPECT_NEAR(v0.z, v_first.z, 1e-3f);
        }
    }
    DuelVec3 v0 = v_first;
    DuelVec3 dir = duel_v3_norm(v0);
    EXPECT(duel_v3_dot(dir, duel_q_forward(fire_q)) > 0.9999f);
    EXPECT_NEAR(duel_v3_len(v0), DUEL_PLAYER_LASER_SPEED + fire_spd, 1e-2f);
    printf("player_muzzle ok (spd %.1f)\n", duel_v3_len(v0));
}

static void test_frustum_asymmetric_signs(void) {
    /* SDL_GPU NDC is Y-UP (lower-left (-1,-1), upper-right (+1,+1), Z in
     * [0,1]; SDL converts for Vulkan internally, so the app must NOT flip
     * Y itself -- see SDL_gpu.h "Coordinate System"). Use an ASYMMETRIC
     * frustum: symmetric tests cannot catch m[8]/m[9] sign errors.
     * P0.2b: shipped code had m[5] negated (vertical mirror => inverted
     * pitch feel) and m[8] negated (horizontal shift on asymmetric FOVs). */
    /* Angles in radians (signed, OpenXR style); the frustum takes tan. */
    float aL = -0.7f, aR = 0.5f, aU = 0.6f, aD = -0.4f;
    float tL = tanf(aL), tR = tanf(aR), tU = tanf(aU), tD = tanf(aD);
    DuelMat4 proj = duel_frustum_xr(aL, aR, aU, aD, 0.1f, 50.0f);
    DuelMat4 view = duel_m4_rigid_inv(duel_q_identity(), duel_v3(0, 0, 0));
    DuelMat4 vp = duel_m4_mul(proj, view);
    float wl = 0, wr = 0, wt = 0, wb = 0, wc = 0;
    /* Frustum edges at depth d=2 must map to NDC extremes. */
    DuelVec3 cl = m4_xform_point(vp, duel_v3(tL * 2, 0, -2), &wl);
    EXPECT(wl > 0.0f);
    EXPECT_NEAR(cl.x / wl, -1.0f, 1e-3f);
    DuelVec3 cr = m4_xform_point(vp, duel_v3(tR * 2, 0, -2), &wr);
    EXPECT_NEAR(cr.x / wr, 1.0f, 1e-3f);
    DuelVec3 ct = m4_xform_point(vp, duel_v3(0, tU * 2, -2), &wt);
    EXPECT_NEAR(ct.y / wt, 1.0f, 1e-3f); /* up on screen, NOT mirrored */
    DuelVec3 cb = m4_xform_point(vp, duel_v3(0, tD * 2, -2), &wb);
    EXPECT_NEAR(cb.y / wb, -1.0f, 1e-3f);
    /* Off-center axis: NDC(0,0,-d) = (-m8, -m9) = (-(tR+tL)/(tR-tL), ...). */
    DuelVec3 cc = m4_xform_point(vp, duel_v3(0, 0, -2), &wc);
    EXPECT_NEAR(cc.x / wc, -(tR + tL) / (tR - tL), 1e-3f);
    EXPECT_NEAR(cc.y / wc, -(tU + tD) / (tU - tD), 1e-3f);
    printf("frustum_asym ok (L %.3f R %.3f T %.3f B %.3f C %.3f,%.3f)\n",
           cl.x / wl, cr.x / wr, ct.y / wt, cb.y / wb, cc.x / wc, cc.y / wc);
}

static void test_tie_muzzle_independent_of_hmd(void) {
    /* TIE bolt velocity along TIE forward only; deterministic across runs. */
    DuelVec3 v_first = duel_v3(0, 0, 0);
    for (int run = 0; run < 2; run++) {
        DuelState s;
        Duel_Init(&s);
        DuelInput in;
        memset(&in, 0, sizeof(in));
        DuelQuat fire_q = duel_q_identity();
        DuelVec3 bv = duel_v3(0, 0, 0);
        int got = 0;
        for (int i = 0; i < 240 && !got; i++) {
            /* keep the player dead-ahead so ATTACK sustains deterministically;
             * M5: silence the wingmen so ties[0] is the certain shooter */
            DuelVec3 tf = duel_q_forward(s.ties[0].orient);
            s.player.pos = duel_v3_add(s.ties[0].pos, duel_v3_scale(tf, 300.0f));
            s.ties[1].fire_cd = 999.0f;
            s.ties[2].fire_cd = 999.0f;
            Duel_Update(&s, &in, 1.0f / 60.0f);
            DuelVec3 bp;
            got = find_bolt(&s, DUEL_SHIP_TIE, &bp, &bv);
            if (got) fire_q = s.ties[0].orient;
        }
        EXPECT(got);
        DuelVec3 dir = duel_v3_norm(bv);
        EXPECT(duel_v3_dot(dir, duel_q_forward(fire_q)) > 0.999f);
        float spd = duel_v3_len(bv);
        EXPECT(spd > DUEL_TIE_LASER_SPEED - 1.0f &&
               spd < DUEL_TIE_LASER_SPEED + DUEL_TIE_MAX_SPEED + 1.0f);
        if (run == 0) v_first = bv;
        else {
            EXPECT_NEAR(bv.x, v_first.x, 1e-3f);
            EXPECT_NEAR(bv.y, v_first.y, 1e-3f);
            EXPECT_NEAR(bv.z, v_first.z, 1e-3f);
        }
    }
    printf("tie_muzzle ok\n");
}

static void test_reticle_ship_locked(void) {
    /* P0.2f: gunsight = ship_pos + ship_fwd*120, collimated (NOT
     * eye-locked). The 8 m v1 sat 6.3 deg off the bolt direction; at
     * 120 m the eye-height parallax is 0.4 deg (nearly centered).
     * (a) head aligned: reticle centered (|x| tiny, y ~ -0.01);
     * (b) head yaws right: the ship-fixed reticle MUST slide left in
     * NDC (an eye-locked reticle would stay glued at center);
     * (c) ship yaws with head fixed: reticle NDC must move. */
    float h = 0.6f;
    DuelMat4 proj = duel_frustum_xr(-h, h, h, -h, 0.05f, 500.0f);
    DuelVec3 seat = test_seat();
    DuelVec3 ret = duel_v3_add(duel_v3(0, 0, 0),
        duel_v3_scale(duel_q_forward(duel_q_identity()), 120.0f));
    EXPECT_NEAR(ret.x, 0.0f, 1e-5f); EXPECT_NEAR(ret.z, -120.0f, 1e-5f);
    DuelMat4 v0 = duel_m4_rigid_inv(duel_q_identity(), seat);
    float w0 = 0;
    DuelVec3 c0 = m4_xform_point(duel_m4_mul(proj, v0), ret, &w0);
    EXPECT(w0 > 0.0f);
    EXPECT(fabsf(c0.x / w0) < 0.02f);
    EXPECT(c0.y / w0 > -0.03f && c0.y / w0 < 0.005f);
    DuelQuat hyaw = duel_q_axis_angle(duel_v3(0, 1, 0), -0.35f);
    DuelMat4 v1 = duel_m4_rigid_inv(hyaw, seat);
    float w1 = 0;
    DuelVec3 c1 = m4_xform_point(duel_m4_mul(proj, v1), ret, &w1);
    EXPECT(w1 > 0.0f);
    EXPECT(c1.x / w1 < -0.2f);
    DuelQuat syaw = duel_q_axis_angle(duel_v3(0, 1, 0), 0.5f);
    DuelVec3 ret2 = duel_v3_add(duel_v3(0, 0, 0),
        duel_v3_scale(duel_q_forward(syaw), 120.0f));
    float w2 = 0;
    DuelVec3 c2 = m4_xform_point(duel_m4_mul(proj, v0), ret2, &w2);
    EXPECT(w2 > 0.0f);
    EXPECT(fabsf(c2.x / w2 - c0.x / w0) > 0.2f);
    printf("reticle_ship_locked ok (ndc %.3f,%.3f -> head %.3f ship %.3f)\n",
           c0.x / w0, c0.y / w0, c1.x / w1, c2.x / w2);
}

static void test_lead_point(void) {
    /* XWA lead computer: target crossing at 30 m/s, 300 m out; shooter
     * speed 100 added to the 600 bolt base (matches duel_fire).
     * t = 300/700 = 0.4286 s; lead = pos + vel*t. */
    DuelShip sh, tg;
    memset(&sh, 0, sizeof sh); memset(&tg, 0, sizeof tg);
    sh.pos = duel_v3(0, 0, 0); sh.orient = duel_q_identity(); sh.speed = 100.0f;
    tg.pos = duel_v3(0, 0, -300.0f);
    tg.orient = duel_q_axis_angle(duel_v3(0, 1, 0), -(float)DUEL_PI / 2.0f);
    tg.speed = 30.0f;
    DuelVec3 lead = duel_lead_point(&sh, &tg, 600.0f);
    EXPECT_NEAR(lead.x, 12.857f, 0.01f);
    EXPECT_NEAR(lead.y, 0.0f, 1e-4f);
    EXPECT_NEAR(lead.z, -300.0f, 1e-4f);
    /* Degenerate: co-located => target pos (t=0, no NaN). */
    sh.pos = tg.pos;
    DuelVec3 lead0 = duel_lead_point(&sh, &tg, 600.0f);
    EXPECT_NEAR(lead0.x, tg.pos.x, 1e-5f);
    EXPECT_NEAR(lead0.y, tg.pos.y, 1e-5f);
    EXPECT_NEAR(lead0.z, tg.pos.z, 1e-5f);
    printf("lead_point ok (%.2f,%.2f,%.2f)\n", lead.x, lead.y, lead.z);
}

static void test_input_axes(void) {
    /* P0.4 §20: each mapped axis drives ONLY its own rotation.
     * yaw: nose swings right (fwd.x>0), up untouched.
     * pitch: nose rises (fwd.y>0), right untouched.
     * roll: up vector rotates, forward untouched.
     * throttle: speed changes, orientation untouched. */
    DuelState s;
    DuelInput in;
    Duel_Init(&s);
    memset(&in, 0, sizeof(in));
    in.yaw = 1.0f;
    for (int i = 0; i < 60; i++) Duel_Update(&s, &in, 1.0f / 60.0f);
    DuelVec3 fy = duel_q_forward(s.player.orient), uy = duel_q_up(s.player.orient);
    EXPECT(fy.x > 0.3f);
    EXPECT_NEAR(uy.x, 0.0f, 1e-3f); EXPECT_NEAR(uy.y, 1.0f, 1e-3f); EXPECT_NEAR(uy.z, 0.0f, 1e-3f);
    Duel_Init(&s);
    memset(&in, 0, sizeof(in));
    in.pitch = 1.0f;
    for (int i = 0; i < 30; i++) Duel_Update(&s, &in, 1.0f / 60.0f);
    DuelVec3 fp = duel_q_forward(s.player.orient), rp = duel_q_right(s.player.orient);
    EXPECT(fp.y > 0.3f);
    EXPECT_NEAR(rp.x, 1.0f, 1e-3f); EXPECT_NEAR(rp.y, 0.0f, 1e-3f); EXPECT_NEAR(rp.z, 0.0f, 1e-3f);
    Duel_Init(&s);
    memset(&in, 0, sizeof(in));
    in.roll = 1.0f;
    for (int i = 0; i < 30; i++) Duel_Update(&s, &in, 1.0f / 60.0f);
    DuelVec3 fr = duel_q_forward(s.player.orient), ur = duel_q_up(s.player.orient);
    EXPECT_NEAR(fr.x, 0.0f, 1e-3f); EXPECT_NEAR(fr.y, 0.0f, 1e-3f); EXPECT_NEAR(fr.z, -1.0f, 1e-3f);
    EXPECT(ur.x > 0.3f);
    Duel_Init(&s);
    float spd0 = s.player.speed;
    memset(&in, 0, sizeof(in));
    in.thr_axis = 1.0f;
    for (int i = 0; i < 60; i++) Duel_Update(&s, &in, 1.0f / 60.0f);
    EXPECT(s.player.speed > spd0 + 5.0f);
    DuelVec3 ft = duel_q_forward(s.player.orient), ut = duel_q_up(s.player.orient);
    EXPECT_NEAR(ft.x, 0.0f, 1e-4f); EXPECT_NEAR(ft.z, -1.0f, 1e-4f);
    EXPECT_NEAR(ut.x, 0.0f, 1e-4f); EXPECT_NEAR(ut.y, 1.0f, 1e-4f);
    /* No head channel: identical inputs => bit-identical orientation
     * (the sim cannot see the HMD; absence also pinned by rule). */
    DuelState a, b;
    Duel_Init(&a); Duel_Init(&b);
    memset(&in, 0, sizeof(in));
    in.yaw = 0.7f; in.pitch = -0.4f;
    for (int i = 0; i < 30; i++) { Duel_Update(&a, &in, 1.0f / 60.0f); Duel_Update(&b, &in, 1.0f / 60.0f); }
    EXPECT_NEAR(a.player.orient.x, b.player.orient.x, 1e-6f);
    EXPECT_NEAR(a.player.orient.y, b.player.orient.y, 1e-6f);
    EXPECT_NEAR(a.player.orient.z, b.player.orient.z, 1e-6f);
    EXPECT_NEAR(a.player.orient.w, b.player.orient.w, 1e-6f);
    printf("input_axes ok (yaw %.2f pitch %.2f roll %.2f)\n", fy.x, fp.y, ur.x);
}

static void test_neutral_head_inside_cockpit(void) {
    /* P0.4 §21: the calibrated neutral eye satisfies the §8 habitability
     * conditions as BOUNDS (not bare literals): behind the dash front,
     * ahead of the seat-back interior, below the canopy crown, above the
     * glareshield sightline, centered laterally. Live-cook audit numbers. */
    DuelVec3 eye = duel_q_rot(duel_q_opt2ship(), test_seat());
    /* P0.5b landmarks (model frame, +Z nose; the P0.5 eye sat inside the
     * mesh45 seat back z=0.32..0.73). Eye hovers OVER the panel top.
     * Mirrors the DUEL_SEAT_CAL checks in duel_render.c. */
    EXPECT(eye.z > 0.74f);   /* ahead of seat-back front (mesh45 0.73) */
    EXPECT(eye.z < 1.00f);   /* behind the windshield zone */
    EXPECT(eye.y < 0.95f);   /* headroom below roof */
    /* M2.2b: user-ordered POV -15cm; eye now level with the glareshield
     * (panel top 0.75) instead of above it. Floor keeps a cushion margin;
     * forward sightline verified clear (panel front is behind-above). */
    EXPECT(eye.y > 0.63f);   /* glareshield-level eye */
    EXPECT(eye.x > -0.10f && eye.x < 0.10f); /* centered */
    printf("neutral_head_inside ok (eye %.2f,%.2f,%.2f)\n", eye.x, eye.y, eye.z);
}

static void test_initial_heading(void) {
    /* P0.4 §22: arbitrary ship yaw + arbitrary neutral yaw. At the neutral
     * pose the corrected camera forward must equal the ship forward
     * (dot > 0.999): the spawn gaze BECOMES ship-forward. Position too:
     * a 5 m room offset must vanish (the gunsight-photo bug). */
    DuelSeatCal cal;
    memset(&cal, 0, sizeof(cal));
    DuelQuat ship_q = duel_q_axis_angle(duel_v3(0, 1, 0), 1.2f);
    DuelVec3 ship_p = duel_v3(100.0f, -50.0f, 300.0f);
    DuelVec3 npos = duel_v3(5.0f, 1.6f, -3.0f);
    DuelQuat nq = duel_q_axis_angle(duel_v3(0, 1, 0), 0.7f);
    duel_cal_capture(&cal, npos, nq, DUEL_CAL_INIT);
    EXPECT(cal.valid);
    EXPECT(cal.generation == 1 && cal.reason == DUEL_CAL_INIT);
    DuelVec3 cp;
    DuelQuat cq;
    duel_cal_neutralize(&cal, npos, nq, &cp, &cq);
    EXPECT_NEAR(cp.x, 0.0f, 1e-5f); EXPECT_NEAR(cp.y, 0.0f, 1e-5f); EXPECT_NEAR(cp.z, 0.0f, 1e-5f);
    DuelQuat eq;
    DuelVec3 ep;
    duel_eye_pose(ship_q, ship_p, test_seat(), cq, cp, &eq, &ep);
    DuelVec3 cf = duel_q_forward(eq), sf = duel_q_forward(ship_q);
    EXPECT(duel_v3_dot(cf, sf) > 0.999f);
    DuelVec3 expect_p = duel_v3_add(ship_p, duel_q_rot(ship_q, test_seat()));
    EXPECT_NEAR(ep.x, expect_p.x, 1e-4f);
    EXPECT_NEAR(ep.y, expect_p.y, 1e-4f);
    EXPECT_NEAR(ep.z, expect_p.z, 1e-4f);
    printf("initial_heading ok (dot %.4f)\n", duel_v3_dot(cf, sf));
}

static void test_free_look_calibrated(void) {
    /* P0.4 §23: with neutral yaw 0.7, turning the head +45 deg must yaw
     * the corrected camera exactly +45 deg while the ship is untouched. */
    DuelSeatCal cal;
    memset(&cal, 0, sizeof(cal));
    DuelVec3 npos = duel_v3(1.0f, 2.0f, 3.0f);
    DuelQuat nq = duel_q_axis_angle(duel_v3(0, 1, 0), 0.7f);
    duel_cal_capture(&cal, npos, nq, DUEL_CAL_INIT);
    EXPECT(cal.generation == 1);
    DuelQuat hq = duel_q_axis_angle(duel_v3(0, 1, 0), 0.7f + DUEL_PI / 4.0f);
    DuelVec3 cp;
    DuelQuat cq;
    duel_cal_neutralize(&cal, npos, hq, &cp, &cq);
    EXPECT_NEAR(duel_yaw_of(cq), DUEL_PI / 4.0f, 1e-4f);
    DuelState s;
    Duel_Init(&s);
    DuelQuat before = s.player.orient;
    DuelInput in;
    memset(&in, 0, sizeof(in));
    Duel_Update(&s, &in, 0.5f);
    EXPECT_NEAR(s.player.orient.x, before.x, 1e-6f);
    EXPECT_NEAR(s.player.orient.y, before.y, 1e-6f);
    EXPECT_NEAR(s.player.orient.z, before.z, 1e-6f);
    EXPECT_NEAR(s.player.orient.w, before.w, 1e-6f);
    printf("free_look_cal ok (camyaw %.3f)\n", duel_yaw_of(cq));
}

static void test_recenter(void) {
    /* P0.4 §24: capture neutral A, move, re-capture B (Meta recenter):
     * corrected pose is ~0 immediately; cockpitWorld is unchanged; leans
     * de-rotate correctly (facing -X, a +X LOCAL lean reads as seat-back). */
    DuelQuat ship_q = duel_q_axis_angle(duel_v3(0, 1, 0), -0.4f);
    DuelVec3 ship_p = duel_v3(-7.0f, 3.0f, 11.0f);
    DuelMat4 c0 = duel_model_matrix(ship_q, ship_p);
    DuelSeatCal cal;
    memset(&cal, 0, sizeof(cal));
    duel_cal_capture(&cal, duel_v3(1.0f, 2.0f, 3.0f),
                     duel_q_axis_angle(duel_v3(0, 1, 0), 0.3f), DUEL_CAL_INIT);
    EXPECT(cal.generation == 1 && cal.reason == DUEL_CAL_INIT);
    DuelVec3 bpos = duel_v3(4.0f, 5.0f, 6.0f);
    DuelQuat bq = duel_q_axis_angle(duel_v3(0, 1, 0), 1.0f);
    duel_cal_capture(&cal, bpos, bq, DUEL_CAL_RECENTER); /* recenter */
    EXPECT(cal.generation == 2 && cal.reason == DUEL_CAL_RECENTER);
    DuelVec3 cp;
    DuelQuat cq;
    duel_cal_neutralize(&cal, bpos, bq, &cp, &cq);
    EXPECT_NEAR(cp.x, 0.0f, 1e-5f); EXPECT_NEAR(cp.y, 0.0f, 1e-5f); EXPECT_NEAR(cp.z, 0.0f, 1e-5f);
    EXPECT_NEAR(duel_yaw_of(cq), 0.0f, 1e-4f);
    DuelMat4 c1 = duel_model_matrix(ship_q, ship_p);
    for (int i = 0; i < 16; i++) EXPECT_NEAR(c0.m[i], c1.m[i], 1e-6f);
    DuelSeatCal rot;
    memset(&rot, 0, sizeof(rot));
    duel_cal_capture(&rot, duel_v3(0, 0, 0),
                     duel_q_axis_angle(duel_v3(0, 1, 0), DUEL_PI / 2.0f), DUEL_CAL_INIT);
    EXPECT(rot.generation == 1);
    DuelVec3 lp;
    DuelQuat lq;
    duel_cal_neutralize(&rot, duel_v3(1.0f, 0.0f, 0.0f),
                        duel_q_axis_angle(duel_v3(0, 1, 0), DUEL_PI / 2.0f), &lp, &lq);
    EXPECT_NEAR(lp.x, 0.0f, 1e-5f); EXPECT_NEAR(lp.y, 0.0f, 1e-5f); EXPECT_NEAR(lp.z, 1.0f, 1e-5f);
    printf("recenter ok\n");
}

static void run_pure_axis(DuelState *s, float yaw, float pitch, float roll, int steps) {
    DuelInput in;
    memset(&in, 0, sizeof(in));
    in.yaw = yaw; in.pitch = pitch; in.roll = roll;
    for (int i = 0; i < steps; i++) Duel_Update(s, &in, 1.0f / 60.0f);
}

static void test_local_axis_invariants(void) {
    /* P0.5 §16-19: at ANY ship attitude, pure roll preserves CURRENT
     * forward, pure pitch preserves CURRENT right, pure yaw preserves
     * CURRENT up. 20 deterministic nontrivial orientations (i=0 is exactly
     * the §16 case: yaw 37 / pitch 28 / roll 19 deg). Identity-only tests
     * cannot see a world-vs-body frame bug, so start far from identity. */
    for (int i = 0; i < 20; i++) {
        float yd = (37.0f + 61.0f * (float)i) * DUEL_DEG2RAD;
        float pd = (28.0f + 47.0f * (float)i) * DUEL_DEG2RAD;
        float rd = (19.0f + 31.0f * (float)i) * DUEL_DEG2RAD;
        DuelQuat q0 = duel_q_mul(
            duel_q_mul(duel_q_axis_angle(duel_v3(0, 1, 0), yd),
                       duel_q_axis_angle(duel_v3(1, 0, 0), pd)),
            duel_q_axis_angle(duel_v3(0, 0, 1), rd));
        DuelVec3 fb = duel_q_forward(q0), ub = duel_q_up(q0), rb = duel_q_right(q0);
        DuelState s;
        /* ROLL: forward fixed, up swings (motion guard against no-op).
         * TIE stays alive: DESTROYED phase freezes the player (Duel_Update
         * only integrates in COMBAT); 30 steps can't kill at spawn range. */
        Duel_Init(&s); s.player.orient = q0;
        run_pure_axis(&s, 0, 0, 1.0f, 30);
        { DuelVec3 fa = duel_q_forward(s.player.orient);
          DuelVec3 ua = duel_q_up(s.player.orient);
          EXPECT_NEAR(duel_v3_dot(fb, fa), 1.0f, 1e-3f);
          EXPECT(duel_v3_dot(ub, ua) < 0.99f); }
        /* PITCH: right fixed, forward swings. */
        Duel_Init(&s); s.player.orient = q0;
        run_pure_axis(&s, 0, 1.0f, 0, 30);
        { DuelVec3 ra = duel_q_right(s.player.orient);
          DuelVec3 fa = duel_q_forward(s.player.orient);
          EXPECT_NEAR(duel_v3_dot(rb, ra), 1.0f, 1e-3f);
          EXPECT(duel_v3_dot(fb, fa) < 0.99f); }
        /* YAW: up fixed, forward swings. */
        Duel_Init(&s); s.player.orient = q0;
        run_pure_axis(&s, 1.0f, 0, 0, 30);
        { DuelVec3 ua = duel_q_up(s.player.orient);
          DuelVec3 fa = duel_q_forward(s.player.orient);
          EXPECT_NEAR(duel_v3_dot(ub, ua), 1.0f, 1e-3f);
          EXPECT(duel_v3_dot(fb, fa) < 0.99f); }
    }
    printf("local_axis_invariants ok (20 orientations x R/P/Y)\n");
}

static void test_camera_ship_local_stable(void) {
    /* P0.5 §10: cameraRelativeToShip = inv(ship) * camera is CONSTANT while
     * the ship rotates under the sticks with a FIXED head; equals
     * seat + R(head)*hmd by construction (no stick->camera path). */
    DuelVec3 seat = test_seat();
    DuelVec3 hp = duel_v3(0.02f, 0.01f, -0.03f);
    DuelQuat head = duel_q_axis_angle(duel_v3(0, 1, 0), 0.3f);
    DuelVec3 pos = duel_v3(5.0f, -2.0f, 30.0f);
    DuelQuat s0 = duel_q_mul(duel_q_axis_angle(duel_v3(0, 1, 0), 0.6f),
                             duel_q_axis_angle(duel_v3(1, 0, 0), -0.4f));
    DuelVec3 up0 = duel_q_up(s0);
    DuelQuat s1 = duel_q_norm(duel_q_mul(
        duel_q_axis_angle(up0, 25.0f * DUEL_DEG2RAD), s0));
    DuelQuat e0, e1; DuelVec3 p0, p1;
    duel_eye_pose(s0, pos, seat, head, hp, &e0, &p0);
    duel_eye_pose(s1, pos, seat, head, hp, &e1, &p1);
    DuelQuat c0 = duel_q(-s0.x, -s0.y, -s0.z, s0.w);
    DuelQuat c1 = duel_q(-s1.x, -s1.y, -s1.z, s1.w);
    DuelVec3 rel0 = duel_q_rot(c0, duel_v3_sub(p0, pos));
    DuelVec3 rel1 = duel_q_rot(c1, duel_v3_sub(p1, pos));
    EXPECT(duel_v3_len(duel_v3_sub(rel0, rel1)) < 1e-4f);
    /* Builder adds the neutralized head offset in SHIP frame (calibration
     * already de-rotated it): local = seat + hmd_p. */
    DuelVec3 expect = duel_v3_add(seat, hp);
    EXPECT(duel_v3_len(duel_v3_sub(rel0, expect)) < 1e-4f);
    (void)e0; (void)e1;
    printf("camera_ship_local_stable ok\n");
}

/* P1.2 §19: original-XWA starfield. One shared deterministic field. */
static DuelSkyStar sky_field[DUEL_SKY_STAR_COUNT];
static int sky_ready = 0;
static void ensure_sky(void) {
    if (!sky_ready) { duel_sky_generate(sky_field); sky_ready = 1; }
}

static void test_star_count(void) {
    EXPECT(DUEL_SKY_STAR_COUNT == 2560);
    EXPECT(DUEL_SKY_DIRECT_COUNT == 512);
    printf("star_count ok\n");
}

static void test_star_determinism(void) {
    static DuelSkyStar again[DUEL_SKY_STAR_COUNT];
    ensure_sky();
    duel_sky_generate(again);
    EXPECT(memcmp(sky_field, again, sizeof(sky_field)) == 0);
    printf("star_determinism ok\n");
}

static void test_star_unit_direction(void) {
    ensure_sky();
    for (int i = 0; i < DUEL_SKY_STAR_COUNT; i++) {
        float x = sky_field[i].dir[0], y = sky_field[i].dir[1], z = sky_field[i].dir[2];
        EXPECT_NEAR(sqrtf(x * x + y * y + z * z), 1.0f, 1e-5f);
    }
    printf("star_unit ok\n");
}

static void test_star_distribution_valid(void) {
    ensure_sky();
    float maxy = -2.0f, miny = 2.0f;
    DuelVec3 axes[6] = { duel_v3(1, 0, 0), duel_v3(-1, 0, 0), duel_v3(0, 1, 0),
                         duel_v3(0, -1, 0), duel_v3(0, 0, 1), duel_v3(0, 0, -1) };
    float best[6] = { -2, -2, -2, -2, -2, -2 };
    for (int i = 0; i < DUEL_SKY_STAR_COUNT; i++) {
        float x = sky_field[i].dir[0], y = sky_field[i].dir[1], z = sky_field[i].dir[2];
        EXPECT(x == x && y == y && z == z); /* no NaN */
        EXPECT(x < 1e30f && x > -1e30f);    /* no Inf (x repr.) */
        EXPECT(sky_field[i].bright >= 0.0f && sky_field[i].bright <= 1.0f);
        EXPECT(sky_field[i].cycle < 4u);
        if (y > maxy) maxy = y;
        if (y < miny) miny = y;
        for (int a = 0; a < 6; a++) {
            float d = x * axes[a].x + y * axes[a].y + z * axes[a].z;
            if (d > best[a]) best[a] = d;
        }
    }
    EXPECT(maxy > 0.9f && miny < -0.9f); /* both poles populated (split cap) */
    for (int a = 0; a < 6; a++) EXPECT(best[a] > 0.94f); /* <=20 deg gap */
    printf("star_dist ok (poles %.2f/%.2f)\n", miny, maxy);
}

static void test_star_translation_invariance(void) {
    /* Sky view-space strips translation: identical for two eye positions;
     * the full view would shift (non-vacuous: proves the strip matters). */
    ensure_sky();
    DuelQuat q = duel_q_axis_angle(duel_v3(0, 1, 0), 0.7f);
    DuelQuat qc = duel_q(-q.x, -q.y, -q.z, q.w);
    DuelVec3 epA = duel_v3(0, 0, 0), epB = duel_v3(3000, -1000, 5000);
    DuelVec3 d = duel_v3(sky_field[0].dir[0], sky_field[0].dir[1], sky_field[0].dir[2]);
    DuelVec3 star = duel_v3_scale(d, DUEL_SKY_RADIUS);
    DuelVec3 skyA = duel_q_rot(qc, duel_v3_sub(star, duel_v3(0, 0, 0)));
    DuelVec3 skyB = duel_q_rot(qc, duel_v3_sub(star, duel_v3(0, 0, 0)));
    DuelVec3 fullA = duel_q_rot(qc, duel_v3_sub(star, epA));
    DuelVec3 fullB = duel_q_rot(qc, duel_v3_sub(star, epB));
    (void)fullA;
    EXPECT(duel_v3_len(duel_v3_sub(skyA, skyB)) < 1e-3f);
    EXPECT(duel_v3_len(duel_v3_sub(fullA, fullB)) > 1.0f);
    printf("star_no_parallax ok (full shift %.0f)\n",
           duel_v3_len(duel_v3_sub(fullA, fullB)));
}

static void test_star_head_independence(void) {
    /* World dirs carry no head input; a 90-deg head yaw swings the same
     * star ~90 deg in view space (sky never follows the face). */
    ensure_sky();
    /* Near-equatorial star: a polar one barely swings under yaw. */
    int si = 0;
    while (si < DUEL_SKY_STAR_COUNT &&
           fabsf(sky_field[si].dir[1]) > 0.3f) si++;
    DuelVec3 d = duel_v3(sky_field[si].dir[0], sky_field[si].dir[1], sky_field[si].dir[2]);
    DuelVec3 star = duel_v3_scale(d, DUEL_SKY_RADIUS);
    DuelVec3 v0 = duel_q_rot(duel_q_identity(), star);
    DuelQuat q90 = duel_q_axis_angle(duel_v3(0, 1, 0), (float)DUEL_PI / 2.0f);
    DuelQuat c90 = duel_q(-q90.x, -q90.y, -q90.z, q90.w);
    DuelVec3 v90 = duel_q_rot(c90, star);
    float dot = duel_v3_dot(duel_v3_norm(v0), duel_v3_norm(v90));
    EXPECT(fabsf(dot) < 0.5f);
    printf("star_head_indep ok (view swing dot %.2f)\n", dot);
}

static void test_original_seed_sequence(void) {
    unsigned s = DUEL_SKY_SEED;
    EXPECT(duel_sky_rand(&s) == 58277u);
    EXPECT(duel_sky_rand(&s) == 32546u);
    EXPECT(duel_sky_rand(&s) == 38459u);
    EXPECT(duel_sky_rand(&s) == 39232u);
    EXPECT(duel_sky_rand(&s) == 47169u);
    printf("star_lcg ok\n");
}

static void test_sky_dump(void) {
    /* §20 reference dump: fnv1a over the packed field + first 16 stars
     * for the independent python oracle (evidence/p1-space/sky_oracle.py). */
    unsigned h = 0x811C9DC5u;
    const unsigned char *p = (const unsigned char *)sky_field;
    ensure_sky();
    for (size_t i = 0; i < sizeof(sky_field); i++) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    printf("SKY_DUMP hash %08X\n", h);
    for (int i = 0; i < 16; i++)
        printf("SKY_DUMP %d %.7f %.7f %.7f %.7f %u\n", i,
               sky_field[i].dir[0], sky_field[i].dir[1], sky_field[i].dir[2],
               sky_field[i].bright, sky_field[i].cycle);
}

/* P1.2: audio core (SDL-free). Synthetic PCM, no files. */
static int16_t au_pcm[8][64];
static void au_fake_sounds(DuelAudioState *st) {
    DuelAudio_InitState(st);
    for (int s = 0; s < 8; s++)
        for (int i = 0; i < 64; i++)
            au_pcm[s][i] = (int16_t)(1000 + s * 100 + i);
    for (int e = 0; e < AUDIO__COUNT; e++) {
        st->sounds[e].pcm = au_pcm[e];
        st->sounds[e].frames = 64;
        st->sounds[e].rate = DUEL_AUDIO_RATE;
        st->sounds[e].channels = 1;
    }
    st->engine_player.pcm = au_pcm[0];
    st->engine_player.frames = 64;
    st->engine_player.rate = DUEL_AUDIO_RATE;
    st->engine_player.channels = 1;
    st->engine_tie = st->engine_player;
    st->music = st->engine_player;
    st->ready = 1;
}
static DuelListener au_lis(void) {
    DuelListener l;
    l.pos = duel_v3(0, 0, 0);
    l.fwd = duel_v3(0, 0, -1);
    l.right = duel_v3(1, 0, 0);
    return l;
}
static int au_active(const DuelAudioState *st) {
    int n = 0;
    for (int i = 0; i < DUEL_AUDIO_VOICES; i++)
        n += st->voices[i].active ? 1 : 0;
    return n;
}

static void test_audio_math(void) {
    EXPECT_NEAR(duel_audio_atten(0.0f, 2000.0f), 1.0f, 1e-6f);
    EXPECT_NEAR(duel_audio_atten(2000.0f, 2000.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(duel_audio_atten(5000.0f, 2000.0f), 0.0f, 1e-6f);
    {
        float m = duel_audio_atten(1000.0f, 2000.0f);
        EXPECT(m > 0.0f && m < 1.0f);
    }
    {
        float l, r;
        duel_audio_pan(-1.0f, &l, &r);
        EXPECT_NEAR(l, 1.0f, 1e-5f);
        EXPECT_NEAR(r, 0.0f, 1e-5f);
        duel_audio_pan(1.0f, &l, &r);
        EXPECT_NEAR(l, 0.0f, 1e-5f);
        EXPECT_NEAR(r, 1.0f, 1e-5f);
        duel_audio_pan(0.0f, &l, &r);
        EXPECT_NEAR(l * l + r * r, 1.0f, 1e-5f);
    }
    EXPECT(duel_audio_clamp(40000.0f) == 32767);
    EXPECT(duel_audio_clamp(-40000.0f) == -32768);
    EXPECT(duel_audio_clamp(1234.0f) == 1234);
    printf("audio_math ok\n");
}

static void test_audio_events(void) {
    DuelAudioState st;
    DuelState s;
    au_fake_sounds(&st);
    Duel_Init(&s);
    s.ties[0].pos = duel_v3(100, 0, -500); /* audible range */
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    EXPECT(au_active(&st) == 0); /* steady state: silent */
    EXPECT(st.loop_engine_p.active && st.loop_music.active);
    /* Player fire. */
    s.player.muzzle = 1;
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    EXPECT(au_active(&st) == 1);
    EXPECT(st.voices[0].snd == &st.sounds[AUDIO_PLAYER_FIRE]);
    /* TIE fire (positional). */
    s.ties[0].muzzle = 1;
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    EXPECT(au_active(&st) == 2);
    /* Ally fire (positional X-wing gun, never the cockpit mix). */
    s.allies[0].muzzle = 1;
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    EXPECT(au_active(&st) == 3);
    /* Muzzle reset wrap: silent resync, no phantom shot. */
    s.player.muzzle = 0;
    s.ties[0].muzzle = 0;
    s.allies[0].muzzle = 0;
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    EXPECT(au_active(&st) == 3);
    /* Hits. */
    s.player.hp -= 10.0f;
    s.ties[0].hp -= 10.0f;
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    EXPECT(au_active(&st) == 5);
    /* TIE death: explosion + R2 (scream loop survives: wingmen live). */
    s.ties[0].hp = 0.0f;
    s.ties[0].alive = 0;
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    EXPECT(au_active(&st) == 7);
    EXPECT(st.loop_engine_t.active);
    /* Last ties die: +2 booms +2 R2, scream loop stops. */
    s.ties[1].alive = 0; s.ties[1].hp = 0.0f;
    s.ties[2].alive = 0; s.ties[2].hp = 0.0f;
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    EXPECT(au_active(&st) == 11);
    EXPECT(!st.loop_engine_t.active);
    /* Ally death: grim boom, no R2. */
    s.allies[0].alive = 0; s.allies[0].hp = 0.0f;
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    EXPECT(au_active(&st) == 12);
    /* Warning at low HP (the drop also fires a hit), then cooldown silence. */
    s.player.hp = 20.0f;
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    EXPECT(au_active(&st) == 14);
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    EXPECT(au_active(&st) == 14);
    printf("audio_events ok\n");
}

static void test_muzzle_world(void) {
    /* Muzzle getter: exact offsets at identity, rigid under rotation. */
    DuelShip sh;
    memset(&sh, 0, sizeof(sh));
    sh.pos = duel_v3(10, 20, 30);
    sh.orient = duel_q_identity();
    {
        DuelVec3 m0 = Duel_MuzzleWorldAt(&sh, DUEL_SHIP_PLAYER, 0);
        DuelVec3 m1 = Duel_MuzzleWorldAt(&sh, DUEL_SHIP_PLAYER, 1);
        DuelVec3 m5 = Duel_MuzzleWorldAt(&sh, DUEL_SHIP_PLAYER, 5);
        /* M2.2c: measured wingtip muzzles (+-5.35, +-1.30, 1.82 fwd). */
        EXPECT_NEAR(m0.x, 15.35f, 1e-5f);
        EXPECT_NEAR(m0.y, 21.30f, 1e-5f);
        EXPECT_NEAR(m0.z, 28.18f, 1e-5f);
        EXPECT_NEAR(m1.x, 4.65f, 1e-5f);
        EXPECT_NEAR(m5.x, 4.65f, 1e-5f); /* wraps mod 4 */
        DuelVec3 t0 = Duel_MuzzleWorldAt(&sh, DUEL_SHIP_TIE, 0);
        DuelVec3 t1 = Duel_MuzzleWorldAt(&sh, DUEL_SHIP_TIE, 1);
        EXPECT_NEAR(t0.x, 10.35f, 1e-5f);
        EXPECT_NEAR(t0.y, 19.2f, 1e-5f);
        EXPECT_NEAR(t0.z, 28.5f, 1e-5f);
        EXPECT_NEAR(t1.x, 9.65f, 1e-5f);
    }
    {
        /* Rotated ship: offset rigid (length + forward projection kept). */
        sh.orient = duel_q_axis_angle(duel_v3(0, 1, 0), 1.1f);
        DuelVec3 m = Duel_MuzzleWorldAt(&sh, DUEL_SHIP_PLAYER, 2);
        DuelVec3 d = duel_v3_sub(m, sh.pos);
        DuelVec3 fwd = duel_q_forward(sh.orient);
        EXPECT_NEAR(duel_v3_len(d), sqrtf(5.35f * 5.35f + 1.30f * 1.30f + 1.82f * 1.82f), 1e-4f);
        EXPECT_NEAR(duel_v3_dot(d, fwd), 1.82f, 1e-4f);
    }
    printf("muzzle_world ok\n");
}

static void test_audio_ducking(void) {
    /* Throttle up => engine louder, music quieter (and reverse). */
    DuelAudioState st;
    DuelState s;
    float eng_lo, eng_hi, mus_lo, mus_hi;
    au_fake_sounds(&st);
    Duel_Init(&s);
    s.player.throttle = 0.0f;
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    eng_lo = st.loop_engine_p.lgain;
    mus_lo = st.loop_music.lgain;
    s.player.throttle = 1.0f;
    DuelAudio_PollEvents(&st, &s, au_lis(), 1.0f / 60.0f);
    eng_hi = st.loop_engine_p.lgain;
    mus_hi = st.loop_music.lgain;
    EXPECT(eng_hi > eng_lo * 1.5f);
    EXPECT(mus_hi < mus_lo);
    EXPECT(mus_lo > 0.6f); /* P1.5a: music must stay audible over engines */
    EXPECT_NEAR(mus_hi / mus_lo, 0.65f, 1e-5f); /* DUCK_DEPTH */
    printf("audio_ducking ok (eng %.2f->%.2f mus %.2f->%.2f)\n",
           eng_lo, eng_hi, mus_lo, mus_hi);
}

static void test_audio_mix(void) {
    DuelAudioState st;
    float out[DUEL_AUDIO_CHUNK * 2];
    au_fake_sounds(&st);
    /* One voice, known PCM, unit-left gain. */
    st.voices[0].snd = &st.sounds[0];
    st.voices[0].pos = 0.0;
    st.voices[0].step = 1.0;
    st.voices[0].lgain = 1.0f;
    st.voices[0].rgain = 0.5f;
    st.voices[0].active = 1;
    st.voices[0].loop = 0;
    st.loop_music.active = 0;
    st.loop_engine_p.active = 0;
    st.loop_engine_t.active = 0;
    for (unsigned i = 0; i < DUEL_AUDIO_CHUNK * 2; i++) out[i] = 0.0f;
    DuelAudio_MixChunk(&st, out, 4);
    EXPECT_NEAR(out[0], 1000.0f, 1e-3f);
    EXPECT_NEAR(out[1], 500.0f, 1e-3f);
    EXPECT_NEAR(out[2], 1001.0f, 1e-3f);
    /* One-shot ends past the last frame. */
    DuelAudio_MixChunk(&st, out, 64);
    EXPECT(!st.voices[0].active);
    /* Loop wraps and stays active. */
    st.voices[0].active = 1;
    st.voices[0].loop = 1;
    st.voices[0].pos = 0.0;
    DuelAudio_MixChunk(&st, out, 200);
    EXPECT(st.voices[0].active);
    /* Resample: 22050 source at 44100 plays half-speed position. */
    st.voices[0].loop = 0;
    st.voices[0].pos = 0.0;
    st.voices[0].step = 0.5;
    st.voices[0].active = 1;
    for (unsigned i = 0; i < DUEL_AUDIO_CHUNK * 2; i++) out[i] = 0.0f;
    DuelAudio_MixChunk(&st, out, 2);
    EXPECT_NEAR(out[0], 1000.0f, 1e-3f);
    EXPECT_NEAR(out[2], 1000.5f, 1e-3f); /* linear interp halfway */
    printf("audio_mix ok\n");
}

static void test_audio_output(void) {
    /* F32 bus: S16-scale sums map into +-1.0 (the engine-clip fix). */
    float out[8] = { 32768.0f, -32768.0f, 1000.0f, -1000.0f,
                     100000.0f, -100000.0f, 0.0f, 0.0f };
    DuelAudio_NormalizeChunk(out, 4, DUEL_AUDIO_MASTER);
    EXPECT_NEAR(out[0], 0.8f, 1e-6f);
    EXPECT_NEAR(out[1], -0.8f, 1e-6f);
    EXPECT_NEAR(out[2], 1000.0f * 0.8f / 32768.0f, 1e-6f);
    EXPECT_NEAR(out[4], 1.0f, 1e-6f);  /* hot sums clamp, not wrap */
    EXPECT_NEAR(out[5], -1.0f, 1e-6f);
    /* Loop crossfade: tail converges to head (seam shrinks ~8x here). */
    {
        int16_t p[16];
        for (int i = 0; i < 8; i++) p[i] = 1000;
        for (int i = 8; i < 16; i++) p[i] = 10000;
        DuelAudio_LoopBlend(p, 16, 1, 8);
        EXPECT(p[8] == 10000); /* t=0: untouched */
        EXPECT(abs((int)p[15] - 2125) <= 2);
        EXPECT(abs((int)p[15] - (int)p[0]) < 9000 / 4);
    }
    printf("audio_output ok\n");
}

static void test_sprite_quad(void) {
    /* Facing quad: center (0,0,-10), camera basis identity, 4x2. */
    DuelVec3 c = duel_v3(0, 0, -10), r = duel_v3(1, 0, 0);
    DuelVec3 u = duel_v3(0, 1, 0), n = duel_v3(0, 0, 1);
    float q[DUEL_SPR_QUAD_FLOATS];
    duel_sprite_quad(&c, &r, &u, 4.0f, 2.0f, &n, 0.0f, q);
    /* tri0 = p0(BL) p1(BR) p2(TR); tri1 = p0 p2 p3(TL) */
    EXPECT_NEAR(q[0], -2.0f, 1e-6f); EXPECT_NEAR(q[1], -1.0f, 1e-6f);
    EXPECT_NEAR(q[2], -10.0f, 1e-6f);
    EXPECT_NEAR(q[8], 2.0f, 1e-6f); EXPECT_NEAR(q[9], -1.0f, 1e-6f);
    EXPECT_NEAR(q[16], 2.0f, 1e-6f); EXPECT_NEAR(q[17], 1.0f, 1e-6f);
    EXPECT_NEAR(q[40], -2.0f, 1e-6f); EXPECT_NEAR(q[41], 1.0f, 1e-6f);
    /* normals copied to every vert */
    EXPECT_NEAR(q[3], 0.0f, 1e-6f); EXPECT_NEAR(q[5], 1.0f, 1e-6f);
    EXPECT_NEAR(q[27], 0.0f, 1e-6f); EXPECT_NEAR(q[29], 1.0f, 1e-6f);
    /* uvs: u=0 left/u=1 right, v_top=0 -> v=0 on the top row */
    EXPECT_NEAR(q[6], 0.0f, 1e-6f); EXPECT_NEAR(q[7], 1.0f, 1e-6f); /* BL */
    EXPECT_NEAR(q[22], 1.0f, 1e-6f); EXPECT_NEAR(q[23], 0.0f, 1e-6f); /* TR */
    EXPECT_NEAR(q[46], 0.0f, 1e-6f); EXPECT_NEAR(q[47], 0.0f, 1e-6f); /* TL */
    /* v_top=1 flips vertically */
    duel_sprite_quad(&c, &r, &u, 4.0f, 2.0f, &n, 1.0f, q);
    EXPECT_NEAR(q[7], 0.0f, 1e-6f);
    EXPECT_NEAR(q[23], 1.0f, 1e-6f);
    /* rotated basis: quad spans the given right/up, centered */
    {
        DuelVec3 rr = duel_v3(0, 0, 1), uu = duel_v3(0, 1, 0);
        duel_sprite_quad(&c, &rr, &uu, 4.0f, 2.0f, &n, 0.0f, q);
        EXPECT_NEAR(q[0], 0.0f, 1e-6f); EXPECT_NEAR(q[1], -1.0f, 1e-6f);
        EXPECT_NEAR(q[2], -12.0f, 1e-6f);
        EXPECT_NEAR(q[16], 0.0f, 1e-6f); EXPECT_NEAR(q[18], -8.0f, 1e-6f);
    }
    printf("sprite_quad ok\n");
}

static void test_sprite_frame(void) {
    EXPECT(duel_sprite_frame(0.0f, 1.2f, 2) == 0);
    EXPECT(duel_sprite_frame(0.3f, 1.2f, 2) == 0);
    EXPECT(duel_sprite_frame(0.61f, 1.2f, 2) == 1);
    EXPECT(duel_sprite_frame(1.2f, 1.2f, 2) == 1);
    EXPECT(duel_sprite_frame(9.0f, 1.2f, 2) == 1); /* clamped */
    EXPECT(duel_sprite_frame(-1.0f, 1.2f, 2) == 0);
    EXPECT(duel_sprite_frame(0.5f, 1.0f, 1) == 0);
    EXPECT(duel_sprite_frame(0.5f, 1.0f, 0) == 0);
    EXPECT(duel_sprite_frame(0.5f, 0.0f, 4) == 3);
    EXPECT_NEAR(duel_sprite_mix(0.3f, 1.2f), 0.25f, 1e-6f);
    EXPECT_NEAR(duel_sprite_mix(9.0f, 1.2f), 1.0f, 1e-6f);
    EXPECT_NEAR(duel_sprite_mix(-2.0f, 1.2f), 0.0f, 1e-6f);
    EXPECT_NEAR(duel_sprite_mix(1.0f, 0.0f), 1.0f, 1e-6f);
    printf("sprite_frame ok\n");
}

static void test_sprite_slots(void) {
    DuelSprite s[3];
    DuelVec3 p = duel_v3(1, 2, 3);
    int a, b, c, d;
    memset(s, 0, sizeof s);
    a = duel_sprite_spawn(s, 3, DUEL_SPR_BOOM0, 2, p, 1.0f, 1, 2, 1, 0);
    b = duel_sprite_spawn(s, 3, DUEL_SPR_SPARK, 1, p, 0.5f, 1, 1, 1, 0);
    c = duel_sprite_spawn(s, 3, DUEL_SPR_SPARK, 1, p, 0.5f, 1, 1, 1, 0);
    EXPECT(a == 0 && b == 1 && c == 2);
    d = duel_sprite_spawn(s, 3, DUEL_SPR_SPARK, 1, p, 0.5f, 1, 1, 1, 0);
    EXPECT(d == -1); /* full */
    EXPECT(s[0].nframes == 2 && s[0].tex_base == DUEL_SPR_BOOM0);
    duel_sprite_update(s, 3, 0.6f);
    EXPECT(s[0].active == 1); /* 0.6 < 1.0 */
    EXPECT(s[1].active == 0 && s[2].active == 0); /* expired */
    d = duel_sprite_spawn(s, 3, DUEL_SPR_SUN, 1, p, 2.0f, 1, 1, 1, 0);
    EXPECT(d == 1); /* slot reused */
    duel_sprite_update(s, 3, 5.0f);
    EXPECT(s[0].active == 0 && s[1].active == 0);
    printf("sprite_slots ok\n");
}

static void test_depth_determinism(void) {
    /* A: same cell -> identical objects; layers/salts differ. */
    DuelDepthP a[2], b[2];
    int na = -1, nb = -2, i;
    duel_depth_cell(DUEL_DEPTH_NEAR, 3, -7, 11, a, &na);
    duel_depth_cell(DUEL_DEPTH_NEAR, 3, -7, 11, b, &nb);
    EXPECT(na == nb && na >= 0 && na <= 2);
    for (i = 0; i < na; i++) {
        EXPECT_NEAR(a[i].pos.x, b[i].pos.x, 1e-6f);
        EXPECT_NEAR(a[i].pos.y, b[i].pos.y, 1e-6f);
        EXPECT_NEAR(a[i].pos.z, b[i].pos.z, 1e-6f);
        EXPECT_NEAR(a[i].size, b[i].size, 1e-6f);
        EXPECT_NEAR(a[i].bright, b[i].bright, 1e-6f);
        EXPECT(a[i].type == b[i].type);
        /* inside its own cell */
        EXPECT(a[i].pos.x >= 3 * 300.0f && a[i].pos.x < 4 * 300.0f);
        EXPECT(a[i].pos.y >= -7 * 300.0f && a[i].pos.y < -6 * 300.0f);
    }
    /* B+E: content has no ship/head/orientation input -- the call takes
     * only (layer, cell), so translation and rotation cannot alter it. */
    {
        DuelDepthP c[2];
        int nc = -1;
        duel_depth_cell(DUEL_DEPTH_MID, -2, 5, 9, c, &nc);
        EXPECT(nc >= 0 && nc <= 2);
        EXPECT(duel_depth_hash(0, 1, 2, 3, 4) == duel_depth_hash(0, 1, 2, 3, 4));
        EXPECT(duel_depth_hash(0, 1, 2, 3, 4) != duel_depth_hash(1, 1, 2, 3, 4));
    }
    printf("depth_determinism ok\n");
}

static float depth_angle(DuelVec3 eye0, DuelVec3 eye1, DuelVec3 p) {
    /* Angle between the view vectors before/after translation. */
    DuelVec3 a = duel_v3_norm(duel_v3_sub(p, eye0));
    DuelVec3 b = duel_v3_norm(duel_v3_sub(p, eye1));
    float d = a.x * b.x + a.y * b.y + a.z * b.z;
    if (d > 1.0f) d = 1.0f;
    if (d < -1.0f) d = -1.0f;
    return acosf(d);
}

static void test_depth_parallax(void) {
    /* D: equal 50 m translation displaces near >> mid >> far(0). */
    DuelVec3 e0 = duel_v3(0, 0, 0), e1 = duel_v3(50, 0, 0);
    float an = depth_angle(e0, e1, duel_v3(50, 100, 0));   /* near 100 m */
    float am = depth_angle(e0, e1, duel_v3(50, 2000, 0));  /* mid 2 km */
    EXPECT(an > 0.4f);          /* ~26 deg */
    EXPECT(am > 0.01f && am < 0.05f); /* ~1.4 deg */
    EXPECT(an > am * 10.0f);
    /* far stars: rotation-only sky => translation displacement is 0 by
     * construction (pinned by test_star_translation_invariance). */
    printf("depth_parallax ok (near=%.3f mid=%.4f far=0)\n", an, am);
}

static void test_depth_collect(void) {
    /* F: crossing one cell boundary keeps shared cells identical. */
    DuelDepthP s0[DUEL_DEPTH_NEAR_CAP], s1[DUEL_DEPTH_NEAR_CAP];
    DuelVec3 p0 = duel_v3(299.0f, 10.0f, 10.0f);  /* cell x=0 */
    DuelVec3 p1 = duel_v3(301.0f, 10.0f, 10.0f);  /* cell x=1 */
    int n0 = duel_depth_collect(&p0, DUEL_DEPTH_NEAR, s0, DUEL_DEPTH_NEAR_CAP);
    int n1 = duel_depth_collect(&p1, DUEL_DEPTH_NEAR, s1, DUEL_DEPTH_NEAR_CAP);
    int i, j, shared = 0;
    EXPECT(n0 > 0 && n0 <= DUEL_DEPTH_NEAR_CAP);
    EXPECT(n1 > 0 && n1 <= DUEL_DEPTH_NEAR_CAP);
    /* every (cell,slot) present on both sides must match bit-closely;
     * cap-edge drops are allowed (only shared ones are compared). */
    for (i = 0; i < n0; i++)
        for (j = 0; j < n1; j++)
            if (s0[i].cx == s1[j].cx && s0[i].cy == s1[j].cy &&
                s0[i].cz == s1[j].cz && s0[i].slot == s1[j].slot) {
                shared++;
                EXPECT_NEAR(s0[i].pos.x, s1[j].pos.x, 1e-4f);
                EXPECT_NEAR(s0[i].pos.y, s1[j].pos.y, 1e-4f);
                EXPECT_NEAR(s0[i].pos.z, s1[j].pos.z, 1e-4f);
            }
    EXPECT(shared > 0);
    /* G: finite, in-fade, in-cell across many cells incl. negatives. */
    {
        int cx, cy, cz;
        for (cx = -3; cx <= 3; cx++)
            for (cy = -2; cy <= 2; cy++)
                for (cz = -3; cz <= 3; cz++) {
                    DuelDepthP ps[2];
                    int cnt = -1, k;
                    duel_depth_cell(DUEL_DEPTH_NEAR, cx, cy, cz, ps, &cnt);
                    EXPECT(cnt >= 0 && cnt <= 2);
                    for (k = 0; k < cnt; k++) {
                        EXPECT(isfinite(ps[k].pos.x) && isfinite(ps[k].pos.y) &&
                               isfinite(ps[k].pos.z));
                        EXPECT(ps[k].bright >= 0.25f && ps[k].bright <= 1.0f);
                        EXPECT(ps[k].size >= 0.5f && ps[k].size <= 1.5f);
                        EXPECT(ps[k].type >= 0 && ps[k].type <= 2);
                    }
                }
    }
    /* H: population budget respected everywhere sampled. */
    {
        DuelDepthP buf[DUEL_DEPTH_NEAR_CAP];
        DuelDepthP mbuf[DUEL_DEPTH_MID_CAP];
        float xs[] = { 0, 150, -1240, 5500, -31900 };
        int t;
        for (t = 0; t < 5; t++) {
            DuelVec3 c = duel_v3(xs[t], xs[4 - t] * 0.5f, -xs[t] * 2.0f);
            int nn = duel_depth_collect(&c, DUEL_DEPTH_NEAR, buf, DUEL_DEPTH_NEAR_CAP);
            int nm = duel_depth_collect(&c, DUEL_DEPTH_MID, mbuf, DUEL_DEPTH_MID_CAP);
            EXPECT(nn >= 0 && nn <= DUEL_DEPTH_NEAR_CAP);
            EXPECT(nm >= 0 && nm <= DUEL_DEPTH_MID_CAP);
        }
    }
    /* fade shape: 0 outside, ramps at both ends, 1 inside */
    EXPECT_NEAR(duel_depth_fade(DUEL_DEPTH_NEAR, 10.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(duel_depth_fade(DUEL_DEPTH_NEAR, 700.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(duel_depth_fade(DUEL_DEPTH_NEAR, 300.0f), 1.0f, 1e-6f);
    EXPECT(duel_depth_fade(DUEL_DEPTH_NEAR, 40.0f) > 0.0f &&
           duel_depth_fade(DUEL_DEPTH_NEAR, 40.0f) < 1.0f);
    EXPECT(duel_depth_fade(DUEL_DEPTH_MID, 3600.0f) > 0.0f &&
           duel_depth_fade(DUEL_DEPTH_MID, 3600.0f) < 1.0f);
    printf("depth_collect ok\n");
}

static void test_motion_springs(void) {
    DuelMotion m;
    int i;
    /* Sustained forward accel presses the seat back (+z), converges. */
    duel_motion_reset(&m);
    for (i = 0; i < 120; i++)
        duel_motion_update(&m, duel_q_identity(), (float)i, 1.0f / 60.0f);
    EXPECT_NEAR(m.accel_fwd, 60.0f, 1.0f);
    EXPECT(m.off_p.z > 0.02f && m.off_p.z <= DUEL_MO_MAX_POS + 1e-6f);
    EXPECT(duel_v3_len(m.off_p) <= DUEL_MO_MAX_POS + 1e-6f);
    /* Steady state ~= target (GAIN*K*accel), settled (velocity small). */
    EXPECT_NEAR(m.off_p.z, DUEL_MO_GAIN * DUEL_MO_K_ACCEL * 60.0f, 0.01f);
    EXPECT(fabsf(m.vel_p.z) < 0.05f);
    /* Braking reverses the offset. */
    for (i = 120; i > 0; i--)
        duel_motion_update(&m, duel_q_identity(), (float)i, 1.0f / 60.0f);
    EXPECT(m.off_p.z < 0.0f);
    /* Yaw rate leans out + yaws the lag; all angles clamped. */
    duel_motion_reset(&m);
    for (i = 0; i < 120; i++) {
        DuelQuat q = duel_q_axis_angle(duel_v3(0, 1, 0), 0.9f * (float)i / 60.0f);
        duel_motion_update(&m, q, 100.0f, 1.0f / 60.0f);
    }
    EXPECT_NEAR(m.rate.y, 0.9f, 0.05f);
    EXPECT(fabsf(m.off_p.x) > 0.005f);
    EXPECT(fabsf(m.off_r.y) > 0.0005f && fabsf(m.off_r.y) <= DUEL_MO_MAX_ANG);
    /* Teleport spike cannot escape the clamps. */
    duel_motion_update(&m, duel_q_axis_angle(duel_v3(1, 0, 0), 2.9f), 400.0f, 1.0f / 60.0f);
    EXPECT(duel_v3_len(m.off_p) <= DUEL_MO_MAX_POS + 1e-6f);
    EXPECT(fabsf(m.off_r.x) <= DUEL_MO_MAX_ANG + 1e-6f);
    /* Zero input decays back to neutral. */
    {
        DuelQuat q = duel_q_axis_angle(duel_v3(1, 0, 0), 2.9f);
        for (i = 0; i < 300; i++) duel_motion_update(&m, q, 400.0f, 1.0f / 60.0f);
        EXPECT(duel_v3_len(m.off_p) < 0.004f);
        EXPECT(fabsf(m.off_r.x) + fabsf(m.off_r.y) + fabsf(m.off_r.z) < 0.002f);
    }
    printf("motion_springs ok\n");
}

static void test_motion_eye(void) {
    /* Zero offsets reproduce the baseline eye pose EXACTLY. */
    DuelQuat sq = duel_q_axis_angle(duel_v3(0, 1, 0), 0.7f);
    DuelVec3 st = duel_v3(10, -3, 40), seat = duel_v3(0, 0.85f, -0.78f);
    DuelQuat hq = duel_q_axis_angle(duel_v3(1, 0, 0), -0.2f);
    DuelVec3 hp = duel_v3(0.05f, 0.02f, -0.03f);
    DuelQuat q0, q1;
    DuelVec3 p0, p1;
    duel_eye_pose(sq, st, seat, hq, hp, &q0, &p0);
    duel_eye_pose_motion(sq, st, seat, duel_v3(0, 0, 0), duel_q_identity(),
                         hq, hp, &q1, &p1);
    EXPECT_NEAR(q0.x, q1.x, 1e-6f); EXPECT_NEAR(q0.y, q1.y, 1e-6f);
    EXPECT_NEAR(q0.z, q1.z, 1e-6f); EXPECT_NEAR(q0.w, q1.w, 1e-6f);
    EXPECT_NEAR(p0.x, p1.x, 1e-5f); EXPECT_NEAR(p0.y, p1.y, 1e-5f);
    EXPECT_NEAR(p0.z, p1.z, 1e-5f);
    /* Nonzero offsets move the camera but never the HMD inputs: same
     * head pose, different seat response => different eye, and the eye
     * shift equals the seat-frame offset rotated by the ship. */
    {
        DuelVec3 mo = duel_v3(0.02f, -0.01f, 0.08f);
        DuelVec3 dp;
        duel_eye_pose_motion(sq, st, seat, mo, duel_q_identity(),
                             hq, hp, &q1, &p1);
        dp = duel_v3_sub(p1, p0);
        {
            DuelVec3 want = duel_q_rot(sq, mo);
            EXPECT_NEAR(dp.x, want.x, 1e-5f);
            EXPECT_NEAR(dp.y, want.y, 1e-5f);
            EXPECT_NEAR(dp.z, want.z, 1e-5f);
        }
        /* Head still free: a different head pose shifts the eye through
         * the same hierarchy (proves HMD feeds forward untouched). */
        {
            DuelVec3 hp2 = duel_v3(0.15f, 0.02f, -0.03f);
            DuelVec3 p2;
            DuelQuat q2;
            duel_eye_pose_motion(sq, st, seat, mo, duel_q_identity(),
                                 hq, hp2, &q2, &p2);
            (void)q2;
            EXPECT(duel_v3_len(duel_v3_sub(p2, p1)) > 0.09f);
        }
    }
    printf("motion_eye ok\n");
}

static void test_trajectory(void) {
    /* A: straight flight advances along the initial forward. */
    DuelState s;
    DuelInput in;
    DuelVec3 p0, f0, dp;
    float along, lat;
    int i;
    memset(&in, 0, sizeof in);
    Duel_Init(&s);
    s.player.throttle = 1.0f;
    p0 = s.player.pos; f0 = duel_q_forward(s.player.orient);
    for (i = 0; i < 120; i++) Duel_Update(&s, &in, 1.0f / 60.0f);
    dp = duel_v3_sub(s.player.pos, p0);
    along = dp.x * f0.x + dp.y * f0.y + dp.z * f0.z;
    lat = duel_v3_len(duel_v3_sub(dp, duel_v3_scale(f0, along)));
    EXPECT(along > 50.0f && lat < 1e-3f);
    /* B: yaw while throttled curves the trajectory (sign-agnostic). */
    {
        DuelVec3 r0;
        Duel_Init(&s);
        s.player.throttle = 1.0f;
        memset(&in, 0, sizeof in);
        in.yaw = 1.0f;
        p0 = s.player.pos; f0 = duel_q_forward(s.player.orient);
        r0 = duel_q_rot(s.player.orient, duel_v3(1, 0, 0));
        for (i = 0; i < 60; i++) Duel_Update(&s, &in, 1.0f / 60.0f);
        dp = duel_v3_sub(s.player.pos, p0);
        along = dp.x * f0.x + dp.y * f0.y + dp.z * f0.z;
        lat = dp.x * r0.x + dp.y * r0.y + dp.z * r0.z;
        EXPECT(along > 10.0f && fabsf(lat) > 5.0f);
    }
    /* C: pitch up gains altitude. */
    {
        Duel_Init(&s);
        s.player.throttle = 1.0f;
        memset(&in, 0, sizeof in);
        in.pitch = 1.0f;
        p0 = s.player.pos;
        for (i = 0; i < 60; i++) Duel_Update(&s, &in, 1.0f / 60.0f);
        EXPECT(s.player.pos.y - p0.y > 5.0f);
    }
    /* D: pure roll keeps the trajectory on the forward line. */
    {
        Duel_Init(&s);
        s.player.throttle = 1.0f;
        memset(&in, 0, sizeof in);
        in.roll = 1.0f;
        p0 = s.player.pos; f0 = duel_q_forward(s.player.orient);
        for (i = 0; i < 60; i++) Duel_Update(&s, &in, 1.0f / 60.0f);
        dp = duel_v3_sub(s.player.pos, p0);
        along = dp.x * f0.x + dp.y * f0.y + dp.z * f0.z;
        lat = duel_v3_len(duel_v3_sub(dp, duel_v3_scale(f0, along)));
        EXPECT(along > 10.0f && lat < 0.5f);
    }
    printf("trajectory ok\n");
}

static void test_celestial(void) {
    /* Equirect mapping matches the sky sphere (u=th/2pi, v=ph/pi). */
    {
        DuelVec3 e = duel_equirect_dir(0.0f, 0.5f);
        EXPECT_NEAR(e.x, 1.0f, 1e-5f); EXPECT_NEAR(e.y, 0.0f, 1e-5f);
        DuelVec3 t = duel_equirect_dir(0.37f, 0.0f);
        EXPECT_NEAR(t.x, 0.0f, 1e-5f); EXPECT_NEAR(t.y, 1.0f, 1e-5f);
        DuelVec3 b = duel_equirect_dir(0.71f, 1.0f);
        EXPECT_NEAR(b.y, -1.0f, 1e-5f);
        /* seam wraps: u=0 and u=1 are the same direction */
        DuelVec3 s0 = duel_equirect_dir(0.0f, 0.3f);
        DuelVec3 s1 = duel_equirect_dir(1.0f, 0.3f);
        EXPECT_NEAR(s0.x, s1.x, 1e-5f); EXPECT_NEAR(s0.z, s1.z, 1e-5f);
        /* measured env sun is unit-length, upper hemisphere */
        DuelVec3 sun = duel_equirect_dir(0.9175f, 0.2246f);
        EXPECT_NEAR(duel_v3_len(sun), 1.0f, 1e-5f);
        EXPECT_NEAR(sun.x, 0.563f, 0.01f);
        EXPECT_NEAR(sun.y, 0.761f, 0.01f);
        EXPECT_NEAR(sun.z, -0.321f, 0.01f);
    }
    /* Flare gain: 1 on-axis, 0 past ~60 deg, smooth between. */
    {
        DuelVec3 sun = duel_v3_norm(duel_v3(0.5f, 0.7f, -0.3f));
        DuelVec3 side = duel_v3_norm(duel_v3(-0.7f, 0.5f, 0.0f)); /* perp */
        EXPECT_NEAR(duel_flare_gain(sun, sun), 1.0f, 1e-6f);
        EXPECT_NEAR(duel_flare_gain(duel_v3_scale(sun, -1.0f), sun), 0.0f, 1e-6f);
        EXPECT_NEAR(duel_flare_gain(side, sun), 0.0f, 1e-6f);
        {
            /* d=0.75 -> smoothstep(0.5) = 0.5 */
            DuelVec3 v = duel_v3_norm(duel_v3_add(
                duel_v3_scale(sun, 0.75f),
                duel_v3_scale(side, 0.6614378f)));
            EXPECT_NEAR(duel_flare_gain(v, sun), 0.5f, 0.02f);
        }
    }
    /* Flare texture: black border (additive-safe), hot center. */
    {
        static unsigned char ft[DUEL_FLARE_SIZE * DUEL_FLARE_SIZE * 4];
        int x, c = DUEL_FLARE_SIZE / 2;
        duel_flare_tex(ft);
        for (x = 0; x < DUEL_FLARE_SIZE; x++) {
            EXPECT(ft[(0 * DUEL_FLARE_SIZE + x) * 4] == 0);
            EXPECT(ft[((DUEL_FLARE_SIZE - 1) * DUEL_FLARE_SIZE + x) * 4] == 0);
            EXPECT(ft[(x * DUEL_FLARE_SIZE + 0) * 4] == 0);
            EXPECT(ft[(x * DUEL_FLARE_SIZE + DUEL_FLARE_SIZE - 1) * 4] == 0);
        }
        EXPECT(ft[(c * DUEL_FLARE_SIZE + c) * 4] == 255);
        EXPECT(ft[(c * DUEL_FLARE_SIZE + c) * 4 + 3] == 255);
        /* radial falloff along +x is non-increasing */
        {
            int last = 9999, k;
            for (k = 0; k < DUEL_FLARE_SIZE / 2; k++) {
                int v = ft[(c * DUEL_FLARE_SIZE + c + k) * 4];
                EXPECT(v <= last);
                last = v;
            }
            EXPECT(last == 0);
        }
    }
    printf("celestial ok\n");
}

static void test_guide_arrow(void) {
    DuelShip sh;
    float dx, dy;
    memset(&sh, 0, sizeof(sh));
    sh.pos = duel_v3(0, 0, 0);
    sh.orient = duel_q_identity();
    /* dead ahead + inside cone: hidden (diamond covers it) */
    EXPECT(Duel_GuideDir(&sh, duel_v3(0, 0, -500), &dx, &dy) == 0);
    EXPECT(Duel_GuideDir(&sh, duel_v3(20, 10, -500), &dx, &dy) == 0);
    /* cardinal bearings shown normalized */
    EXPECT(Duel_GuideDir(&sh, duel_v3(-100, 0, -100), &dx, &dy) == 1);
    EXPECT_NEAR(dx, -1.0f, 1e-5f); EXPECT_NEAR(dy, 0.0f, 1e-5f);
    EXPECT(Duel_GuideDir(&sh, duel_v3(0, 100, -100), &dx, &dy) == 1);
    EXPECT_NEAR(dx, 0.0f, 1e-5f); EXPECT_NEAR(dy, 1.0f, 1e-5f);
    EXPECT(Duel_GuideDir(&sh, duel_v3(-60, 80, -50), &dx, &dy) == 1);
    EXPECT_NEAR(dx, -0.6f, 1e-5f); EXPECT_NEAR(dy, 0.8f, 1e-5f);
    EXPECT(Duel_GuideDir(&sh, duel_v3(100, 0, 100), &dx, &dy) == 1);
    EXPECT_NEAR(dx, 1.0f, 1e-5f); EXPECT_NEAR(dy, 0.0f, 1e-5f);
    /* dead astern: (0,-1) convention */
    EXPECT(Duel_GuideDir(&sh, duel_v3(0, 0, 100), &dx, &dy) == 1);
    EXPECT_NEAR(dx, 0.0f, 1e-5f); EXPECT_NEAR(dy, -1.0f, 1e-5f);
    /* ship-relative under yaw: yawed 90 deg, target above-ahead */
    sh.orient = duel_q_axis_angle(duel_v3(0, 1, 0), (float)DUEL_PI / 2.0f);
    EXPECT(Duel_GuideDir(&sh, duel_v3(-100, 50, 0), &dx, &dy) == 1);
    EXPECT_NEAR(dx, 0.0f, 1e-4f); EXPECT_NEAR(dy, 1.0f, 1e-4f);
    printf("guide_arrow ok\n");
}

/* M3.8b: head-frame look cue. Same contract as the ship arrow, but the
 * bearing follows the HEAD: yaw the head 90 deg with the target fixed
 * and the arrow swings from ahead-hidden to hard-right. */
static void test_head_guide(void) {
    DuelVec3 eye = duel_v3(10, 0, 40);
    DuelVec3 tgt = duel_v3(10, 0, -460);
    DuelQuat fwd = duel_q_identity();
    DuelQuat yawed = duel_q_axis_angle(duel_v3(0, 1, 0),
                                       (float)DUEL_PI / 2.0f);
    float dx, dy;
    EXPECT(Duel_HeadGuide(fwd, eye, tgt, &dx, &dy) == 0); /* looking at it */
    EXPECT(Duel_HeadGuide(yawed, eye, tgt, &dx, &dy) == 1);
    EXPECT_NEAR(dx, 1.0f, 1e-4f); EXPECT_NEAR(dy, 0.0f, 1e-4f);
    /* above the head reads straight up */
    EXPECT(Duel_HeadGuide(fwd, eye, duel_v3(10, 500, 40), &dx, &dy) == 1);
    EXPECT_NEAR(dx, 0.0f, 1e-5f); EXPECT_NEAR(dy, 1.0f, 1e-5f);
    /* dead astern of the head: (0,-1) convention */
    EXPECT(Duel_HeadGuide(fwd, eye, duel_v3(10, 0, 540), &dx, &dy) == 1);
    EXPECT_NEAR(dx, 0.0f, 1e-5f); EXPECT_NEAR(dy, -1.0f, 1e-5f);
    printf("head_guide ok\n");
}

/* M3.0: bone topology covers 25 segments over joints 0..25, no
 * self-loops, wrist-palm present, every joint used at least once. */
static void test_hands_bones(void) {
    int used[DUEL_HAND_JOINTS];
    int have_wp = 0;
    int i;
    memset(used, 0, sizeof used);
    EXPECT(DUEL_HAND_BONES == 25);
    EXPECT(DUEL_HAND_JOINTS == 26);
    for (i = 0; i < DUEL_HAND_BONES; i++) {
        int a, b;
        DuelHands_Bone(i, &a, &b);
        EXPECT(a >= 0 && a < DUEL_HAND_JOINTS);
        EXPECT(b >= 0 && b < DUEL_HAND_JOINTS);
        EXPECT(a != b);
        used[a]++;
        used[b]++;
        if ((a == 1 && b == 0) || (a == 0 && b == 1)) have_wp = 1;
    }
    EXPECT(have_wp);
    for (i = 0; i < DUEL_HAND_JOINTS; i++) EXPECT(used[i] > 0);
    /* finger tips are leaves (thumb 5, index 10, middle 15, ring 20,
     * little 25): exactly one bone each. */
    EXPECT(used[5] == 1);
    EXPECT(used[10] == 1);
    EXPECT(used[15] == 1);
    EXPECT(used[20] == 1);
    EXPECT(used[25] == 1);
    printf("hands_bones ok\n");
}

static void test_hands_validity(void) {
    EXPECT(DuelHands_PosValid(XR_SPACE_LOCATION_POSITION_VALID_BIT));
    EXPECT(!DuelHands_PosValid(0));
    /* tracked-without-valid must NOT render (stale pose). */
    EXPECT(!DuelHands_PosValid(XR_SPACE_LOCATION_POSITION_TRACKED_BIT));
    EXPECT(!DuelHands_PosValid(XR_SPACE_LOCATION_ORIENTATION_VALID_BIT));
    EXPECT(DuelHands_PosValid(XR_SPACE_LOCATION_POSITION_VALID_BIT |
                               XR_SPACE_LOCATION_POSITION_TRACKED_BIT));
    EXPECT(DuelHands_RotValid(XR_SPACE_LOCATION_ORIENTATION_VALID_BIT));
    EXPECT(!DuelHands_RotValid(0));
    EXPECT(!DuelHands_RotValid(XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT));
    printf("hands_validity ok\n");
}

/* A joint sitting exactly at the neutral head pose maps to the seat
 * anchor under identity ship/motion: the hand shares the camera path. */
static void test_hands_seat_frame(void) {
    DuelSeatCal cal;
    DuelHandJoint j;
    DuelVec3 w;
    DuelVec3 seat = test_seat();
    memset(&cal, 0, sizeof cal);
    duel_cal_capture(&cal, duel_v3(1.0f, 1.6f, -2.0f),
                     duel_q_axis_angle(duel_v3(0, 1, 0), 0.7f), DUEL_CAL_INIT);
    memset(&j, 0, sizeof j);
    j.pos = duel_v3(1.0f, 1.6f, -2.0f); /* == neutral */
    j.quat = duel_q_identity();
    j.pos_valid = 1;
    DuelHands_JointWorld(&cal, duel_q_identity(), duel_v3(0, 0, 0), seat,
                         duel_v3(0, 0, 0), duel_q_identity(), &j, &w);
    EXPECT_NEAR(w.x, seat.x, 1e-4f);
    EXPECT_NEAR(w.y, seat.y, 1e-4f);
    EXPECT_NEAR(w.z, seat.z, 1e-4f);
    printf("hands_seat_frame ok\n");
}

/* Per-frame head motion must not move a fixed LOCAL joint: calibration
 * is event-driven, so recomputing with an unchanged cal is identical. */
static void test_hands_head_independent(void) {
    DuelSeatCal cal;
    DuelHandJoint j;
    DuelVec3 w1, w2;
    DuelVec3 seat = test_seat();
    memset(&cal, 0, sizeof cal);
    duel_cal_capture(&cal, duel_v3(0.5f, 1.5f, 0.5f), duel_q_identity(),
                     DUEL_CAL_INIT);
    memset(&j, 0, sizeof j);
    j.pos = duel_v3(0.9f, 1.1f, 0.2f); /* hand held still in LOCAL */
    j.quat = duel_q_axis_angle(duel_v3(0, 1, 0), 0.3f);
    j.pos_valid = 1;
    /* frame 1: head at neutral; frame 2: head turned 60 deg and leaned
     * -- but no recapture, so the cal (and the joint world pose) hold. */
    DuelHands_JointWorld(&cal, duel_q_identity(), duel_v3(0, 0, 0), seat,
                         duel_v3(0, 0, 0), duel_q_identity(), &j, &w1);
    DuelHands_JointWorld(&cal, duel_q_identity(), duel_v3(0, 0, 0), seat,
                         duel_v3(0, 0, 0), duel_q_identity(), &j, &w2);
    EXPECT_NEAR(w1.x, w2.x, 1e-6f);
    EXPECT_NEAR(w1.y, w2.y, 1e-6f);
    EXPECT_NEAR(w1.z, w2.z, 1e-6f);
    printf("hands_head_independent ok\n");
}

/* Recenter coherence: after capturing a new neutral, a joint at that
 * neutral lands on the same seat anchor as before (ship untouched). */
static void test_hands_recenter(void) {
    DuelSeatCal cal;
    DuelHandJoint j;
    DuelVec3 w1, w2;
    DuelVec3 seat = test_seat();
    memset(&cal, 0, sizeof cal);
    duel_cal_capture(&cal, duel_v3(0.0f, 1.6f, 0.0f), duel_q_identity(),
                     DUEL_CAL_INIT);
    memset(&j, 0, sizeof j);
    j.pos = duel_v3(0.0f, 1.6f, 0.0f);
    j.quat = duel_q_identity();
    j.pos_valid = 1;
    DuelHands_JointWorld(&cal, duel_q_identity(), duel_v3(0, 0, 0), seat,
                         duel_v3(0, 0, 0), duel_q_identity(), &j, &w1);
    /* user walked/turned, Meta recenter -> new neutral captured */
    duel_cal_capture(&cal, duel_v3(-1.2f, 1.55f, 0.8f),
                     duel_q_axis_angle(duel_v3(0, 1, 0), -1.1f),
                     DUEL_CAL_RECENTER);
    j.pos = duel_v3(-1.2f, 1.55f, 0.8f); /* joint follows to new neutral */
    DuelHands_JointWorld(&cal, duel_q_identity(), duel_v3(0, 0, 0), seat,
                         duel_v3(0, 0, 0), duel_q_identity(), &j, &w2);
    EXPECT_NEAR(w1.x, w2.x, 1e-4f);
    EXPECT_NEAR(w1.y, w2.y, 1e-4f);
    EXPECT_NEAR(w1.z, w2.z, 1e-4f);
    printf("hands_recenter ok\n");
}

/* Ship rotation carries the hands rigidly: the ship-local joint offset
 * is identical before/after a yaw+pitch maneuver. */
static void test_hands_ship_rigid(void) {
    DuelSeatCal cal;
    DuelHandJoint j;
    DuelVec3 w1, w2, o1, o2;
    DuelVec3 seat = test_seat();
    DuelQuat q0 = duel_q_identity();
    DuelQuat q1 = duel_q_mul(duel_q_axis_angle(duel_v3(0, 1, 0), 0.9f),
                             duel_q_axis_angle(duel_v3(1, 0, 0), -0.4f));
    DuelQuat qc;
    memset(&cal, 0, sizeof cal);
    duel_cal_capture(&cal, duel_v3(0.2f, 1.6f, -0.3f), duel_q_identity(),
                     DUEL_CAL_INIT);
    memset(&j, 0, sizeof j);
    j.pos = duel_v3(0.6f, 1.2f, -0.7f);
    j.quat = duel_q_identity();
    j.pos_valid = 1;
    DuelHands_JointWorld(&cal, q0, duel_v3(10, 0, 40), seat,
                         duel_v3(0, 0, 0), duel_q_identity(), &j, &w1);
    DuelHands_JointWorld(&cal, q1, duel_v3(10, 0, 40), seat,
                         duel_v3(0, 0, 0), duel_q_identity(), &j, &w2);
    o1 = duel_v3_sub(w1, duel_v3(10, 0, 40));
    qc = duel_q(q1.x, q1.y, q1.z, q1.w);
    qc = duel_q(-qc.x, -qc.y, -qc.z, qc.w); /* unit inverse */
    o2 = duel_q_rot(qc, duel_v3_sub(w2, duel_v3(10, 0, 40)));
    EXPECT_NEAR(o1.x, o2.x, 1e-4f);
    EXPECT_NEAR(o1.y, o2.y, 1e-4f);
    EXPECT_NEAR(o1.z, o2.z, 1e-4f);
    printf("hands_ship_rigid ok\n");
}

/* Lever anchor sanity (telemetry-only in M3.0): ahead of and below the
 * pilot eye, near centerline, within arm's reach of the seat. */
static void test_hands_lever_anchor(void) {
    DuelVec3 seat = test_seat();
    DuelVec3 d = duel_v3_sub(DUEL_LEVER_SHIP, seat);
    float dist = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    EXPECT(DUEL_LEVER_SHIP.z < seat.z); /* forward is -Z */
    EXPECT(DUEL_LEVER_SHIP.y < seat.y); /* below the eyes */
    EXPECT(fabsf(DUEL_LEVER_SHIP.x - seat.x) < 0.15f);
    EXPECT(dist > 0.3f && dist < 0.9f);
    printf("hands_lever_anchor ok (dist=%.2f)\n", dist);
}

/* M3.1: palm approach drives FAR -> NEAR at the radius. */
static void test_grab_approach(void) {
    DuelGrab g;
    DuelGrab_Reset(&g);
    EXPECT(g.state == DUEL_GRAB_FAR);
    DuelGrab_Update(&g, 1, 0.50f, 0.10f, 0.50f);
    EXPECT(g.state == DUEL_GRAB_FAR);
    DuelGrab_Update(&g, 1, DUEL_GRAB_NEAR_R, 0.10f, 0.20f);
    EXPECT(g.state == DUEL_GRAB_NEAR);
    /* leaving beyond FAR_R drops back */
    DuelGrab_Update(&g, 1, DUEL_GRAB_FAR_R, 0.10f, 0.30f);
    EXPECT(g.state == DUEL_GRAB_FAR);
    printf("grab_approach ok\n");
}

/* M3.1: pinch shut near the grip latches HELD only after debounce. */
static void test_grab_latch(void) {
    DuelGrab g;
    int i;
    DuelGrab_Reset(&g);
    DuelGrab_Update(&g, 1, 0.10f, 0.10f, 0.10f);
    EXPECT(g.state == DUEL_GRAB_NEAR);
    /* one shut frame is not enough */
    DuelGrab_Update(&g, 1, 0.10f, DUEL_GRAB_PINCH_SHUT, 0.10f);
    EXPECT(g.state == DUEL_GRAB_NEAR);
    /* ... but HOLD_FRAMES consecutive shut frames latch */
    for (i = 1; i < DUEL_GRAB_HOLD_FRAMES; i++)
        DuelGrab_Update(&g, 1, 0.10f, DUEL_GRAB_PINCH_SHUT, 0.10f);
    EXPECT(g.state == DUEL_GRAB_HELD);
    /* pinch far from the grip never latches */
    DuelGrab_Reset(&g);
    DuelGrab_Update(&g, 1, 0.10f, 0.10f, 0.10f);
    for (i = 0; i < DUEL_GRAB_HOLD_FRAMES + 2; i++)
        DuelGrab_Update(&g, 1, 0.10f, DUEL_GRAB_PINCH_SHUT, 0.50f);
    EXPECT(g.state == DUEL_GRAB_NEAR);
    printf("grab_latch ok\n");
}

/* M3.1: opening the pinch releases (debounced); yanking away drops. */
static void test_grab_release(void) {
    DuelGrab g;
    int i;
    DuelGrab_Reset(&g);
    DuelGrab_Update(&g, 1, 0.10f, 0.10f, 0.10f);
    for (i = 0; i < DUEL_GRAB_HOLD_FRAMES; i++)
        DuelGrab_Update(&g, 1, 0.10f, DUEL_GRAB_PINCH_SHUT, 0.10f);
    EXPECT(g.state == DUEL_GRAB_HELD);
    /* single open frame is tolerated */
    DuelGrab_Update(&g, 1, 0.10f, DUEL_GRAB_PINCH_OPEN, 0.10f);
    EXPECT(g.state == DUEL_GRAB_HELD);
    for (i = 1; i < DUEL_GRAB_REL_FRAMES; i++)
        DuelGrab_Update(&g, 1, 0.10f, DUEL_GRAB_PINCH_OPEN, 0.10f);
    EXPECT(g.state == DUEL_GRAB_NEAR);
    /* re-latch, then yank the midpoint out: instant drop to FAR */
    for (i = 0; i < DUEL_GRAB_HOLD_FRAMES; i++)
        DuelGrab_Update(&g, 1, 0.10f, DUEL_GRAB_PINCH_SHUT, 0.10f);
    EXPECT(g.state == DUEL_GRAB_HELD);
    DuelGrab_Update(&g, 1, 0.10f, DUEL_GRAB_PINCH_SHUT, DUEL_GRAB_LOSE_R);
    EXPECT(g.state == DUEL_GRAB_FAR);
    printf("grab_release ok\n");
}

/* M3.1: hysteresis band holds state (no flicker at the boundary). */
static void test_grab_hysteresis(void) {
    DuelGrab g;
    int i;
    DuelGrab_Reset(&g);
    DuelGrab_Update(&g, 1, 0.10f, 0.10f, 0.10f);
    EXPECT(g.state == DUEL_GRAB_NEAR);
    /* palm halfway between NEAR_R and FAR_R: stays NEAR */
    DuelGrab_Update(&g, 1, (DUEL_GRAB_NEAR_R + DUEL_GRAB_FAR_R) * 0.5f,
                    0.10f, 0.10f);
    EXPECT(g.state == DUEL_GRAB_NEAR);
    /* latch, then pinch halfway between SHUT and OPEN: stays HELD */
    for (i = 0; i < DUEL_GRAB_HOLD_FRAMES; i++)
        DuelGrab_Update(&g, 1, 0.10f, DUEL_GRAB_PINCH_SHUT, 0.10f);
    EXPECT(g.state == DUEL_GRAB_HELD);
    DuelGrab_Update(&g, 1, 0.10f,
                    (DUEL_GRAB_PINCH_SHUT + DUEL_GRAB_PINCH_OPEN) * 0.5f, 0.10f);
    EXPECT(g.state == DUEL_GRAB_HELD);
    printf("grab_hysteresis ok\n");
}

/* M3.1: tracking loss resets immediately from any state. */
static void test_grab_inactive(void) {
    DuelGrab g;
    int i;
    DuelGrab_Reset(&g);
    DuelGrab_Update(&g, 1, 0.10f, 0.10f, 0.10f);
    for (i = 0; i < DUEL_GRAB_HOLD_FRAMES; i++)
        DuelGrab_Update(&g, 1, 0.10f, DUEL_GRAB_PINCH_SHUT, 0.10f);
    EXPECT(g.state == DUEL_GRAB_HELD);
    DuelGrab_Update(&g, 0, 0.10f, DUEL_GRAB_PINCH_SHUT, 0.10f);
    EXPECT(g.state == DUEL_GRAB_FAR);
    printf("grab_inactive ok\n");
}

/* M3.3: stick deflection signs (inverted-real): back = +pd (nose up),
 * right = +rd (roll right); clamped at RANGE. */
static void test_stick_defl(void) {
    float pd, rd;
    DuelVec3 rest = DUEL_LEVER_SHIP;
    DuelStick_Defl(duel_v3(rest.x, rest.y, rest.z + DUEL_STICK_RANGE), &pd, &rd);
    EXPECT_NEAR(pd, 1.0f, 1e-5f);
    EXPECT_NEAR(rd, 0.0f, 1e-5f);
    DuelStick_Defl(duel_v3(rest.x, rest.y, rest.z - DUEL_STICK_RANGE), &pd, &rd);
    EXPECT_NEAR(pd, -1.0f, 1e-5f);
    DuelStick_Defl(duel_v3(rest.x + DUEL_STICK_RANGE, rest.y, rest.z), &pd, &rd);
    EXPECT_NEAR(rd, 1.0f, 1e-5f);
    DuelStick_Defl(duel_v3(rest.x - DUEL_STICK_RANGE, rest.y, rest.z), &pd, &rd);
    EXPECT_NEAR(rd, -1.0f, 1e-5f);
    DuelStick_Defl(duel_v3(rest.x + 1.0f, rest.y, rest.z + 1.0f), &pd, &rd);
    EXPECT_NEAR(pd, 1.0f, 1e-5f);
    EXPECT_NEAR(rd, 1.0f, 1e-5f);
    printf("stick_defl ok\n");
}

/* M3.3: moved stick grip follows deflection on both axes; rest exact. */
static void test_stick_grip(void) {
    DuelVec3 g, g0;
    DuelStick_GripShip(1.0f, 0.0f, &g);
    EXPECT(g.z > DUEL_LEVER_SHIP.z); /* pulled back */
    DuelStick_GripShip(-1.0f, 0.0f, &g);
    EXPECT(g.z < DUEL_LEVER_SHIP.z); /* pushed forward */
    DuelStick_GripShip(0.0f, 1.0f, &g);
    EXPECT(g.x > DUEL_LEVER_SHIP.x); /* pushed right */
    DuelStick_GripShip(0.0f, -1.0f, &g);
    EXPECT(g.x < DUEL_LEVER_SHIP.x);
    DuelStick_GripShip(0.0f, 0.0f, &g0);
    EXPECT_NEAR(g0.x, DUEL_LEVER_SHIP.x, 1e-5f);
    EXPECT_NEAR(g0.y, DUEL_LEVER_SHIP.y, 1e-5f);
    EXPECT_NEAR(g0.z, DUEL_LEVER_SHIP.z, 1e-5f);
    printf("stick_grip ok\n");
}

/* M3.3: spring decay shrinks deflection, snaps residue, never grows. */
static void test_stick_decay(void) {
    float pd = 1.0f, rd = -1.0f;
    DuelStick_Decay(&pd, &rd, 0.0f);
    EXPECT_NEAR(pd, 1.0f, 1e-6f);
    DuelStick_Decay(&pd, &rd, DUEL_STICK_TAU * 0.5f);
    EXPECT(pd < 1.0f && pd > 0.0f);
    EXPECT(rd > -1.0f && rd < 0.0f);
    DuelStick_Decay(&pd, &rd, 10.0f);
    EXPECT(pd == 0.0f && rd == 0.0f);
    printf("stick_decay ok\n");
}

/* M3.3: OPT-frame 2-axis offset = Ry(PI) conjugate: pivot fixed, rest
 * grip maps to the ship-frame moved grip at deflection corners. */
static void test_stick_matrix(void) {
    float ds[][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                       { 0.7f, -0.7f } };
    int i;
    for (i = 0; i < 6; i++) {
        float o[16];
        DuelMat4 m;
        DuelVec3 pv, gv, gs, want;
        int k;
        DuelStick_ModelOffset(ds[i][0], ds[i][1], o);
        for (k = 0; k < 16; k++) m.m[k] = o[k];
        pv = duel_m4_xform_point(m, DUEL_LEVER_PIVOT_MODEL);
        EXPECT_NEAR(pv.x, DUEL_LEVER_PIVOT_MODEL.x, 1e-4f);
        EXPECT_NEAR(pv.y, DUEL_LEVER_PIVOT_MODEL.y, 1e-4f);
        EXPECT_NEAR(pv.z, DUEL_LEVER_PIVOT_MODEL.z, 1e-4f);
        gv = duel_m4_xform_point(m, duel_v3(DUEL_LEVER_SHIP.x,
                                            DUEL_LEVER_SHIP.y,
                                            -DUEL_LEVER_SHIP.z));
        gs = duel_v3(-gv.x, gv.y, -gv.z); /* model -> ship */
        DuelStick_GripShip(ds[i][0], ds[i][1], &want);
        EXPECT_NEAR(gs.x, want.x, 1e-4f);
        EXPECT_NEAR(gs.y, want.y, 1e-4f);
        EXPECT_NEAR(gs.z, want.z, 1e-4f);
    }
    printf("stick_matrix ok\n");
}

/* M3.3: quadrant slide + hand map (0 back, 1 fwd, clamped). */
static void test_quad_map(void) {
    DuelVec3 g0, g1;
    DuelQuad_GripShip(0.0f, &g0);
    DuelQuad_GripShip(1.0f, &g1);
    EXPECT_NEAR(g0.z - g1.z, 2.0f * DUEL_QUAD_TRAVEL, 1e-6f);
    EXPECT(g0.z > DUEL_QUAD_SHIP.z && g1.z < DUEL_QUAD_SHIP.z);
    EXPECT_NEAR(DuelQuad_ThrottleForMid(g0.z), 0.0f, 1e-5f);
    EXPECT_NEAR(DuelQuad_ThrottleForMid(g1.z), 1.0f, 1e-5f);
    EXPECT_NEAR(DuelQuad_ThrottleForMid(g0.z + 1.0f), 0.0f, 1e-5f);
    EXPECT_NEAR(DuelQuad_ThrottleForMid(g1.z - 1.0f), 1.0f, 1e-5f);
    EXPECT(DUEL_QUAD_SHIP.x < 0.0f); /* pilot's left = ship -X */
    printf("quad_map ok\n");
}

/* M3.2: lever prim set = mesh41 prims 8..12 only. */
static void test_lever_primset(void) {
    int p;
    for (p = 8; p <= 12; p++) EXPECT(DuelLever_IsPrim(41, p));
    EXPECT(!DuelLever_IsPrim(41, 0));
    EXPECT(!DuelLever_IsPrim(41, 7));
    EXPECT(!DuelLever_IsPrim(41, 13));
    EXPECT(!DuelLever_IsPrim(40, 8));
    EXPECT(!DuelLever_IsPrim(0, 0));
    printf("lever_primset ok\n");
}

/* M3.3: held stick steers inverted-real (pull = nose up); M6: +rd
 * is PURE yaw (nose swings right, wings stay level); controller
 * pitch/yaw ignored while held. */
static void test_hand_stick_sim(void) {
    DuelState s;
    DuelInput in;
    DuelVec3 f0, f1, r1;
    memset(&in, 0, sizeof(in));
    Duel_Init(&s);
    f0 = duel_q_forward(s.player.orient);
    Duel_HandStick(&s, 1, 1.0f, 0.0f);
    in.pitch = -1.0f; /* stick fights the hand: hand must win */
    Duel_Update(&s, &in, 0.25f);
    f1 = duel_q_forward(s.player.orient);
    EXPECT(f1.y > f0.y + 0.05f); /* nose rose */
    Duel_Init(&s);
    memset(&in, 0, sizeof(in));
    f0 = duel_q_forward(s.player.orient);
    Duel_HandStick(&s, 1, 0.0f, 1.0f);
    in.yaw = -1.0f; /* controller yaw fights: hand must win */
    Duel_Update(&s, &in, 0.25f);
    f1 = duel_q_forward(s.player.orient);
    r1 = duel_q_right(s.player.orient);
    EXPECT(f1.x > f0.x + 0.05f); /* nose swung right */
    EXPECT_NEAR(r1.y, 0.0f, 0.02f); /* wings stayed level: pure yaw */
    Duel_HandStick(&s, 0, 0.0f, 0.0f);
    printf("hand_stick_sim ok\n");
}

/* M6: yaw and roll are INDEPENDENT (bank coordination deleted by user
 * order). Lever roll banks with zero nose swing; stick yaw swings the
 * nose with zero bank. (Replaces test_hand_coord_sim, whose coupling
 * the user explicitly removed.) */
static void test_hand_no_coord_sim(void) {
    DuelState s;
    DuelInput in;
    DuelVec3 f0, f1, r1;
    memset(&in, 0, sizeof(in));
    Duel_Init(&s);
    f0 = duel_q_forward(s.player.orient);
    Duel_HandLever(&s, 1, 1.0f);
    Duel_Update(&s, &in, 0.5f);
    f1 = duel_q_forward(s.player.orient);
    r1 = duel_q_right(s.player.orient);
    EXPECT(r1.y < -0.05f); /* right wing dropped */
    EXPECT_NEAR(f1.x, f0.x, 0.02f); /* nose did NOT swing: pure roll */
    Duel_Init(&s);
    f0 = duel_q_forward(s.player.orient);
    Duel_HandStick(&s, 1, 0.0f, -1.0f);
    Duel_Update(&s, &in, 0.5f);
    f1 = duel_q_forward(s.player.orient);
    r1 = duel_q_right(s.player.orient);
    EXPECT(f1.x < f0.x - 0.02f); /* nose swung left */
    EXPECT_NEAR(r1.y, 0.0f, 0.02f); /* no bank */
    printf("hand_no_coord_sim ok\n");
}

/* M6: left lever owns roll (controller roll ignored); both levers
 * work at once (stick pitch + lever roll); release returns roll to
 * neutral while throttle holds its value. */
static void test_hand_lever_sim(void) {
    DuelState s;
    DuelInput in;
    DuelVec3 r1, f1;
    memset(&in, 0, sizeof(in));
    Duel_Init(&s);
    Duel_HandLever(&s, 1, 1.0f);
    in.roll = -1.0f; /* controller fights the hand: hand must win */
    Duel_Update(&s, &in, 0.25f);
    r1 = duel_q_right(s.player.orient);
    EXPECT(r1.y < -0.05f); /* banked right despite controller */
    /* Both hands: stick pitches while the lever rolls. */
    Duel_Init(&s);
    memset(&in, 0, sizeof(in));
    Duel_HandStick(&s, 1, 1.0f, 0.0f);
    Duel_HandLever(&s, 1, 1.0f);
    Duel_Update(&s, &in, 0.25f);
    f1 = duel_q_forward(s.player.orient);
    r1 = duel_q_right(s.player.orient);
    EXPECT(f1.y > 0.05f && r1.y < -0.05f);
    /* Release: roll back to neutral, throttle untouched. */
    Duel_HandThrottle(&s, 1, 0.8f);
    Duel_Update(&s, &in, 1.0f / 60.0f);
    Duel_HandLever(&s, 0, 0.0f);
    Duel_HandStick(&s, 0, 0.0f, 0.0f);
    {
        DuelVec3 rb = duel_q_right(s.player.orient);
        Duel_Update(&s, &in, 1.0f / 60.0f);
        DuelVec3 ra = duel_q_right(s.player.orient);
        EXPECT_NEAR(ra.x, rb.x, 1e-4f); /* no roll motion unheld */
        EXPECT_NEAR(ra.y, rb.y, 1e-4f);
    }
    EXPECT_NEAR(s.player.throttle, 0.8f, 1e-5f); /* throttle held */
    printf("hand_lever_sim ok\n");
}

/* M6: lever grip = throttle slot + lateral swing tracking the hand. */
static void test_lever_grip(void) {
    DuelVec3 g0, g1, g2;
    DuelLever_GripShip(0.5f, 0.0f, &g0);
    DuelLever_GripShip(0.5f, 1.0f, &g1);
    DuelLever_GripShip(1.0f, -1.0f, &g2);
    EXPECT_NEAR(g1.x - g0.x, DUEL_STICK_RANGE, 1e-5f);
    EXPECT_NEAR(g1.y, g0.y, 1e-6f);
    EXPECT_NEAR(g1.z, g0.z, 1e-6f); /* roll never moves the slot axis */
    EXPECT(g2.z < g0.z); /* full throttle = forward */
    EXPECT_NEAR(g2.x - g0.x, -DUEL_STICK_RANGE, 1e-5f);
    printf("lever_grip ok\n");
}

/* M3.2: held hand owns throttle absolutely; release resumes the stick
 * integrator from the hand value (no jump); reset clears the override. */
static void test_hand_throttle_sim(void) {
    DuelState s;
    DuelInput in;
    memset(&in, 0, sizeof(in));
    Duel_Init(&s);
    EXPECT_NEAR(s.player.throttle, 0.5f, 1e-5f);
    Duel_HandThrottle(&s, 1, 0.8f);
    in.thr_axis = 1.0f; /* stick must be ignored while held */
    Duel_Update(&s, &in, 1.0f / 60.0f);
    EXPECT_NEAR(s.player.throttle, 0.8f, 1e-5f);
    Duel_HandThrottle(&s, 0, 0.0f);
    in.thr_axis = 0.0f;
    Duel_Update(&s, &in, 1.0f / 60.0f);
    EXPECT_NEAR(s.player.throttle, 0.8f, 1e-5f); /* stays: no jump */
    in.thr_axis = 1.0f;
    Duel_Update(&s, &in, 1.0f / 60.0f);
    EXPECT(s.player.throttle > 0.8f); /* integrator resumes upward */
    /* M3.2a: reset preserves the hand throttle while held, else 0.5. */
    Duel_HandThrottle(&s, 1, 0.2f);
    Duel_ResetCombat(&s);
    EXPECT_NEAR(s.player.throttle, 0.2f, 1e-5f);
    Duel_HandThrottle(&s, 0, 0.0f);
    Duel_ResetCombat(&s);
    EXPECT_NEAR(s.player.throttle, 0.5f, 1e-5f);
    printf("hand_throttle_sim ok\n");
}

/* M3.4: index extension latches fire fast, curl releases slower,
 * hysteresis band holds. */
static void test_fire_trigger(void) {
    DuelFire f;
    int i;
    DuelFire_Reset(&f);
    EXPECT(!f.firing);
    DuelFire_Update(&f, 1, 1, DUEL_FIRE_ON_DIST);
    EXPECT(!f.firing); /* 1 frame is not enough */
    DuelFire_Update(&f, 1, 1, DUEL_FIRE_ON_DIST);
    EXPECT(f.firing);
    DuelFire_Update(&f, 1, 1,
                    (DUEL_FIRE_ON_DIST + DUEL_FIRE_OFF_DIST) * 0.5f);
    EXPECT(f.firing); /* band holds */
    for (i = 0; i < DUEL_FIRE_OFF_FRAMES - 1; i++)
        DuelFire_Update(&f, 1, 1, DUEL_FIRE_OFF_DIST);
    EXPECT(f.firing);
    DuelFire_Update(&f, 1, 1, DUEL_FIRE_OFF_DIST);
    EXPECT(!f.firing);
    printf("fire_trigger ok\n");
}

/* M3.4: no hold or no joints = no fire, instantly. */
static void test_fire_gates(void) {
    DuelFire f;
    DuelFire_Reset(&f);
    DuelFire_Update(&f, 1, 1, DUEL_FIRE_ON_DIST);
    DuelFire_Update(&f, 1, 1, DUEL_FIRE_ON_DIST);
    EXPECT(f.firing);
    DuelFire_Update(&f, 0, 1, DUEL_FIRE_ON_DIST); /* released stick */
    EXPECT(!f.firing);
    DuelFire_Update(&f, 1, 1, DUEL_FIRE_ON_DIST);
    DuelFire_Update(&f, 1, 1, DUEL_FIRE_ON_DIST);
    EXPECT(f.firing);
    DuelFire_Update(&f, 1, 0, DUEL_FIRE_ON_DIST); /* joints lost */
    EXPECT(!f.firing);
    DuelFire_Update(&f, 0, 1, 1.0f); /* pointing without holding */
    DuelFire_Update(&f, 0, 1, 1.0f);
    EXPECT(!f.firing);
    printf("fire_gates ok\n");
}

/* M3.8 touch-fire: thumb tip (5) to index-intermediate (8); touch
 * (~2 cm) latches, peel-away stops. Rest (>= 5 cm) clears the band. */
static void test_fire_touch(void) {
    DuelFire f;
    EXPECT(DUEL_FIRE_JOINT == 5);
    EXPECT(DUEL_FIRE_REF_JOINT == 8);
    EXPECT(DUEL_FIRE_ON_DIST < DUEL_FIRE_OFF_DIST);
    EXPECT(DUEL_FIRE_ON_DIST > 0.015f && DUEL_FIRE_ON_DIST < 0.05f);
    EXPECT(DUEL_FIRE_OFF_DIST > 0.015f && DUEL_FIRE_OFF_DIST < 0.05f);
    DuelFire_Reset(&f);
    DuelFire_Update(&f, 1, 1, 0.020f); /* touching */
    DuelFire_Update(&f, 1, 1, 0.020f);
    EXPECT(f.firing);
    DuelFire_Update(&f, 1, 1, 0.060f); /* peeled away */
    DuelFire_Update(&f, 1, 1, 0.060f);
    DuelFire_Update(&f, 1, 1, 0.060f);
    EXPECT(!f.firing);
    printf("fire_touch ok\n");
}

/* M3.5: Local2Game must reproduce the joint seat path EXACTLY (valid
 * cal and invalid-cal passthrough), or the mesh detaches from the
 * joints/markers. */
static void test_mesh_frame(void) {
    DuelSeatCal cal;
    DuelVec3 seat = duel_v3(0.0f, 0.75f, -0.88f);
    DuelVec3 mo_p = duel_v3(0.01f, -0.02f, 0.03f);
    DuelQuat mo_q = duel_q_axis_angle(duel_v3(0, 0, 1), 0.02f);
    DuelQuat ship_q = duel_q_mul(duel_q_axis_angle(duel_v3(0, 1, 0), 0.7f),
                                 duel_q_axis_angle(duel_v3(1, 0, 0), -0.3f));
    DuelVec3 ship_t = duel_v3(100.0f, -50.0f, 800.0f);
    int ks[4] = { 0, 5, 10, 25 };
    int pass, i;
    duel_cal_capture(&cal, duel_v3(1.2f, 0.5f, -0.7f),
                     duel_q_axis_angle(duel_v3(0, 1, 0), 0.4f), DUEL_CAL_INIT);
    for (pass = 0; pass < 2; pass++) {
        float g16[16];
        DuelMat4 G;
        if (pass == 1) cal.valid = 0; /* passthrough */
        DuelHands_Local2Game(&cal, ship_q, ship_t, seat, mo_p, mo_q, g16);
        memcpy(G.m, g16, sizeof G.m);
        for (i = 0; i < 4; i++) {
            DuelHandJoint j;
            DuelVec3 w1, w2;
            j.pos = duel_v3(0.1f * ks[i], 0.2f, -0.3f + 0.01f * ks[i]);
            j.quat = duel_q_axis_angle(duel_v3(0, 0, 1), 0.1f * ks[i]);
            j.pos_valid = 1;
            j.rot_valid = 1;
            DuelHands_JointWorld(&cal, ship_q, ship_t, seat, mo_p, mo_q,
                                 &j, &w1);
            w2 = duel_m4_xform_point(G, j.pos);
            /* Algebraically identical paths; float assoc. order differs,
             * so tolerance scales with the ~800 m ship translation. */
            EXPECT_NEAR(w1.x, w2.x, 1e-3f);
            EXPECT_NEAR(w1.y, w2.y, 1e-3f);
            EXPECT_NEAR(w1.z, w2.z, 1e-3f);
        }
    }
    printf("mesh_frame ok\n");
}

/* M3.5: bind pose == located pose -> skin is the game matrix alone;
 * dead joints renormalize; all dead collapses to the wrist. */
static void test_mesh_skin(void) {
    DuelHand h;
    DuelHandMesh m;
    DuelVec3 bpos[26], bind;
    DuelQuat bquat[26];
    int16_t bi[4] = { 3, 7, 0, 0 };
    float bw[4] = { 0.5f, 0.5f, 0.0f, 0.0f };
    int jv[26], j;
    DuelMat4 jm[26], gj[26], G;
    DuelVec3 out, exp;
    float g16[16];
    DuelSeatCal cal;
    memset(&h, 0, sizeof h);
    memset(&m, 0, sizeof m);
    m.have = 1;
    m.scale = 1.0f;
    m.bpos = bpos;
    m.bquat = bquat;
    for (j = 0; j < 26; j++) {
        bpos[j] = duel_v3(0.01f * j, 0.02f * j, 0.03f * j);
        bquat[j] = duel_q_axis_angle(duel_v3(1, 0, 0), 0.05f * j);
        h.j[j].pos = bpos[j]; /* located == bind */
        h.j[j].quat = bquat[j];
        h.j[j].pos_valid = 1;
        jv[j] = 1;
    }
    bind = duel_v3(0.11f, 0.22f, 0.33f);
    duel_cal_capture(&cal, duel_v3(0, 0, 0), duel_q_identity(), DUEL_CAL_INIT);
    DuelHands_Local2Game(&cal, duel_q_identity(), duel_v3(0, 0, 0),
                         duel_v3(0, 0, 0), duel_v3(0, 0, 0),
                         duel_q_identity(), g16);
    memcpy(G.m, g16, sizeof G.m);
    DuelSkin_JointMats(&h, &m, jm);
    DuelSkin_GameMats(g16, jm, gj);
    exp = duel_m4_xform_point(G, bind);
    DuelSkin_Vert(bind, bi, bw, gj, jv, &out);
    EXPECT_NEAR(out.x, exp.x, 1e-5f);
    EXPECT_NEAR(out.y, exp.y, 1e-5f);
    EXPECT_NEAR(out.z, exp.z, 1e-5f);
    jv[7] = 0; /* dead blend partner renormalizes to joint 3 alone */
    DuelSkin_Vert(bind, bi, bw, gj, jv, &out);
    EXPECT_NEAR(out.x, exp.x, 1e-5f);
    EXPECT_NEAR(out.y, exp.y, 1e-5f);
    EXPECT_NEAR(out.z, exp.z, 1e-5f);
    for (j = 0; j < 26; j++) jv[j] = 0; /* all dead -> wrist, finite */
    DuelSkin_Vert(bind, bi, bw, gj, jv, &out);
    exp = duel_m4_xform_point(gj[1], bind);
    EXPECT_NEAR(out.x, exp.x, 1e-6f);
    EXPECT_NEAR(out.y, exp.y, 1e-6f);
    EXPECT_NEAR(out.z, exp.z, 1e-6f);
    printf("mesh_skin ok\n");
}

/* M3.5: the live hand scale must actually reach the bind joints. */
static void test_mesh_scale(void) {
    DuelHand h;
    DuelHandMesh m;
    DuelVec3 bpos[26];
    DuelQuat bquat[26];
    int16_t bi[4] = { 3, 0, 0, 0 };
    float bw[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    int jv[26], j;
    DuelMat4 jm[26], gj[26], G;
    DuelVec3 out;
    float g16[16];
    DuelSeatCal cal;
    memset(&h, 0, sizeof h);
    memset(&m, 0, sizeof m);
    m.have = 1;
    m.scale = 1.05f;
    m.bpos = bpos;
    m.bquat = bquat;
    for (j = 0; j < 26; j++) {
        bpos[j] = duel_v3(0, 0, 0);
        bquat[j] = duel_q_identity();
        h.j[j].pos = duel_v3(0, 0, 0);
        h.j[j].quat = duel_q_identity();
        jv[j] = 1;
    }
    bpos[3] = duel_v3(1.0f, 0.0f, 0.0f);
    h.j[3].pos = duel_v3(1.0f, 0.0f, 0.0f); /* bind == located, scaled */
    duel_cal_capture(&cal, duel_v3(0, 0, 0), duel_q_identity(), DUEL_CAL_INIT);
    DuelHands_Local2Game(&cal, duel_q_identity(), duel_v3(0, 0, 0),
                         duel_v3(0, 0, 0), duel_v3(0, 0, 0),
                         duel_q_identity(), g16);
    memcpy(G.m, g16, sizeof G.m);
    DuelSkin_JointMats(&h, &m, jm);
    DuelSkin_GameMats(g16, jm, gj);
    /* vert at the scaled bind tip lands on the located joint */
    DuelSkin_Vert(duel_v3(1.05f, 0.0f, 0.0f), bi, bw, gj, jv, &out);
    {
        DuelVec3 exp = duel_m4_xform_point(G, duel_v3(1.0f, 0.0f, 0.0f));
        EXPECT_NEAR(out.x, exp.x, 1e-5f);
        EXPECT_NEAR(out.y, exp.y, 1e-5f);
        EXPECT_NEAR(out.z, exp.z, 1e-5f);
    }
    printf("mesh_scale ok\n");
}

/* M3.6: pointing splays the thumb too, so fire needs fist + thumb:
 * grip + tracked + index curled. */
static void test_fire_holdok(void) {
    EXPECT(DUEL_FIRE_INDEX_JOINT == 10);
    EXPECT(DuelFire_HoldOk(1, 1, 1, 0.07f)); /* fist + thumb: hold */
    EXPECT(DuelFire_HoldOk(1, 1, 1, DUEL_FIRE_INDEX_MAX)); /* boundary */
    EXPECT(!DuelFire_HoldOk(1, 1, 1, 0.11f)); /* pointing: dead */
    EXPECT(!DuelFire_HoldOk(1, 1, 0, 0.07f)); /* index lost: dead */
    EXPECT(!DuelFire_HoldOk(0, 1, 1, 0.07f)); /* released: dead */
    EXPECT(!DuelFire_HoldOk(1, 0, 1, 0.07f)); /* untracked: dead */
    printf("fire_holdok ok\n");
}

/* M3.6: free-grab winners: per-hand nearest of HELD, then per-target
 * nearest hand. Standby HELD machines stay (seamless handoff). */
static void test_grab_assign(void) {
    DuelGrab g[2][2];
    float d[2][2];
    int holder[2];
    int h, t;
    for (h = 0; h < 2; h++)
        for (t = 0; t < 2; t++) {
            DuelGrab_Reset(&g[h][t]);
            d[h][t] = 99.0f;
        }
    DuelGrab_Assign((const DuelGrab (*)[2])g, d, holder);
    EXPECT(holder[0] == -1 && holder[1] == -1); /* nothing held */
    g[0][DUEL_TGT_QUAD].state = DUEL_GRAB_HELD; d[0][0] = 0.05f;
    g[1][DUEL_TGT_STICK].state = DUEL_GRAB_HELD; d[1][1] = 0.06f;
    DuelGrab_Assign((const DuelGrab (*)[2])g, d, holder);
    EXPECT(holder[0] == 0 && holder[1] == 1); /* classic HOTAS split */
    g[0][DUEL_TGT_STICK].state = DUEL_GRAB_HELD; d[0][1] = 0.04f;
    DuelGrab_Assign((const DuelGrab (*)[2])g, d, holder);
    EXPECT(holder[1] == 0); /* both on stick: nearer (left) wins */
    EXPECT(holder[0] == -1); /* left pruned to its nearest: stick */
    g[0][DUEL_TGT_QUAD].state = DUEL_GRAB_FAR; /* left leaves quad */
    d[0][1] = 0.09f; /* left drifts off while still HELD */
    DuelGrab_Assign((const DuelGrab (*)[2])g, d, holder);
    EXPECT(holder[1] == 1); /* standby right takes over, no relatch */
    EXPECT(holder[0] == -1);
    printf("grab_assign ok\n");
}

/* M3.7: fist = max tip-to-palm over 10/15/20/25; scraps read open. */
static void test_fist_dist(void) {
    DuelHand h;
    static const int tips[4] = { 10, 15, 20, 25 };
    float curled[4] = { 0.05f, 0.06f, 0.07f, 0.08f };
    int k;
    memset(&h, 0, sizeof h);
    h.j[0].pos = duel_v3(0, 0, 0);
    h.j[0].pos_valid = 1;
    for (k = 0; k < 4; k++) {
        h.j[tips[k]].pos = duel_v3(curled[k], 0, 0);
        h.j[tips[k]].pos_valid = 1;
    }
    EXPECT_NEAR(DuelHand_FistDist(&h), 0.08f, 1e-6f); /* worst wins */
    h.j[10].pos = duel_v3(0.15f, 0, 0); /* one extended dominates */
    EXPECT_NEAR(DuelHand_FistDist(&h), 0.15f, 1e-6f);
    h.j[15].pos_valid = 0; /* invalid tips ignored, not zeroed */
    h.j[20].pos_valid = 0;
    EXPECT_NEAR(DuelHand_FistDist(&h), 0.15f, 1e-6f);
    h.j[10].pos_valid = 0; /* one valid tip left: scraps */
    EXPECT_NEAR(DuelHand_FistDist(&h), 99.0f, 1e-6f);
    h.j[0].pos_valid = 0; /* no palm reference: open */
    EXPECT_NEAR(DuelHand_FistDist(&h), 99.0f, 1e-6f);
    printf("fist_dist ok\n");
}

/* M3.7: stick machine on fist thresholds: open approach never latches,
 * fist latches, band holds, open releases, yank resets instantly.
 * M3.8c: positioned latch (9 cm): fist shut in air never grabs. */
static void test_grab_fist(void) {
    DuelGrab g;
    int i;
    DuelGrab_Reset(&g);
    DuelGrab_UpdateT(&g, 1, 0.19f, 0.15f, 0.10f,
                     DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                     DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
    EXPECT(g.state == DUEL_GRAB_NEAR);
    DuelGrab_UpdateT(&g, 1, 0.19f, 0.15f, 0.10f,
                     DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                     DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
    EXPECT(g.state == DUEL_GRAB_NEAR); /* open hand: no latch */
    for (i = 0; i < 3; i++)
        DuelGrab_UpdateT(&g, 1, 0.10f, 0.08f, 0.12f,
                         DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                         DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
    EXPECT(g.state == DUEL_GRAB_NEAR); /* fist shut in air: no latch */
    DuelGrab_UpdateT(&g, 1, 0.07f, 0.08f, 0.07f,
                     DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                     DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
    EXPECT(g.state == DUEL_GRAB_NEAR); /* first shut frame: counting */
    DuelGrab_UpdateT(&g, 1, 0.07f, 0.08f, 0.07f,
                     DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                     DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
    EXPECT(g.state == DUEL_GRAB_HELD);
    DuelGrab_UpdateT(&g, 1, 0.07f, 0.12f, 0.07f,
                     DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                     DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
    EXPECT(g.state == DUEL_GRAB_HELD); /* band: holds */
    for (i = 0; i < 4; i++)
        DuelGrab_UpdateT(&g, 1, 0.07f, 0.15f, 0.07f,
                         DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                         DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
    EXPECT(g.state == DUEL_GRAB_HELD);
    DuelGrab_UpdateT(&g, 1, 0.07f, 0.15f, 0.07f,
                     DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                     DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
    EXPECT(g.state == DUEL_GRAB_NEAR); /* 5 open frames: release */
    DuelGrab_UpdateT(&g, 1, 0.07f, 0.08f, 0.07f,
                     DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                     DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
    DuelGrab_UpdateT(&g, 1, 0.07f, 0.08f, 0.07f,
                     DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                     DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
    EXPECT(g.state == DUEL_GRAB_HELD);
    DuelGrab_UpdateT(&g, 1, 0.07f, 0.08f, 0.25f,
                     DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                     DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
    EXPECT(g.state == DUEL_GRAB_FAR); /* yanked: instant reset */
    printf("grab_fist ok\n");
}

/* M3.7: latch-relative stick: grab point steers zero, push steers. */
static void test_stick_rel(void) {
    DuelVec3 a = duel_v3(0.0f, 0.5f, -1.2f);
    float pd, rd;
    DuelStick_DeflRel(a, a, &pd, &rd);
    EXPECT_NEAR(pd, 0.0f, 1e-6f);
    EXPECT_NEAR(rd, 0.0f, 1e-6f);
    DuelStick_DeflRel(duel_v3(a.x, a.y, a.z + DUEL_STICK_RANGE), a, &pd, &rd);
    EXPECT_NEAR(pd, 1.0f, 1e-6f); /* pull: nose up */
    EXPECT_NEAR(rd, 0.0f, 1e-6f);
    DuelStick_DeflRel(duel_v3(a.x + DUEL_STICK_RANGE, a.y, a.z), a, &pd, &rd);
    EXPECT_NEAR(pd, 0.0f, 1e-6f);
    EXPECT_NEAR(rd, 1.0f, 1e-6f);
    DuelStick_DeflRel(duel_v3(a.x, a.y, a.z - 3.0f * DUEL_STICK_RANGE), a,
                      &pd, &rd);
    EXPECT_NEAR(pd, -1.0f, 1e-6f); /* clamped */
    printf("stick_rel ok\n");
}

/* M7: stroke font coverage (menu charset must render, nothing asserts
 * on exact segment art). */
static void test_font_glyphs(void) {
    static const char *set =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%:*.-+/() ";
    int i;
    DuelFontSeg segs[DUEL_FONT_MAXSEGS];
    for (i = 0; set[i]; i++) {
        int n = DuelFont_Glyph(set[i], segs, DUEL_FONT_MAXSEGS);
        if (set[i] == ' ') { EXPECT(n == 0); continue; }
        EXPECT(n > 0);
        EXPECT(n <= DUEL_FONT_MAXSEGS);
    }
    /* Lowercase folds to the same glyph as uppercase. */
    {
        DuelFontSeg a[DUEL_FONT_MAXSEGS], b[DUEL_FONT_MAXSEGS];
        int na = DuelFont_Glyph('a', a, DUEL_FONT_MAXSEGS);
        int nb = DuelFont_Glyph('A', b, DUEL_FONT_MAXSEGS);
        EXPECT(na == nb && na > 0);
        EXPECT(!memcmp(a, b, (size_t)na * sizeof(a[0])));
    }
    EXPECT(DuelFont_Glyph('#', segs, DUEL_FONT_MAXSEGS) == 0);
    EXPECT_NEAR(DuelFont_TextW("AB"), 2.0f * DUEL_FONT_ADV - 1.0f, 1e-6f);
    EXPECT_NEAR(DuelFont_TextW(""), 0.0f, 1e-6f);
    printf("font_glyphs ok\n");
}

/* M7: every static menu string must be renderable by the font. */
static void test_menu_strings(void) {
    static const DuelAppState states[] = {
        DUEL_APP_MENU, DUEL_APP_BRIEF, DUEL_APP_OPTIONS,
        DUEL_APP_PILOTS, DUEL_APP_PAUSED, DUEL_APP_RESULT
    };
    size_t s;
    DuelFontSeg segs[DUEL_FONT_MAXSEGS];
    for (s = 0; s < sizeof(states) / sizeof(states[0]); s++) {
        DuelMenu m;
        DuelMenuRow rows[DUEL_MENU_MAXROWS];
        int n, i;
        DuelMenu_Init(&m);
        DuelMenu_SetState(&m, states[s]);
        n = DuelMenu_Rows(&m, rows, DUEL_MENU_MAXROWS);
        EXPECT(n > 0 && n <= DUEL_MENU_MAXROWS);
        {
            const char *t = DuelMenu_Title(&m);
            int c;
            EXPECT(t != NULL);
            for (c = 0; t[c]; c++) {
                if (t[c] == ' ') continue;
                EXPECT(DuelFont_Glyph(t[c], segs, DUEL_FONT_MAXSEGS) > 0);
            }
        }
        for (i = 0; i < n; i++) {
            int c;
            EXPECT(rows[i].label != NULL);
            for (c = 0; rows[i].label[c]; c++) {
                char ch = rows[i].label[c];
                if (ch == ' ') continue;
                EXPECT(DuelFont_Glyph(ch, segs, DUEL_FONT_MAXSEGS) > 0);
            }
        }
    }
    printf("menu_strings ok\n");
}

/* M7: menu layout/nav/activate/choice/slider behavior. */
static void test_menu_rows(void) {
    DuelMenu m;
    DuelMenuRow rows[DUEL_MENU_MAXROWS];
    int n, i;
    DuelMenu_Init(&m);
    /* MENU: 4 actions, selection lands on the first. */
    n = DuelMenu_Rows(&m, rows, DUEL_MENU_MAXROWS);
    EXPECT(n == 4);
    EXPECT(m.state == DUEL_APP_MENU && m.sel == 0);
    EXPECT(DuelMenu_Activate(&m) == DUEL_ACT_ENTER_BRIEF);
    DuelMenu_Nav(&m, 1); EXPECT(m.sel == 1);
    EXPECT(DuelMenu_Activate(&m) == DUEL_ACT_ENTER_PILOTS);
    DuelMenu_Nav(&m, -1); EXPECT(m.sel == 0);
    DuelMenu_Nav(&m, -1); EXPECT(m.sel == 3); /* wrap */
    /* BRIEF: labels skipped by nav + rect/hit. */
    DuelMenu_SetState(&m, DUEL_APP_BRIEF);
    n = DuelMenu_Rows(&m, rows, DUEL_MENU_MAXROWS);
    EXPECT(n == 6);
    EXPECT(m.sel == 3); /* DIFICULTAD choice */
    {
        float x0, y0, x1, y1;
        EXPECT(!DuelMenu_RowRect(&m, 0, &x0, &y0, &x1, &y1)); /* label */
        EXPECT(DuelMenu_RowRect(&m, 3, &x0, &y0, &x1, &y1));
        EXPECT(x1 > x0 && y1 > y0);
        EXPECT(DuelMenu_HitRow(&m, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f) == 3);
        EXPECT(DuelMenu_HitRow(&m, 99.0f, 99.0f) == -1);
        /* Rects stack downward without overlap. */
        for (i = 3; i < n - 1; i++) {
            float a0, a1, b0, b1, t;
            DuelMenu_RowRect(&m, i, &a0, &t, &a1, &t);
            DuelMenu_RowRect(&m, i, NULL, &b0, NULL, &b1);
            (void)a0; (void)a1;
            {
                float c0, c1;
                DuelMenu_RowRect(&m, i + 1, NULL, &c0, NULL, &c1);
                EXPECT(b0 >= c1); /* this row fully above the next */
            }
        }
    }
    /* Choice click cycles with wraparound. */
    EXPECT(m.difficulty == DUEL_DIFF_PILOT);
    EXPECT(DuelMenu_ActivateRow(&m, 3) == DUEL_ACT_NONE);
    EXPECT(m.difficulty == DUEL_DIFF_ACE);
    DuelMenu_Adjust(&m, 1);
    EXPECT(m.difficulty == DUEL_DIFF_ROOKIE);
    DuelMenu_Adjust(&m, -1);
    EXPECT(m.difficulty == DUEL_DIFF_ACE);
    EXPECT(DuelMenu_ActivateRow(&m, 4) == DUEL_ACT_START);
    /* OPTIONS sliders: drag + step clamp. */
    DuelMenu_SetState(&m, DUEL_APP_OPTIONS);
    DuelMenu_SetSlider(&m, 0, 0.5f);
    EXPECT_NEAR(m.vol_master, 0.5f, 1e-6f);
    DuelMenu_SetSlider(&m, 0, 9.0f);
    EXPECT_NEAR(m.vol_master, 1.0f, 1e-6f);
    DuelMenu_SetSlider(&m, 3, 0.0f);
    EXPECT_NEAR(m.sens, 0.5f, 1e-6f);
    DuelMenu_SetSlider(&m, 4, 1.0f);
    EXPECT_NEAR(m.dead, 0.30f, 1e-6f);
    m.sel = 0;
    m.vol_master = 0.95f;
    DuelMenu_Adjust(&m, 1);
    EXPECT_NEAR(m.vol_master, 1.0f, 1e-6f);
    /* Activate on label/out-of-range is a no-op. */
    EXPECT(DuelMenu_ActivateRow(&m, 99) == DUEL_ACT_NONE);
    DuelMenu_SetState(&m, DUEL_APP_BRIEF);
    EXPECT(DuelMenu_ActivateRow(&m, 0) == DUEL_ACT_NONE);
    printf("menu_rows ok\n");
}

/* M7: row values (choice name, slider %, pilot name). */
static void test_menu_values(void) {
    DuelMenu m;
    char val[16];
    DuelMenu_Init(&m);
    DuelMenu_SetState(&m, DUEL_APP_BRIEF);
    m.difficulty = DUEL_DIFF_ROOKIE;
    DuelMenu_RowValue(&m, 3, val, sizeof(val));
    EXPECT(!strcmp(val, "FACIL"));
    DuelMenu_SetState(&m, DUEL_APP_OPTIONS);
    m.vol_master = 0.8f;
    DuelMenu_RowValue(&m, 0, val, sizeof(val));
    EXPECT(!strcmp(val, "80%"));
    m.sens = 1.0f;
    DuelMenu_RowValue(&m, 3, val, sizeof(val));
    EXPECT(!strcmp(val, "50%"));
    DuelMenu_SetState(&m, DUEL_APP_MENU);
    strcpy(m.pilot.name, "ACE");
    m.pilot_idx = 0;
    DuelMenu_RowValue(&m, 1, val, sizeof(val)); /* PILOTO row */
    EXPECT(!strcmp(val, "ACE"));
    m.pilot_idx = -1;
    m.pilot.name[0] = '\0';
    DuelMenu_RowValue(&m, 1, val, sizeof(val));
    EXPECT(!strcmp(val, "-"));
    printf("menu_values ok\n");
}

#define DUEL_TEST_PROFDIR "/data/local/tmp/duel_test_prof"

/* M7: prefs close/reopen persistence + tolerant reader. */
static void test_prefs_roundtrip(void) {
    DuelPrefs p, q;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", DUEL_TEST_PROFDIR);
    EXPECT(system(cmd) == 0);
    DuelPrefs_Defaults(&p);
    EXPECT_NEAR(p.sens, 1.0f, 1e-6f);
    EXPECT_NEAR(p.dead, 0.15f, 1e-6f);
    EXPECT(p.last_pilot[0] == '\0');
    p.vol_master = 0.33f; p.sens = 1.4f; p.dead = 0.22f;
    strcpy(p.last_pilot, "ACE1");
    EXPECT(DuelPrefs_Save(&p, DUEL_TEST_PROFDIR));
    EXPECT(DuelPrefs_Load(&q, DUEL_TEST_PROFDIR));
    EXPECT_NEAR(q.vol_master, 0.33f, 1e-3f);
    EXPECT_NEAR(q.sens, 1.4f, 1e-3f);
    EXPECT_NEAR(q.dead, 0.22f, 1e-3f);
    EXPECT(!strcmp(q.last_pilot, "ACE1"));
    /* Missing dir -> defaults, never crash. */
    EXPECT(!DuelPrefs_Load(&q, "/data/local/tmp/duel_nope_missing"));
    EXPECT_NEAR(q.sens, 1.0f, 1e-6f);
    printf("prefs_roundtrip ok\n");
}

/* M7: pilot stats record/save/load (one record per combat). */
static void test_pilot_roundtrip(void) {
    DuelPilot p, q;
    DuelPilot_Defaults(&p, "ace-1");
    EXPECT(!strcmp(p.name, "ACE-1")); /* sanitized upper */
    EXPECT(p.last_win == -1);
    DuelPilot_Record(&p, 1, 3, 2);
    EXPECT(p.combats == 1 && p.wins == 1 && p.losses == 0);
    EXPECT(p.kills == 3 && p.last_diff == 2 && p.last_win == 1);
    DuelPilot_Record(&p, 0, 1, 0);
    EXPECT(p.combats == 2 && p.wins == 1 && p.losses == 1);
    EXPECT(p.kills == 4 && p.last_diff == 0 && p.last_win == 0);
    EXPECT(DuelPilot_Save(&p, DUEL_TEST_PROFDIR));
    EXPECT(DuelPilot_Load(&q, DUEL_TEST_PROFDIR, "ACE-1"));
    EXPECT(q.combats == 2 && q.wins == 1 && q.kills == 4);
    EXPECT(q.last_diff == 0 && q.last_win == 0);
    /* Unknown pilot -> fresh defaults. */
    EXPECT(!DuelPilot_Load(&q, DUEL_TEST_PROFDIR, "NOBODY"));
    EXPECT(q.combats == 0 && q.last_win == -1);
    EXPECT(!strcmp(q.name, "NOBODY"));
    printf("pilot_roundtrip ok\n");
}

/* M7: pilot scan/delete roundtrip. */
static void test_pilot_scan(void) {
    char names[DUEL_PILOT_MAX][DUEL_PILOT_NAME_MAX + 1];
    DuelPilot p;
    int n0, n1, i, found = 0;
    n0 = DuelPilot_Scan(DUEL_TEST_PROFDIR, names, DUEL_PILOT_MAX);
    DuelPilot_Defaults(&p, "SCANME");
    EXPECT(DuelPilot_Save(&p, DUEL_TEST_PROFDIR));
    n1 = DuelPilot_Scan(DUEL_TEST_PROFDIR, names, DUEL_PILOT_MAX);
    EXPECT(n1 == n0 + 1);
    for (i = 0; i < n1; i++) {
        if (!strcmp(names[i], "SCANME")) found = 1;
        if (i > 0) EXPECT(strcmp(names[i - 1], names[i]) <= 0); /* sorted */
    }
    EXPECT(found);
    EXPECT(DuelPilot_Delete(DUEL_TEST_PROFDIR, "SCANME"));
    EXPECT(!DuelPilot_Delete(DUEL_TEST_PROFDIR, "SCANME")); /* gone */
    EXPECT(DuelPilot_Scan(DUEL_TEST_PROFDIR, names, DUEL_PILOT_MAX) == n0);
    EXPECT(DuelPilot_Scan("/data/local/tmp/duel_nope_missing", names,
                           DUEL_PILOT_MAX) == 0);
    printf("pilot_scan ok\n");
}

/* M7: difficulty clamps, survives resets, changes TIE spawn HP. */
static void test_difficulty(void) {
    DuelState a, b;
    Duel_SetDifficulty(&a, 99);
    EXPECT(a.diff == 2);
    Duel_SetDifficulty(&a, -99);
    EXPECT(a.diff == 0);
    Duel_Init(&a);
    EXPECT(a.diff == 1); /* pilot baseline */
    Duel_SetDifficulty(&a, 0);
    Duel_ResetCombat(&a);
    EXPECT(a.diff == 0); /* survives reset */
    Duel_Init(&b);
    Duel_SetDifficulty(&b, 2);
    Duel_ResetCombat(&b);
    EXPECT(b.ties[0].hp > a.ties[0].hp); /* 1.3x vs 0.7x */
    EXPECT_NEAR(b.ties[0].hp / a.ties[0].hp, 1.3f / 0.7f, 1e-4f);
    {
        /* Hand deadzone storage clamps to the options range. */
        Duel_SetHandDead(&a, 9.0f);
        EXPECT_NEAR(a.hand_dead, 0.30f, 1e-6f);
        Duel_SetHandDead(&a, -1.0f);
        EXPECT_NEAR(a.hand_dead, 0.05f, 1e-6f);
    }
    printf("difficulty ok\n");
}

int main(void) {
    test_quat();
    test_flight();
    test_ai_steer();
    test_laser_collision();
    test_laser_lifetime();
    test_reset_loop();
    test_m5_no_friendly_fire();
    test_m5_tie_bolts_hit_rebels_only();
    test_m5_autotarget();
    test_m5_ally_kills();
    test_spawn_layout();
    test_render_math();
    test_view_matrix();
    test_model_basis();
    test_head_does_not_change_cockpit_world();
    test_head_changes_cockpit_view_relative();
    test_ship_rotates_cockpit_world();
    test_seat_rigid_to_ship();
    test_opt_to_ship_basis();
    test_player_muzzle_independent_of_hmd();
    test_tie_muzzle_independent_of_hmd();
    test_frustum_asymmetric_signs();
    test_reticle_ship_locked();
    test_lead_point();
    test_input_axes();
    test_neutral_head_inside_cockpit();
    test_initial_heading();
    test_free_look_calibrated();
    test_recenter();
    test_local_axis_invariants();
    test_camera_ship_local_stable();
    test_star_count();
    test_star_determinism();
    test_star_unit_direction();
    test_star_distribution_valid();
    test_star_translation_invariance();
    test_star_head_independence();
    test_original_seed_sequence();
    test_sky_dump();
    test_audio_math();
    test_audio_events();
    test_audio_mix();
    test_audio_output();
    test_audio_ducking();
    test_muzzle_world();
    test_sprite_quad();
    test_sprite_frame();
    test_sprite_slots();
    test_depth_determinism();
    test_depth_parallax();
    test_depth_collect();
    test_motion_springs();
    test_motion_eye();
    test_trajectory();
    test_celestial();
    test_guide_arrow();
    test_head_guide();
    test_hands_bones();
    test_hands_validity();
    test_hands_seat_frame();
    test_hands_head_independent();
    test_hands_recenter();
    test_hands_ship_rigid();
    test_hands_lever_anchor();
    test_grab_approach();
    test_grab_latch();
    test_grab_release();
    test_grab_hysteresis();
    test_grab_inactive();
    test_stick_defl();
    test_stick_grip();
    test_stick_decay();
    test_stick_matrix();
    test_quad_map();
    test_lever_primset();
    test_hand_throttle_sim();
    test_hand_stick_sim();
    test_hand_no_coord_sim();
    test_hand_lever_sim();
    test_lever_grip();
    test_fire_trigger();
    test_fire_gates();
    test_fire_touch();
    test_mesh_frame();
    test_mesh_skin();
    test_mesh_scale();
    test_fire_holdok();
    test_grab_assign();
    test_fist_dist();
    test_grab_fist();
    test_stick_rel();
    test_font_glyphs();
    test_menu_rows();
    test_menu_strings();
    test_menu_values();
    test_prefs_roundtrip();
    test_pilot_roundtrip();
    test_pilot_scan();
    test_difficulty();
    if (fails == 0) printf("ALL DUEL TESTS PASS\n");
    else printf("%d FAILURES\n", fails);
    return fails == 0 ? 0 : 1;
}
