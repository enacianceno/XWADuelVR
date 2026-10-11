# CHECKPOINT XWADuelVR M4h — stage-2 arms (IK sleeves)

Version **3.19-M4h-arms**, APK `XWADuelVR-M4h-ARMS-debug.apk`
(2250223 B, SHA256 `79334445928456876109A7DB670257B9E9988E2F6A18F473081130EC73CDF078`).
Installed on Quest 3487C10H9T0BKZ (`org.openxwa.xwaduellvr`,
dumpsys `versionName=3.19-M4h-arms`). Prior APKs preserved (M4g kept).

Stage 1 (torso/legs/boots, user-approved) unchanged in the visor: the
static bake is bit-identical where the arms cannot reach. New: the suit
sleeves solve two-bone IK every frame and follow the tracked wrists.

## What changed

- `src/duel_pilot.h/.c`: `DuelPilot_BuildArmRig` (finds the 8 chain
  nodes, hunches clavicles 15 deg mirror-signed, caches shoulder
  centers/rotations, bind twists, bone lengths), `DuelPilot_SolveArm`
  (pure elbow-circle + twist-preserving AimAt + stretch clamp 1.35),
  `DuelPilot_ApplyArmPose`, `IsShoulderJoint`/`IsFingerJoint`.
  Arm bones elongate 1.2x (the sculpt's 38.6 cm arms vs a full-scale
  cockpit); non-uniform parent scale is unskewed in the aim with a
  length correction (world lengths stay exact).
- `src/duel_hands.h/.c`: `DuelHands_Local2Ship` (Local2Game without
  ship motion; arms are seat-locked, ship cancels, wrist converts
  ship->model with a plain R_y(PI) flip).
- `src/duel_render.c`: suit partition dominant->ANY moving joint
  (1181 dynamic tris, one sleeve prim, suit texture borrowed never
  freed); finger weights zeroed + renormalized everywhere (glove owns
  fingers); per-frame `skin_arms` (WRIST j[1], PALM j[0] fallback,
  freeze on invalid/degenerate), persistent-tbuf upload, draw with the
  cockpit ship matrix.
- `tests/test_duel.c`: `test_pilot_arm` (rig pins from the python FK
  oracle, 4 FK-verified solve cases, determinism, degenerate freeze,
  bit-exact torso isolation, classifiers over the 52 real names) +
  `test_local2ship` (seat translation + ship-cancellation).
- `tests/test_render_rules.py`: M4 deferral rule updated to the ANY
  sleeve store + 5 new M4h rules (159 total).

## Verification (host + device, no visor claim)

- Render rules (host): ALL RENDER RULE TESTS PASS (159/159).
- Native: `Build-Target.ps1` clean (pilot/hands/render recompiled).
- Device CPU suite `/data/local/tmp/test_duel_m4h`: ALL DUEL TESTS
  PASS (incl. `pilot_arm ok`, `local2ship ok`).
- Install: `install -r` Success, dumpsys confirms 3.19-M4h-arms.
- Geometry (measured, python oracles): stick reach x=0.99 (no
  stretch), throttle x=0.56 (bent), menu x=1.15, far corner clamps at
  1.35 with a 2.7 cm deficit hidden inside the glove; cuff overlaps
  the wrist by 6 cm; static/dynamic seams sit at zero moving weight
  (bit-exact, no tears by construction).

## Visor test (PENDING user validation -- not claimed)

1. Launch XWADuelVR. Look down: sleeves now cover the shoulders down
   to the gloves (no more bare cut at the vest armholes).
2. Grab the stick (right) and throttle (left): sleeves follow, elbows
   bend out-down-back, cuffs stay tucked in the gloves.
3. Open the menu, touch options with both index fingers: sleeves
   stretch to follow; corners may extend near-full (no gaps allowed:
   report any visible cuff/wrist separation).
4. Shake hands out of tracking briefly (cover the headset cameras):
   sleeves must freeze in place, never teleport or vanish.
5. Report: sleeve length natural? elbow direction OK? any clip
   through vest/cockpit? any white/untextured patch on the arms?
