/* XWADuelVR M3.0: native OpenXR hand tracking (visual/tracking only).
 * XR_EXT_hand_tracking joints, located in the LOCAL base space (the same
 * space as the HMD views) and mapped to the game world through the EXISTING
 * seat calibration + ship transform -- the exact camera path
 * (duel_cal_neutralize + duel_eye_pose_motion). No new calibration, no sim
 * coupling, no gestures: M3.0 proves spatial correctness first.
 * SDL-free so the CPU test suite can link it (Xr calls go through runtime
 * function pointers; logging is the caller's job). */
#ifndef XWADUELVR_HANDS_H
#define XWADUELVR_HANDS_H

#include <openxr/openxr.h>

#include <stdint.h>

#include "duel_math.h"

#define DUEL_HAND_LEFT 0
#define DUEL_HAND_RIGHT 1
#define DUEL_HAND_JOINTS 26
#define DUEL_HAND_BONES 25

typedef struct DuelHandJoint {
    DuelVec3 pos; /* LOCAL space, meters (raw runtime pose) */
    DuelQuat quat;
    int pos_valid; /* POSITION_VALID_BIT at locate time */
    int rot_valid; /* ORIENTATION_VALID_BIT at locate time */
} DuelHandJoint;

typedef struct DuelHand {
    int tracked; /* tracker created for this side */
    int active;  /* runtime reports the hand currently tracked */
    int n_valid; /* joints with pos_valid (0 when hidden) */
    int last_result; /* last xrLocateHandJointsEXT XrResult (0 = success) */
    int locate_calls; /* total locate attempts this run */
    DuelHandJoint j[DUEL_HAND_JOINTS];
} DuelHand;

/* Loads the EXT functions (via getProc from DuelXr_GetProc) and creates
 * the left/right trackers. Returns a bitmask (1 = left, 2 = right);
 * 0 means hand tracking unavailable -- the game proceeds with
 * controllers. Safe to call once. */
int DuelHands_Init(PFN_xrGetInstanceProcAddr getProc, XrInstance instance,
                   XrSession session);
void DuelHands_Shutdown(void);
/* Locates both hands at the frame's predicted display time in baseSpace
 * (normally the LOCAL space from DuelXr_Space). Never crashes when
 * unsupported: hands simply report inactive. */
void DuelHands_Locate(XrSession session, XrSpace baseSpace, XrTime time);
const DuelHand *DuelHands_Get(int side); /* side: DUEL_HAND_LEFT/RIGHT */

/* Bone topology: bone i connects joints a..b (XR_HAND_JOINT_* indices).
 * 25 segments: wrist->palm, palm->5 metacarpals, then each finger chain. */
void DuelHands_Bone(int i, int *a, int *b);

/* Pure validity helpers (unit-testable, no XR calls). */
int DuelHands_PosValid(XrSpaceLocationFlags flags);
int DuelHands_RotValid(XrSpaceLocationFlags flags);

/* LOCAL joint -> game world through the shared seat/camera path. Reuses
 * the validated calibration: neutralize into the seat frame, then the
 * same ship*inertia*seat composition as the eye pose. Head pose never
 * enters except via the (event-driven, never per-frame) calibration. */
void DuelHands_JointWorld(const DuelSeatCal *cal,
                           DuelQuat ship_q, DuelVec3 ship_t, DuelVec3 seat_t,
                           DuelVec3 mo_p, DuelQuat mo_q,
                           const DuelHandJoint *j, DuelVec3 *out_p);

/* M3.0 lever audit, CORRECTED M3.1a by user HMD report ("marker floats
 * ahead of the real lever") + per-primitive audit (evidence/m31-lever):
 * mesh31 is the button PANEL face (z~1.52); the flight-stick COLUMN is
 * mesh41 prims 8/9/11/12 (x~0, z~1.27, y 0.0..0.55, up to 48 cm tall,
 * 3-12 cm wide). M3.1b: user HMD report ("almost: off the tip, onto the
 * grip") moved it 6 cm back + 3 cm down the handle: model
 * (0.00, 0.47, 1.21) -> ship frame via R_y(PI). ~33 cm ahead, ~28 cm
 * below the eyes. */
extern const DuelVec3 DUEL_LEVER_SHIP;

/* M3.1 lever grab test: proximity + deliberate pinch, per hand. Pure
 * state machine (unit-testable, no XR calls). Distances in meters.
 * No flight control: throttle is never touched (rule-pinned). */
typedef enum DuelGrabState {
    DUEL_GRAB_FAR = 0,  /* hand away from the lever */
    DUEL_GRAB_NEAR,     /* palm close: lever highlight */
    DUEL_GRAB_HELD      /* pinched near the grip: grabbed */
} DuelGrabState;

#define DUEL_GRAB_NEAR_R 0.20f   /* palm within: NEAR */
#define DUEL_GRAB_FAR_R 0.24f    /* palm beyond: back to FAR (hysteresis) */
#define DUEL_GRAB_GRAB_R 0.16f   /* pinch midpoint within: grabbable */
#define DUEL_GRAB_LOSE_R 0.20f   /* pinch midpoint beyond: release */
/* M3.8c: stick latches positioned-only. Markers are gone, and the 16 cm
 * sphere latched air ~10 cm off the column ("no agarra"). Palm wrapped
 * on the column reads 3-6 cm; 9 cm keeps that, rejects air. */
#define DUEL_GRAB_STICK_LATCH_R 0.09f
/* M3.2a: power-grip friendly. A fist around the stick holds thumb-index
 * ~4-6 cm apart; the old 3.5 cm precision-pinch gate made grabs hard. */
#define DUEL_GRAB_PINCH_SHUT 0.055f /* thumb-index tips: pinch closed */
#define DUEL_GRAB_PINCH_OPEN 0.085f /* ... opened (M3.8: stickier slide;
                                       latch stays deliberate) */
#define DUEL_GRAB_HOLD_FRAMES 2 /* consecutive shut frames to latch HELD */
#define DUEL_GRAB_REL_FRAMES 5  /* consecutive open frames to release */

typedef struct DuelGrab {
    DuelGrabState state;
    int hold_frames; /* shut counter toward HELD */
    int rel_frames;  /* open counter toward release */
} DuelGrab;

void DuelGrab_Reset(DuelGrab *g);
/* active: hand currently tracked. palm_dist: palm-to-grip (ship frame).
 * pinch_dist: thumb-tip to index-tip (any frame: distance is invariant).
 * mid_dist: pinch-midpoint-to-grip (ship frame). */
void DuelGrab_Update(DuelGrab *g, int active, float palm_dist,
                     float pinch_dist, float mid_dist);
/* Threshold-parameterized machine (pinch wrapper above keeps quad +
 * legacy tests). The stick uses fist distances (M3.7) + its own latch
 * radius (M3.8c). */
void DuelGrab_UpdateT(DuelGrab *g, int active, float palm_dist,
                      float grip_dist, float mid_dist,
                      float shut, float open, float grab_r, float lose_r);

/* M3.7 fist grip: max tip-to-palm over index/middle/ring/little tips
 * (10/15/20/25), LOCAL (frame-invariant). Thumb-independent, so firing
 * never breaks the hold. Shut band from M3.4: this user's curled index
 * reads <= 10 cm, pointed >= 11 cm. */
#define DUEL_FIST_SHUT 0.11f
#define DUEL_FIST_OPEN 0.14f
#define DUEL_FIST_MIN_VALID 2
/* <2 valid tips reads open (99): never latch on tracking scraps. */
float DuelHand_FistDist(const DuelHand *h);

/* M3.3 HOTAS split (user order; supersedes the M3.2 throttle-stick).
 * RIGHT hand: center stick (mesh41 column) steers pitch (inverted real:
 * pull back = nose up) + roll. LEFT hand: throttle quadrant on the left
 * console (mesh02p0 slot rail). Discipline: LEFT->throttle, RIGHT->stick.
 */
#define DUEL_LEVER_MESH 41
#define DUEL_LEVER_P0 8
#define DUEL_LEVER_P1 12
#define DUEL_LEVER_MAX_A 0.1745329f /* 10 deg pivot swing per axis */
#define DUEL_STICK_RANGE 0.07f      /* hand travel (m) for full deflection */
#define DUEL_STICK_TAU 0.06f        /* spring-return time constant (s) */
#define DUEL_QUAD_TRAVEL 0.08f      /* throttle slide half-travel (m) */
extern const DuelVec3 DUEL_LEVER_PIVOT_SHIP;  /* (0, 0.05, -1.27) */
extern const DuelVec3 DUEL_LEVER_PIVOT_MODEL; /* (0, 0.05, +1.27) */
extern const DuelVec3 DUEL_QUAD_SHIP; /* (-0.38, 0.46, -0.91) mid-slot */

int DuelLever_IsPrim(int mesh_idx, int prim_idx);
/* Stick deflection from a pinch-midpoint (ship frame) vs rest grip:
 * pd>0 pulled back (nose up), rd>0 pushed right (roll right). */
void DuelStick_Defl(DuelVec3 mid_ship, float *pd, float *rd);
/* M3.7 latch-relative steering: deflection from the grab anchor (where
 * the palm latched), not from an absolute grip point. Grabbing never
 * steers by itself; works for any grip, either hand. */
void DuelStick_DeflRel(DuelVec3 palm_ship, DuelVec3 anchor_ship,
                       float *pd, float *rd);
/* Moved stick grip in ship frame for a deflection (grab + marker). */
void DuelStick_GripShip(float pd, float rd, DuelVec3 *out);
/* Spring return when released (in place). */
void DuelStick_Decay(float *pd, float *rd, float dt);
/* OPT-frame 4x4 for the stick draw pass: T(Pm) Rx(-a) Rz(-b) T(-Pm),
 * the R_y(PI) conjugate of ship Rx(a)Rz(b) with a=+A*pd, b=-A*rd. */
void DuelStick_ModelOffset(float pd, float rd, float out16[16]);
/* Throttle quadrant: moved grip + hand map (linear, clamped). */
void DuelQuad_GripShip(float throttle, DuelVec3 *out);
float DuelQuad_ThrottleForMid(float mid_z);
/* M6 left lever: the quadrant handle gains a roll axis. Grip = slot
 * position (throttle) + lateral swing that tracks the hand 1:1, so
 * the latch point follows the hand while rolling (same idea as the
 * stick grip following pd/rd). */
void DuelLever_GripShip(float throttle, float roll, DuelVec3 *out);

/* M3.4 trigger: gripping hand fires by thumb gesture (gated on HELD
 * so idle hands stay tame). M3.5: INDEX replaced by THUMB (extending
 * the index breaks the fist grip). M3.6: + index-curled gate (pointing
 * splays the thumb too). M3.8 (user): thumb RESTING ON the index
 * fires continuously; thumb lifted/away = no fire. Metric is thumb
 * tip to index-intermediate (touch ~= 2 cm, rest >= 5 cm). */
#define DUEL_FIRE_JOINT 5          /* thumb tip */
#define DUEL_FIRE_REF_JOINT 8      /* index intermediate (M3.8: the pad
                                    * the thumb presses when laid across) */
#define DUEL_FIRE_ON_DIST 0.030f   /* touching: start counting */
#define DUEL_FIRE_OFF_DIST 0.042f  /* peeled off: stop (hysteresis) */
/* M3.6: pointing also splays the thumb, so the thumb band alone cannot
 * tell "fist + thumb up" (fire) from "point" (no fire). Require the
 * index curled: M3.4 validated fist-index < 10 cm, point > 11 cm. */
#define DUEL_FIRE_INDEX_JOINT 10
#define DUEL_FIRE_INDEX_MAX 0.10f  /* index tip-to-palm at/above: no fire */
#define DUEL_FIRE_ON_FRAMES 2      /* fast trigger pull */
#define DUEL_FIRE_OFF_FRAMES 3     /* slightly sticky release */

typedef struct DuelFire {
    int firing;
    int on_n;
    int off_n;
} DuelFire;

void DuelFire_Reset(DuelFire *f);
void DuelFire_Update(DuelFire *f, int held, int valid, float dist);
/* M3.6: gated hold for the fire machine: gripping + tracked + index
 * curled (fist). Pointing (index out) kills the hold instantly. */
int DuelFire_HoldOk(int grab_held, int active, int index_valid,
                    float index_ext);

/* M3.6 free grab: either hand may hold either target (user order).
 * Targets index the per-hand machine row + the winner holder. */
#define DUEL_TGT_QUAD 0   /* left throttle quadrant */
#define DUEL_TGT_STICK 1  /* center stick */
/* Winners per target (-1 = none): each hand keeps the nearest of its
 * HELD targets, then each target keeps its nearest hand. No resets:
 * a standby HELD machine takes over seamlessly on release (handoff). */
void DuelGrab_Assign(const DuelGrab g[2][2], const float dist[2][2],
                     int holder[2]);

/* M3.5 platform hand mesh (XR_FB_hand_tracking_mesh): the Quest bind
 * mesh skinned on CPU from the located EXT joints. Bind data is static
 * per user/device (fetched once); joint order matches XrHandJointEXT
 * (fetch requires exactly 26, else the diagnostic joints stay). */
#define DUEL_MESH_MAX_VERTS 8192
#define DUEL_MESH_MAX_IDX 32768

typedef struct DuelHandMesh {
    int have;          /* usable bind mesh */
    int n_verts, n_idx;
    DuelVec3 *pos;     /* bind positions */
    int16_t *bi;       /* n_verts*4 blend joint indices (EXT order) */
    float *bw;         /* n_verts*4 blend weights */
    DuelVec3 *bpos;    /* 26 bind joint positions (unscaled) */
    DuelQuat *bquat;   /* 26 bind joint orientations */
    int16_t *idx;      /* triangle indices (IBO source) */
    float scale;       /* hand scale (1.0 fallback) */
} DuelHandMesh;

/* Two-call xrGetHandMeshFB + copy of the kept arrays. 1 = usable. */
int DuelHands_FetchMesh(int side);
void DuelHands_FreeMesh(int side);
const DuelHandMesh *DuelHands_Mesh(int side);

/* LOCAL -> game-world rigid matrix (column-major 16 floats), EXACTLY the
 * seat path: ship * inertia * seat * neutralize. Mirrors invalid-cal
 * passthrough. Verified against DuelHands_JointWorld in tests. */
void DuelHands_Local2Game(const DuelSeatCal *cal,
                           DuelQuat ship_q, DuelVec3 ship_t, DuelVec3 seat_t,
                           DuelVec3 mo_p, DuelQuat mo_q, float out16[16]);
/* Per-joint bind->LOCAL skin matrices (scale applied). */
void DuelSkin_JointMats(const DuelHand *h, const DuelHandMesh *m,
                        DuelMat4 out[26]);
/* Fold the game matrix in: G * joint. */
void DuelSkin_GameMats(const float game16[16], const DuelMat4 jm[26],
                       DuelMat4 out[26]);
/* Blend one bind vert (renormalized over position-valid joints; all
 * invalid collapses to the wrist matrix so the mesh never explodes). */
void DuelSkin_Vert(DuelVec3 bind, const int16_t bi[4], const float bw[4],
                   const DuelMat4 gj[26], const int jvalid[26],
                   DuelVec3 *out);

#endif /* XWADUELVR_HANDS_H */
