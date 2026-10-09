# XWADuelVR M3 INTERACTIVE COCKPIT — M3.0 VIRTUAL HANDS

## Frozen M2.2d baseline
- APK: `XWADuelVR-M2.2d-COMBAT-debug.apk`, v2.4, 2196015 bytes,
  SHA-256 `583B3FB08F0AFE09869F8968177D57CFC80D8626438593EA536A49CC531582B9`.
- User verdict (Quest 3S, physical): rays improved near cannons — counted
  as SUCCESS. Game "finally feels like a real game".
- Frozen untouched: cockpit, seat, HMD cal, recenter, motion feel, camera
  hierarchy, M8 controls, body-local axes, flight model, TIE AI, weapons,
  collision, damage, reset, audio, stars/RealSpace, HDR planet, flare,
  guide, muzzles, OpenXR renderer, 72 fps target.
- Git: workspace has no commits yet (all files untracked); no destructive
  ops performed. Prior APKs preserved (P0 … M2.2d).

## OpenXR hand tracking audit
- Preferred route XR_EXT_hand_tracking: NOT previously enabled, but the
  in-use prebuilt `libSDL3.so` (XWAQuest m2, 18/09/2026) SUPPORTS user
  extension injection via `SDL_PROP_GPU_DEVICE_CREATE_XR_EXTENSION_COUNT_NUMBER`
  / `_NAMES_POINTER` (strings verified in the .so + `SDL_gpu_openxr.c`
  lines 113-137). No SDL rebuild needed.
- Headers (`SDL3/src/video/khronos/openxr/openxr.h`): extension v4,
  `XR_HAND_JOINT_COUNT_EXT = 26`, `PFN_xrCreateHandTrackerEXT` present.
- Manifest lacked hand permission → added
  `com.oculus.permission.HAND_TRACKING` (verified in APK via aapt).
- Runtime data still gated by the Quest OS hand-tracking toggle and the
  permission grant; both failure modes degrade to invisible hands.

## Extension support
- `duel_xr.c`: `xr_device_props(..., with_hands)` requests
  `XR_EXT_hand_tracking` at instance creation through SDL props.
- Insurance: if instance creation fails (runtime without the ext), retry
  with the exact pre-M3.0 props so boot never bricks; hands stay off.
- New accessor `DuelXr_GetProc()` exposes the loader entry for EXT
  function pointers (same pattern as existing `load_functions`).

## Hand architecture
- New module `src/duel_hands.h/.c`, SDL-free (links into the CPU suite).
  - `DuelHands_Init(getProc, instance, session)` → bitmask 1=L 2=R.
  - `DuelHands_Locate(session, baseSpace, time)` at the frame's predicted
    display time; per-joint POSITION/ORIENTATION validity; inactive or
    failed locates → hand hidden (no teleports, no stale geometry).
  - `DuelHands_Shutdown()`; `DuelHands_Get(side)`; bone topology table
    (25 segments); pure validity helpers; `DuelHands_JointWorld()`.
- `DuelRender` owns init (Create, after XR session up) + locate
  (`DuelRender_HandsLocate`, called from main once per frame after
  `DuelRender_Calibrate`, before the eye loop) + shutdown (Destroy).
- Input (`duel_input.c`, XR action sets) untouched: controllers remain
  authoritative; hands are VISUAL/TRACKING ONLY (no pinch/fire/steer).

## Coordinate hierarchy
- Joints located in LOCAL (same base space as HMD views).
- LOCAL → world reuses the validated camera path EXACTLY:
  `duel_cal_neutralize()` (seat frame) + `duel_eye_pose_motion()`
  (ship × inertia × seat). Zero new calibration.
- WORLD → SHIP → COCKPIT → INERTIA → SEAT → {HMD, LEFT, RIGHT}.
- Hands are siblings of HMD, never children: head turns move the camera
  only. Recenter coherence is automatic (cal regenerates on the same
  events). `DuelHands_JointWorld` takes NO HMD parameter (rule-pinned).

## Left hand / Right hand
- Both sides supported (independent trackers). L = cyan, R = amber,
  palms brighter, for M3.0 identification.
- Invalid tracking: joint skipped; bone skipped unless both endpoints
  valid. `isActive == 0` → whole hand hidden.

## Rendering method
- Diagnostic: joint spheres→points + bone segments→line pairs (no meshes).
- Joints: existing `pipe_points_add` world-VP range (`hand_range`,
  point_size 7), 1 draw/eye. Bones: NEW `pipe_lines_test` (alpha lines
  WITH depth test; `pipe_lines` has none — right for the HUD reticle,
  wrong for hands), 1 draw/eye.
- Depth-tested → cockpit/dashboard occlude hands coherently.
- Cost: ≤52 points + ≤50 segments, 2 draws/eye, zero per-frame allocs.
- A/B flag `DUEL_VIRTUAL_HANDS` (default 1; 0 = exact pre-M3 behavior).

## Tracking validity handling
- Render gate: `pos_valid` (POSITION_VALID_BIT). Tracked-without-valid
  never renders (stale pose). Tested in `test_hands_validity`.

## Controller coexistence
- Trackers are independent objects; action-set input never sees them.
- If the runtime exposes no hands (toggle off / permission denied /
  controllers-only mode), mask = 0 and the game is unchanged.

## Lever geometry audit (telemetry only)
- Live 49-mesh XWAUP cockpit, model frame (+Z nose), pilot eye ≈
  (0, 0.85, 0.78). "Directly in front" = +Z from eye.
- Best candidate: mesh31 `Mesh31_MainHull`
  x[-0.12,0.19] y[0.41,0.71] z[1.45,1.59] — narrow center box ~67 cm
  ahead, below eye line, many small textures (Tex70-88).
- Context: mesh41 (x±0.28 y[-0.03,0.75] z[0.55,1.78]) = console column.
- Grip estimate: model (0.03, 0.56, 1.52) → ship `DUEL_LEVER_SHIP =
  (-0.03, 0.56, -1.52)`: 54 cm ahead, 14 cm below eyes — consistent
  with a stick grip. Used ONLY for `DUEL_HANDS ... lev=` distances.
  HMD confirmation + interaction deferred to M3.1.

## Tests
- CPU suite on Quest (NDK arm64, `/data/local/tmp/test_duel_m30`):
  ALL DUEL TESTS PASS, oracle `SKY_DUMP hash AB5F8131` unchanged.
- 7 new: `hands_bones` (topology/leaves), `hands_validity` (flag
  logic), `hands_seat_frame` (neutral→seat anchor), `hands_head_-
  independent` (no per-frame HMD leak), `hands_recenter` (neutral
  recapture coherence), `hands_ship_rigid` (maneuver rigidity),
  `hands_lever_anchor` (ahead/below/centered, dist 0.56 m).
- Render rules (laptop): ALL RENDER RULE TESTS PASS (86 incl. 8 new
  M3.0 pins: ext+retry, seat path, no-HMD-param, no sim coupling,
  valid-only emission, depth-tested bones, once-per-frame locate,
  manifest permission).

## Performance
- Expected: +2 draws/eye, ≤152 verts/eye dynamic, no textures, no
  per-frame allocations. 72 fps target preserved by construction;
  device fps to be confirmed in the HMD test (`DUEL fps` line).

## APK
- `XWADuelVR-M3.0-HANDS-debug.apk`, versionName `2.5-M3.0-hands`,
  versionCode 1, package `org.openxwa.xwaduellvr`, arm64-v8a.
- Size: 2199691 bytes.
- SHA-256: `A3ABE8FDEBDCC77C88C960A940D9BCF289B921E7236CE6D0A694976B04D92A50`.
- aapt: OPENXR + OPENXR_SYSTEM + HAND_TRACKING permissions present.
- Changed: `src/duel_hands.h/.c` (new), `duel_xr.h/.c`, `duel_render.h/.c`,
  `duel_main.c`, `CMakeLists.txt`, `AndroidManifest.xml`, `app/build.gradle`,
  `tests/test_duel.c`, `tests/test_render_rules.py`.
- NOT installed (user installs).

## Quest validation R1 (2026-10-08): hands missing
- Installed M3.0 (v2.5) via adb; user opened app: combat normal, NO hands.
- Log: `DUEL_HANDS init: mask=3` (both trackers created), permission
  `HAND_TRACKING granted=true`, but `act=0 nv=0` on both sides for the
  whole run. Extension + trackers + permission OK; joints never active.
- Ambiguity: locate-failure vs runtime-inactive indistinguishable in R1
  telemetry → M3.0a instrumentation (below), one directed retest.

## M3.0a diagnostic delta
- `DuelHand` += `last_result` (XrResult per locate) + `locate_calls`.
- `DUEL_HANDS` line += `lr=`/`lc=` per side; new event line
  `DUEL_HANDS transition L/R %d->%d` on active flips.
- Rules += 2 pins (88 total, ALL PASS). C suite unchanged (still PASS).
- APK: `XWADuelVR-M3.0a-HANDSDIAG-debug.apk`, v2.5-M3.0a-handsdiag,
  2199879 bytes,
  SHA-256 `72F023F02D0AE650B1AC7F5AC4B410F40F302287EE63D29DF7713782047A56F0`.
- Possible causes ranked: (1) hands never in camera view during R1
  (user flew with controllers; zeros at end may be set-down, not
  hands-up); (2) runtime suppresses hands while controllers live;
  (3) locate misuse (result code will tell).

## Quest validation R2 (2026-10-08): still inactive, cause narrowed
- M3.0a installed (v2.5-M3.0a-handsdiag). User retested, hands up: no hands.
- Log: `lr=0 lc=N` counting every frame (locate SUCCEEDS @72Hz), but
  `act=0 nv=0` both sides, zero `transition` lines the whole run.
  → App code correct; runtime never reports hands.
- OS service (`IHandTrackingService`): Left/Right Enabled=yes, but
  `OutOfView` at dump time; `handTrackingOptIn=1`,
  `handTrackingAutoTransition=1` (auto-switch hands/controllers ON).
- Leading hypothesis: controllers stayed in hand (gripped hands are
  physically untrackable as bare-hand skeletons). R1/R2 likely
  procedural, not a code bug. Needs a live correlated run (below).

## Quest validation R3 (2026-10-08): hands-up moment captured, still act=0
- User held hands up in-app ("manos levantadas", "no vi nada").
- Log at 18:29: `lr=0 lc=12k+` (locate @72Hz OK), `act=0 nv=0` both.
- Minutes later (app alive pid 21213 but unfocused, no DUEL lines in
  last 500): OS service reports BOTH hands `Tracked` with live poses
  (17 ms fresh) → user's bare hands ARE trackable; OS sees them in
  home, app never saw them in runs. Consistent with gripped controllers
  during all in-app runs (never confirmed controllers-down).
- R4 must be decisive: in-app + focused + controllers ON TABLE +
  bare hands up, with simultaneous OS-service poll (Tracked vs
  OutOfView) + app log (act/transition). If OS=Tracked yet app act=0
  → app-side locate bug; if OS=OutOfView → procedure/position.

## M3.0b POV move (user order 2026-10-08)
- `DUEL_SEAT_POS`: (0, 0.70, -0.98) → (0, 0.75, -0.88): 10 cm back
  (+Z ship), 5 cm up (+Y). test_seat() + rule 13 + POV rule synced.
- Suite on device: ALL PASS (eye 0.75/0.88, lever dist 0.67 m).
- Rules: ALL PASS (88). Hands code identical to M3.0a (telemetry kept).
- APK: `XWADuelVR-M3.0b-POV-debug.apk`, v2.5-M3.0b-pov, 2199839 bytes,
  SHA-256 `368237C732072064624A8E5BCF4584B281F0C248C3A78E5828332B9CCA699BE4`.
  Installed via adb (standing order), verified on device.

## Quest validation R4 (2026-10-08): ROOT CAUSE FOUND (OS modality)
- In-app + hands up (18:47): app `lr=0 lc=1443 act=0 nv=0`; OS service
  simultaneously: `Left/Right Hand Enabled: no`, `Disabled`, clock 0ns.
- Minutes before at home: same service said Enabled=yes + `Tracked`
  with live poses. Controllers connected/awake in both cases.
- Decisive system line: `ControllerNotifications:
  mHandTrackingEnabled=true, mMultimodalityEnabled=false`.
- ROOT CAUSE: multimodality OFF → EXCLUSIVE modality. Controllers
  awake = controller modality = hand-tracking service hard-disabled
  → `isActive=0` forever, no matter what the app does. App code
  (extension/trackers/locate/render path) fully exonerated.
- No settings key or system property exposes the multimodal toggle
  (searched secure/system/global + getprop); it lives in Horizon prefs
  or the Settings UI. Gripped hands additionally untrackable physically.
- FIX PATH A (preferred): enable simultaneous hands+controllers in
  Quest Settings (Hands and controllers), if the OS version exposes it.
- FIX PATH B: controllers down + wait for controller sleep → modality
  auto-flips to hands (auto-transition=1). Then bare-hands M3.0 test.
- NOTE for M3.x: one-hand-on-stick + one-hand-free REQUIRES
  multimodality ON. Without it, grabbing the stick kills hand data.

## Quest validation R5 (2026-10-08): A/B via UI failed, flag found
- User tried UI options A (simultaneous toggle) and B (controller sleep):
  no hands. Verified: `mMultimodalityEnabled` still false (18:54),
  controllers still ConnectedBatteryNormal, app not running; OS hands
  Tracked at home (bare hands navigating).
- Neither A nor B actually took effect (toggle likely absent on this
  Horizon build; controllers never slept).
- Found device_config flag: `hzos_vendor_native/
  oculus_hybrid_hand_controller_transition` (was false; only hand/
  controller modality flag in the namespace) → set TRUE via adb
  (revert: `cmd device_config put hzos_vendor_native
  oculus_hybrid_hand_controller_transition false`). Get confirms true.
- Effect on `mMultimodalityEnabled` + in-app hands pending R6 test.

## Quest validation R6 (2026-10-08): hybrid flag no-op live
- With `oculus_hybrid_hand_controller_transition=true`: in-app still
  `act=0`, service still `Enabled: no / Disabled`. Flag reverted to
  false (OS pristine). It is not the live multimodal switch (reboot
  effect untested, not pursued).
- User confirms: controllers were ON THE TABLE during hands-up tests
  → gripped-controllers hypothesis DEAD.

## M3.0c manifest fix (root-cause candidate #2, doc-backed)
- Web consensus (SDL README-xr, Unity OpenXR, oxrsys, hz-vr-debug):
  Quest hand joints require BOTH the permission AND
  `<uses-feature android:name="oculus.software.handtracking">`;
  "if missing, the runtime can still operate, but headset-side hand
  joints will not be available" — exactly our symptom (trackers OK,
  locate OK, isActive=0, service Disabled while focused).
- Added: uses-feature (required=false) + meta-data
  `com.oculus.handtracking.version=V2.0` (controller-free launch).
  aapt confirms `uses-feature-not-required:
  name='oculus.software.handtracking'` in APK.
- Rules +1 pin (89 total, ALL PASS). Native code identical to M3.0b.
- APK: `XWADuelVR-M3.0c-HANDSFIX-debug.apk`, v2.5-M3.0c-handsfix,
  2199911 bytes,
  SHA-256 `4C7242C17D9336040C59FC7BF13A429DD5800517F70F7BF5B74861CBCE67D5A0`.
  Installed via adb (standing order), verified on device.

## Quest validation R7 (2026-10-08): HANDS TRACKING SUCCESS
- M3.0c in-app, controllers on table, bare hands up (19:39-19:40):
  `L act=1 nv=26 ... R act=1 nv=26`, palms moving across samples
  (user waving), lever distances live (0.31-0.59 m).
- OS service simultaneously: Enabled=yes, both `Tracked`, poses live.
- ROOT CAUSE CONFIRMED: missing `oculus.software.handtracking`
  uses-feature (+V2.0 meta) gated the runtime modality per-app.
  Extension/trackers/locate/render path all correct as diagnosed.
- Pending: user's VISUAL confirmation (cyan L + amber R in cockpit,
  alignment with real hands, head-turn/rigid/recenter behavior).

## Quest validation R8 (2026-10-08): VISUAL CONFIRMATION
- User: "ahora si se ven" — virtual hands VISIBLE in the cockpit.
- Combined with R7 (act=1 nv=26 both, moving, OS Tracked): M3.0
  VIRTUAL HANDS FOUNDATION **USER-VALIDATED**: native tracking live,
  joints+bones rendered in the calibrated pilot space, controllers as
  fallback untouched, 72 fps intact.
- Next: M3.1 lever grab test (HMD-confirmed alignment available).

## Final M3.0 state
- Installed on Quest: v2.5-M3.0c-handsfix (`4C7242C1…`), POV M3.0b
  included. Prior APKs preserved (M3.0, M3.0a kept on disk).
- Suite: ALL DUEL TESTS PASS on device. Rules: 89/89 PASS.
- OS left pristine: experimental device_config flag reverted to false.

# XWADuelVR M3.1 LEVER GRAB TEST

## Goal
Detect hand-near-lever + deliberate pinch; visual highlight +
grab/release state. NO flight control (throttle untouched).

## Grab state machine (`duel_hands`, pure/testable)
- States FAR → NEAR → HELD per hand. NEAR at palm ≤ 18 cm, back to
  FAR at ≥ 22 cm (hysteresis). HELD needs pinch shut (thumb tip 5 to
  index tip 10 ≤ 3.5 cm) + pinch midpoint ≤ 14 cm, 3 consecutive
  frames. Release: pinch open (≥ 4.5 cm) 5 frames, or midpoint yanked
  ≥ 18 cm (instant), or tracking lost (instant reset).
- Distances: palm/midpoint in ship frame (seat + neutralized LOCAL);
  pinch distance frame-invariant (LOCAL ok). Gated on valid seat cal.

## Visuals (additive, depth-tested)
- Lever grip marker: camera-facing diamond at ship-frame
  DUEL_LEVER_SHIP → world. FAR faint gray locator, NEAR green,
  HELD orange, growing 4.5 → 6 → 7 cm.
- Pinch tethers: line from each non-FAR hand's pinch midpoint to the
  grip. Both in hbone_range (0 new pipes/draws beyond M3.0).

## Telemetry
- `DUEL_GRAB transition L/R %d->%d` on flips; `DUEL_GRAB L st=%d
  R st=%d` @1Hz while any hand engaged. DUEL_HANDS unchanged.

## Tests
- 5 new C tests on device: approach, latch (+debounce, +far-pinch
  rejection), release (+debounce, +yank), hysteresis, inactive-reset.
  ALL DUEL TESTS PASS. Rules +3 (92 total): tips 5&10, no-throttle in
  hands module, marker at audit point. ALL RENDER RULE TESTS PASS.

## APK
- `XWADuelVR-M3.1-GRAB-debug.apk`, v2.6-M3.1-grab, 2201531 bytes,
  SHA-256 `D2A96CE464448BEB233E0D8534C66FC23F7E4346121CC5C3E525F55BBD533040`.
  Installed via adb (standing order), verified on device.

## Quest validation M3.1 (2026-10-08): marker floats ahead of lever
- User: diamond grip appears BUT misplaced; the real lever is further
  back (toward pilot). Procedure correct, anchor wrong.
- Per-primitive audit (`evidence/m31-lever/lever_hunt.py`, indexed):
  mesh31 = button panel face (z~1.52); the flight-stick COLUMN is
  mesh41 prims 8/9/11/12 (x~0, z~1.26-1.28, y 0.0..0.55, 3-12 cm
  wide, up to 48 cm tall) — 25 cm behind the old marker, exactly
  matching the user's report.

## M3.1a lever fix
- `DUEL_LEVER_SHIP`: (-0.03, 0.56, -1.52) → (0.0, 0.50, -1.27)
  (column top minus finger-wrap; 39 cm ahead, 25 cm below eyes).
- Suite: ALL PASS (lever dist 0.46 m). Rules: ALL PASS (92).
- APK: `XWADuelVR-M3.1a-LEVERFIX-debug.apk`, v2.6-M3.1a-leverfix,
  2201531 bytes,
  SHA-256 `64FB8FBA3CA104083A4A3338F9991496CE92E0DF3E3BE2E44E92EDAA23E97B8D`.
  Installed via adb (standing order), verified on device.

## Quest validation M3.1a (2026-10-08): almost, sits on the tip
- User: "casi, muévela un poco más atrás, que no esté en la punta,
  que esté en donde se agarra la palanca."

## M3.1b grip position
- `DUEL_LEVER_SHIP`: (0.0, 0.50, -1.27) → (0.0, 0.47, -1.21):
  6 cm back (+Z ship) + 3 cm down the mesh41 handle (model
  (0, 0.47, 1.21), inside the column volume, below the 0.545 top).
- Suite: ALL PASS (lever dist 0.43 m). Rules: ALL PASS (92).
- APK: `XWADuelVR-M3.1b-GRIPPOS-debug.apk`, v2.6-M3.1b-grippos,
  2201539 bytes,
  SHA-256 `CA0BB7CC687CB185BA27109114D95EC8ACA322960B5074FC9D8DF6C058A6B1F3`.
  Installed via adb (standing order), verified on device.

## Quest validation M3.1b (2026-10-08): EXCELLENT (user)
- User: "listo M3.1b quedó excelente". Grab machine + marker validated.

# XWADuelVR M3.2 PHYSICAL THROTTLE

## Goal
Move the cockpit lever with the hand; lever travel maps to throttle
0..1. Controller stick remains as fallback (integrates when released).

## Lever motion (render)
- Stick column = mesh41 prims 8..12 (baked `prim_idx` added, additive).
- `draw_model` refactored: NULL matrix = legacy single pass (TIE);
  cockpit always two passes (rest + lever prims with pivot offset).
- Offset = OPT-frame conjugate T(Pm)·Rx(−a)·T(−Pm), pivot model
  (0, 0.05, 1.27), a = ±10° from throttle (0 back / 1 fwd).
- The lever ALWAYS mirrors sim throttle (hand- or stick-driven): a
  physical throttle gauge. Marker + grab distances track the moved grip.

## Arbitration (sim)
- `DuelState.hand_thr_active/value` (memset-cleared on reset) via
  `Duel_HandThrottle`, published by main from the previous frame's
  grab (first HELD side, left first) + stored pinch midpoint Z.
- Held → absolute hand value (stick ignored). Released → M8 stick
  integrator resumes from the hand value: no jump by construction.
- Grab resets outside combat; lever snaps to 0.5 on combat reset.

## Telemetry
- `DUEL_GRAB ... thr=%.2f src=H/S` @1Hz while engaged.

## Tests
- 6 new C tests on device: angle map, grip swing (travel 0.146 m),
  hand map + roundtrip (arc/chord < 0.03), matrix conjugation
  (pivot fixed, grip matches), prim set, sim arbitration + no-jump +
  reset. ALL DUEL TESTS PASS.
- Rules: M3.1 no-throttle pin refined to "never writes sim state"
  (map math legitimately lives in the module); +3 M3.2 pins
  (two-pass, TIE legacy, arbitration). ALL RENDER RULE TESTS PASS.

## APK
- `XWADuelVR-M3.2-THROTTLE-debug.apk`, v2.7-M3.2-throttle, 2203627 bytes,
  SHA-256 `33FC50CFDF030AC0B17C85E271553345DE6F2A063824DA42E083EA25FE966405`.
  Installed via adb (standing order), verified on device.

## Quest validation M3.2 (2026-10-08): works, 3 issues (user)
- "fue dificil de agarrar" → pinch gate was precision-only (3.5 cm);
  a fist around the stick holds tips ~4-6 cm apart. Fix below.
- "despues de ser destruido se pierde el agarre" → grab was gated to
  combat phase + lever snapped to 0.5 on reset, teleporting away from
  a still-holding hand. Fix below.
- "solo funcionaba hacia adelante y hacia atras, izquierda y derecha
  no se movio nunca" → BY DESIGN: throttle is 1-DOF (fore-aft pivot).
  Lateral stick steering (roll/pitch) is NOT implemented; proposed as
  M3.3 scope, pending user order. LOSE_R widened so lateral wiggle
  while dragging does not drop the hold.

## M3.2a grab tune + death continuity
- Radii: NEAR 18→20, FAR 22→24, GRAB 14→16, LOSE 18→20 cm.
- Pinch: SHUT 3.5→5.5, OPEN 4.5→7.0 cm (power-grip friendly).
- Latch: HOLD 3→2 frames (snappier); release stays 5.
- Render: phase gate REMOVED (grab persists through death if held).
- Sim: `Duel_ResetCombat` preserves player throttle when
  `hand_thr_active` (published pre-update), else classic 0.5.
- Tests: grab suite green on new constants (symbolic); sim test now
  covers preserve-while-held + 0.5-when-free. ALL DUEL TESTS PASS.
- Rules: arbitration pin extended with reset preservation. ALL PASS.
- APK: `XWADuelVR-M3.2a-GRABTUNE-debug.apk`, v2.7-M3.2a-grabtune,
  2203707 bytes,
  SHA-256 `46E723E55BACEB130C0070F03EB57AEE41D551A2FDF4C89552B5ABFADEC3D62D`.
  Installed via adb (standing order), verified on device.

## Quest validation M3.2a (2026-10-08): VALIDATED (user)
- "quedo muy bien, agarrar con puño, ya no se pierde al morir."
- New user proposal (approved as M3.3): stick steers the ship
  (left/right + inverted up/down, real feel); throttle moves to a
  separate left-hand quadrant.

# XWADuelVR M3.3 HOTAS (stick + quadrant)

## Design (user order)
- RIGHT hand: center stick → pitch (inverted-real: pull back = nose
  up, push fwd = nose down) + roll (push right = roll right).
- LEFT hand: throttle quadrant on the left console → throttle 0..1.
- Discipline: LEFT→throttle, RIGHT→stick (nearest-target rejected).
- Yaw: bank-coordinated (yaw += 0.35·roll) + live stick yaw; no
  twist/pedals (decision noted, adjustable).

## Stick (replaces M3.2 throttle-stick)
- Deflection from right mid vs REST grip, ±7 cm → ±1 per axis.
- Column prims rotate 2-axis (Rx·Rz ship, conjugated to OPT frame);
  spring-decays to center on release (τ=0.06 s, smooth sim+visual).
- Grab distances/marker track the moved grip. Death: holds persist
  (consistent with M3.2a); commands freeze while dead (early-return).

## Quadrant (new)
- Audit (`quad_hunt.py`): mesh02p0 = 32 cm fore-aft slot rail on the
  left console (model x≈0.38) → anchor ship (-0.38, 0.46, -0.91).
- Synthetic solid handle box (5×7×10 cm, two-tone, depth-tested new
  `pipe_tri_test`) slides ±8 cm with sim throttle; gauge w/o hands.
- LEFT HELD owns throttle absolutely; stick integrates on release;
  death-preserve logic unchanged (now left-driven).

## Sim
- `hand_stk_active/hand_pitch/hand_roll` + `Duel_HandStick`; override
  + `DUEL_COORD_YAW` in `duel_update_player`. M8 sticks untouched.

## Telemetry
- `DUEL_GRAB ... thr=%.2f tsrc=H/S defl=(pd,rd) ssrc=H/S` @1Hz.

## Tests
- 8 C tests on device (defl signs, grip 2-axis, decay, matrix
  conjugation @6 corners, quad map, stick sim nose-up/roll-right vs
  fighting stick, coord yaw). ALL DUEL TESTS PASS.
- Rules rewritten to HOTAS (per-side markers, deflection pass,
  left-throttle, right-pitch/roll+coord, quad box). ALL PASS.

## APK
- `XWADuelVR-M3.3-HOTAS-debug.apk`, v2.8-M3.3-hotas, 2205283 bytes,
  SHA-256 `6815F6AA59C0410CDEEA4C95E1497D38E5B0BE9887D3F1615BAE9D02FD8B6C47`.
  Installed via adb (standing order), verified on device.

## Quest validation M3.3 (2026-10-08): COMPLETE (user)
- User: "excelente! M3.3 completado". HOTAS validated: stick steering
  (inverted pitch + roll, coordinated yaw, spring return) + left
  quadrant throttle + controller fallback.

# XWADuelVR M3.4 INDEX TRIGGER

## Gap (user-confirmed)
- With HOTAS hands busy (stick + quadrant), controllers on the table,
  there was NO way to fire. Fix: right index extension while gripping.

## Design
- Extension = index-tip(10) to palm(0) distance, LOCAL (invariant).
- Gate: right HELD + both joints valid. Fire on >= 11 cm (2 frames),
  stop on <= 10 cm (3 frames), hysteresis band holds. Release stick
  or lose joints = trigger dead instantly.
- Main ORs `DuelRender_HandFire(render)` into `in.fire` pre-update;
  sim untouched (cooldown/ownership/muzzles all existing).
- Feedback: lasers + audio + right marker turns RED while firing.
- Telemetry: `DUEL_FIRE R on/off (ext=...)` on transitions.

## Tests
- 2 C tests on device (latch/release/hysteresis; hold+joint gates).
  ALL DUEL TESTS PASS. Rules +2 (HELD+joint gating, main OR + sim
  untouched). ALL RENDER RULE TESTS PASS.

## APK
- `XWADuelVR-M3.4-FIRE-debug.apk`, v2.9-M3.4-fire, 2205795 bytes,
  SHA-256 `7C2B12A64B7982E29D7E69D9B20D046F992C54D9F902F80AC3122883CDB69DA1`.
  Installed via adb (standing order), verified on device.

## Quest validation M3.4 (pending, user)
1. Grip stick (ORANGE) with a fist → NO fire (curled index).
2. Extend right index (point) → RED marker + lasers + laser audio.
3. Curl index → fire stops. Release stick → trigger dead.
4. Point WITHOUT gripping → nothing (gate works).
5. Controller trigger still fires (fallback intact).
1. Install M3.0 APK. 2. Launch → combat as usual (controllers work).
3. Put controllers down (Quest hand tracking ON in OS settings).
4. Raise hands → cyan (L) + amber (R) joint/line hands in cockpit.
5. Move hands/fingers → virtual follows. 6. Approach dashboard/lever →
   alignment judged visually. 7. Turn head → hands stay (no head glue).
8. Fly/rotate → hands rigid with cockpit. 9. Recenter → still aligned.
10. Grab controllers → combat unchanged. 11. `DUEL_HANDS` log @0.5 Hz:
    act/nv/palm/ship/lev. 12. fps ≈ 72.
- PASS = "mis manos están dentro del X-Wing".

## Remaining issues
- OS hand-tracking toggle state on the user's Quest unknown until test.
- Controller/hand simultaneous-report behavior unverified on this runtime
  (fallback covers either way).
- Lever anchor is geometry-derived only; needs HMD confirmation (M3.1).
- Point size 7 / line width are first-guess diagnostics.

# XWADuelVR M3.5 PLATFORM HANDS + THUMB TRIGGER

## Gap (user-confirmed)
- M3.4 index fire works but extending the index breaks the closed-fist
  grip on the stick. User: fire with the RIGHT THUMB (lifts
  independently) + replace diagnostic joints with the OpenXR/Quest
  platform hands.

## Design
- Trigger digit: thumb tip (joint 5), band ON 9.5 cm / OFF 8.5 cm
  (above a wrapped fist ~6 cm). Same HELD+valid gating, same
  2-on/3-off frames, same main-OR; sim untouched.
- Platform mesh: `XR_FB_hand_tracking_mesh` requested at instance
  creation (retry ladder both → EXT-only → baseline, boot never
  bricks). `xrGetHandMeshFB` two-call fetch per tracker; bind mesh
  (poses, blend idx/w, indices) copied once; radii/normals/UVs
  dropped. Requires exactly 26 bind joints, else joints stay.
- Scale: `XrHandTrackingScaleFB` chained in every locate (ignored by
  runtimes that lack it); applied to bind joints before skinning.
- Skin: CPU per eye, `SkinLocal = W * inv(B)` (scaled bind), folded with
  `Local2Game` (ship·inertia·seat·neutralize — matrix form of the
  exact joint seat path). Blend renormalized over position-valid
  joints; total loss collapses to the wrist (never explodes).
- Render: dynamic VBO (9-float unlit layout) + static combined IBO
  (16-bit), opaque `pipe_tri_test`, one indexed draw per ACTIVE hand
  (lost hand vanishes, never freezes); `dyn_vbo` rebound after.
- Fallback: per hand — fetch/buffer failure keeps diagnostic joints;
  `DUEL_HAND_MESH 0` forces joints for A/B.
- Telemetry: `DUEL_HANDS mesh: L=%d R=%d` with counts at boot.

## Tests
- 4 C tests on device: thumb band, Local2Game==JointWorld (valid cal
  + passthrough), skin identity/renormalize/wrist-collapse, scale
  application. ALL DUEL TESTS PASS (`/data/local/tmp/test_duel_m35`).
  Rules +2 (FB ext + ladder, mesh-owns-joints + indexed draws).
  ALL RENDER RULE TESTS PASS.
- Build note: native .so comes from `Build-Target.ps1` BEFORE
  gradle; a gradle-only rebuild ships a STALE .so (caught via
  strings check this session).

## APK
- `XWADuelVR-M3.5-HANDSMESH-debug.apk`, v3.0-M3.5-handsmesh,
  2374425 bytes (entries +6 KB vs M3.4; +163 KB file overhead is
  zero alignment padding, verified),
  SHA-256 `CE60241798B0913FFFA8019D9E96D073C98D2CB9125ACC59C75C54615A8305C0`.
  Installed via adb (standing order). Boot smoke: TBD via logcat.

## Quest validation M3.5 (user, 2026-10-08)
- LEFT platform mesh renders, tracks well — user likes it, confirms
  OpenXR hands work. RIGHT hand INVISIBLE (no mesh, no joints).
- Index-point still fires; thumb behavior unverified.
- Boot log (captured later): `mask=3`, `mesh: L=1(1360/6942)
  R=1(1360/6942)` — both fetches fine, so the right hand died in
  RENDER (double index offset, see M3.6), not fetch.
- Root cause trigger: pointing splays the thumb past 9.5 cm too, so
  the thumb band alone cannot separate point from fist+thumb.
- User order: fix right hand, thumb fire, EITHER hand grabs EITHER
  control (left could only hold the quadrant).

## Remaining issues
- Mesh shading is flat unlit (no normals/lighting yet); looks solid
  but not lit like the cockpit — future pass if it reads wrong.
- Thumb band (8.5/9.5 cm) is ergonomic first guess; tune from HMD.
- Simultaneous controller/hand behavior still runtime-dependent.

# XWADuelVR M3.6 FREE GRAB + THUMB THAT MEANS THUMB

## Gap (user-confirmed M3.5 results)
1. Right platform hand invisible (fetch OK: 1360/6942 both hands).
2. Index-point still fires (thumb also splays when pointing).
3. Hands locked to targets (left=quadrant only); user wants either
   hand on either control.

## Root causes
1. IBO pre-offset hand-1 indices by +nv0 AND the draw passed
   `vertex_offset=nv0` → verts read at +2*nv0, out of the VBO.
2. Thumb-to-palm band cannot tell point from fist+thumb-up.
3. One grab machine per hand, hardwired to one target.

## Design
- IBO stores RAW runtime indices (`memcpy`); per-draw
  `vertex_offset` carries the hand base. Rule-pinned (negative pin
  on the old `+ vo`).
- `DuelFire_HoldOk`: grip + tracked + index curled (<= 10 cm, the
  M3.4-validated fist/point split). Pointing kills the hold
  instantly; fist+thumb-up fires. Right hand must hold the STICK.
- Free grab: `grab[2][2]` (hand × target) + `DuelGrab_Assign`:
  per-hand nearest of HELD, then per-target nearest hand. Standby
  HELD machines stay → seamless handoff on release, no relatch.
- Stick defl / throttle / markers / tethers follow
  `grab_holder[]`; markers are per TARGET now (tether from the
  holder's stored mid). Telemetry: `DUEL_GRAB holders Q=%d S=%d`.
- Sim untouched (getters only change WHICH hand feeds them).

## Tests
- 2 C tests on device (hold matrix incl. boundary; assign incl.
  prune + handoff). ALL DUEL TESTS PASS
  (`/data/local/tmp/test_duel_m36`). Rules +3, 2 updated (holder
  telemetry, holder-owned throttle). ALL RENDER RULE TESTS PASS.

## APK
- `XWADuelVR-M3.6-FREEGRAB-debug.apk`, v3.1-M3.6-freegrab,
  2212191 bytes,
  SHA-256 `F01FB05FD78AFC9C84A1023C509BCAF006B3D7454CAFBC7E47181C0B08064EA3`.
  Installed via adb (standing order). Boot log clean, no FATAL.

## Quest validation M3.6 (user, 2026-10-08)
- "bien!" Both meshes visible, free grab works.
- BUT lifting the thumb RELEASES the stick (pinch opens) — fire
  unusable. Sometimes no fire when lowering the thumb (grip lost).
- Request: fire with the LEFT thumb too when left holds the stick.
- Root cause: pinch-based hold is incompatible with thumb fire;
  the stick needs a thumb-independent fist grip.

## Remaining issues
- Thumb band (8.5/9.5 cm) still first-guess ergonomics.
- Fire needs right-on-stick; right-on-quadrant + thumb does nothing
  (by design, M3.4 semantics preserved).

# XWADuelVR M3.7 FIST GRIP + EITHER THUMB FIRES

## Gap (user-confirmed M3.6 results)
1. Stick releases when the thumb lifts (pinch opens) → fire breaks.
2. No fire when the thumb lowers (grip already lost).
3. Fire should work from the LEFT thumb when left holds the stick.

## Design
- Stick hold = FIST (`DuelHand_FistDist`: max tip-to-palm over
  10/15/20/25, <2 valid reads open), shut 10 cm / open 12 cm
  (M3.4-validated fist/point split). Thumb moves freely.
- Machine parameterized (`DuelGrab_UpdateT`); pinch wrapper keeps
  the quadrant + all legacy grab tests green.
- Stick proximity/latch on the PALM point (thumb-independent);
  quadrant keeps pinch-mid (validated slide).
- Steering latch-relative (`DuelStick_DeflRel`): palm minus the
  grab anchor. Grabbing never steers; any grip, either hand.
  Held-but-anchorless freezes (no phantom decay).
- One `DuelFire` machine per hand; `HandFire` ORs both; stick
  marker red when either fires; `DUEL_FIRE L/R` telemetry.
- Sim untouched (same getters, same main OR).

## Tests
- 3 C tests on device (fist max/scraps/no-palm; fist machine
  latch/band/release/yank; relative zero/push/clamp).
  ALL DUEL TESTS PASS (`/data/local/tmp/test_duel_m37`).
  Rules +3, 2 updated (per-hand fire loop, holder-relative drive).
  ALL RENDER RULE TESTS PASS.

## APK
- `XWADuelVR-M3.7-FISTFIRE-debug.apk`, v3.2-M3.7-fistfire,
  2212871 bytes,
  SHA-256 `A5F138B7E2D7274CF50C66B1CED8FB93297F45A698A988C60D2BFB68E5D727F2`.
  Installed via adb (standing order). Boot clean, mask=3,
  mesh L=1 R=1 (1360/6942), no FATAL.

## Quest validation M3.7 (user, 2026-10-08)
- "excelente!!" Fist grip + either thumb work.
- BUT the trigger sense is backwards for the user: thumb UP should
  be NO fire; thumb RESTING ON the index = continuous fire.
- Fist hold flaps (marker detaches though steering follows);
  same on the quadrant (pinch opens mid-slide).
- Order: remove the diamonds + control lines when possible.
- New order (same session): TIE too fast glued to the tail,
  impossible to see/shoot → easier; guide arrow should follow
  the HEAD so the enemy bearing is always known.

## Remaining issues
- Fist band (10/12 cm) derived from index data; middle/ring/little
  assumed tighter — tune from HMD if latch feels sticky/loose.
- Held-but-anchorless freeze is a corner case (palm invalid at the
  exact latch frame); re-grab resolves.

# XWADuelVR M3.8 TOUCH-FIRE + STICKY GRIP + FAIR FIGHT

## Design (hands)
- Touch-fire: thumb tip (5) to index-intermediate (8); touch
  (<= 3 cm, 2 frames) = continuous fire, peel past 4.2 cm
  (3 frames) = stop. Lifted/away thumb = rest = no fire.
  Grip + index-curled gates unchanged (either hand).
- Caveat (inherent to the mapping): a thumb WRAPPED over the
  fingers while steering also reads "on the index" and fires.
  The user's thumb-away rest pose avoids it; no clean automatic
  disambiguation exists (pressure unmeasurable).
- Sticky grip: fist band 11/14 cm (was 10/12; user's hand runs
  big/loose, was flapping at the edge); quad pinch release
  7 -> 8.5 cm (latch still 5.5, deliberate).
- Grip telemetry: `DUEL_GRAB ... fist=(L,R) pinch=(L,R)` @1 Hz.
- Markers/tethers OFF (`DUEL_GRAB_MARKERS 0`, code retained);
  quadrant handle box stays (it IS the throttle control).

## Design (M3.8b fair fight, user order)
- TIE: max speed 150 -> 135 (player 160 keeps the extension
  edge); turn rate 1.1 -> 0.9 (overshoots turns, crosses the
  nose instead of gluing to the tail); attack throttle 0.55 ->
  0.70 (runs through instead of sitting behind); evade at 80 m
  (was 60) for 2.0 s (was 1.6 literal, now `DUEL_TIE_EVADE_TIME`).
  Fire rate/damage untouched. First-guess balance, needs HMD.
- Guide arrow now HEAD-locked (`Duel_HeadGuide`): amber arrowhead
  2 m ahead of the eyes, pointing in the head plane toward the
  TIE; turn the head toward it. Hidden inside the 12-deg view
  cone (red diamond + lead pip cover). No-depth HUD lines:
  visible wherever you look. Ship chevron removed (helper +
  tests kept). HMD stays in view only (render-side bearing).

## Tests
- C: fire_touch (near band 3/4.2 cm + touch/peel cycle),
  head_guide (ahead-hidden, 90-deg head swing, up, astern);
  grab_fist values moved to the widened band; trigger/gates
  symbolic (mirror logic keeps them green). ALL DUEL TESTS PASS
  (`/data/local/tmp/test_duel_m38`). Rules +2, 2 updated.
  ALL RENDER RULE TESTS PASS.

## APK
- `XWADuelVR-M3.8-TOUCHFIRE-debug.apk`, v3.3-M3.8-touchfire,
  2211975 bytes,
  SHA-256 `334A19C130E1750DF56E377C907EB969BAD68A23F90DFC9B7A3BB1DC3388D503`.
  Installed via adb (standing order). Boot clean, mask=3,
  mesh L=1 R=1 (1360/6942), no FATAL.

## Quest validation M3.8 (user, 2026-10-08)
- Hands briefly missing after install: tracking state, not a
  regression (reappeared; Quest rebooted by user). NOTE: with
  markers gone there is no on-screen grab cue; column-follow +
  fire are the only feedback now.
- Stick won't grab: hand latches ~10 cm RIGHT of the column, in
  air. Diagnosis: 16 cm latch sphere + no visual guides =
  blind air-grabs. Fix in M3.8c (9 cm positioned latch).
- Order: easier TIE kills; review XWA difficulty functions.
- Q: can our hands experiment degrade Quest hand/controller
  detection? A: no (read-only XR consumer; act=0 just falls
  back). Real culprits: awake controllers fighting hand mode,
  light, OS state. Mitigation: power controllers OFF (not just
  set down) for clean hand sessions.

## Remaining issues
- Wrapped-thumb steering misfires by design (see caveat).
- Balance is unflown theory; expect one tuning round.
- Arrow size/offset (14 cm @ 2 m, 18 cm below center)
  first-guess ergonomics.

# XWADuelVR M3.8c POSITIONED LATCH + FAIR KILL

## XWA difficulty research (read-only, C:\OpenXWA\src\xwa)
- `game_config`: difficulty + tourDifficulty, 3 levels (0-2).
- Gameplay effect is BEHAVIORAL skill tiers, not HP: per-pilot
  `aiSkillTier` indexes tables for shoot max range, laser link
  mode, fire range limit, lock threshold, turn-away delay,
  attacker search + warhead threat ranges (`flight/ai/pai*.c`).
- `g_flightDifficulty` also gates attacker gang-up limits.
- Mirror: our easy = worse TIE evasion/fire envelope (below),
  plus a direct HP cut per explicit user order (XWA-purist
  deviation, documented).

## Design
- Stick latch 16 -> 9 cm (`DUEL_GRAB_STICK_LATCH_R`; release
  stays 20 cm). Machine takes grab/lose radii params now;
  pinch wrapper unchanged (quad keeps 16/20, validated).
- Winner-dist telemetry: `DUEL_GRAB ... wd=(Q,S)` @1 Hz —
  validates the 9 cm sphere from the next session's log.
- Fair kill: TIE HP 100 -> 70 (6 hits @12 dmg), radius 5 -> 7 m
  (forgiving hits), laser cooldown 0.55 -> 0.75 s (breathing
  room), weave x0.5 (`DUEL_TIE_WEAVE`, steadier lead).
  TIE damage 10, player guns untouched. Stacks with M3.8b
  (slower, wider turns, earlier/longer breaks).

## Tests
- grab_fist re-midded to the 9 cm sphere + air-rejection case
  (shut fist at 12 cm never latches). ALL DUEL TESTS PASS
  (`/data/local/tmp/test_duel_m38c`). Rules +2.
  ALL RENDER RULE TESTS PASS.

## APK
- `XWADuelVR-M3.8c-FAIRKILL-debug.apk`, v3.4-M3.8c-fairkill,
  2212083 bytes,
  SHA-256 `586728BAC64810DF5C9029CDDE2D39A6B3D4A7DFEBF1BFF48600BA0A641D9C5E`.
  Installed via adb wifi (tcpip re-enabled post-reboot).
  v3.4 confirmed on device, fresh pid, no FATAL.

## Quest validation M3.8c (user, 2026-10-08)
- "exito rotundo, muy bien!!! milleston superado!"
- Touch-fire, positioned latch, diamonds/lines removed, TIE
  fairness + head arrow, fair-kill bundle: ALL ACCEPTED.
- No further M3 issues reported this session.

# M3 INTERACTIVE COCKPIT - MILESTONE COMPLETE (2026-10-08)

## Shipped and user-validated
- M3.0 virtual hands (XR_EXT 26 joints, manifest fix).
- M3.1/3.1b lever grab + grip position.
- M3.2/3.2a throttle + grab tune.
- M3.3 HOTAS split (stick + quadrant).
- M3.4 index trigger (superseded).
- M3.5 platform meshes (XR_FB) + thumb trigger.
- M3.6 right-hand IBO fix + free grab + fist-gated thumb.
- M3.7 fist-held stick + latch-relative steering + either thumb.
- M3.8 touch-fire + sticky grip + markers off + fair fight +
  head look-cue.
- M3.8c positioned 9 cm latch + fair kill (XWA-skill mirror).
- Final: `XWADuelVR-M3.8c-FAIRKILL-debug.apk`, v3.4,
  SHA-256 `586728BA...D9C5E`, installed, user celebrating.

## Frozen for M4
Seat, HMD cal, camera hierarchy, controls, sim/AI/balance,
audio, stars/RealSpace/celestial, hands/meshes, grab/fire.

## Remaining issues
- 9 cm latch is blind (no markers): placement guided only by
  the visible column + column-follow feedback. If finding the
  sphere is hard, options: latch click audio, or NEAR-only micro
  cue (both need user approval: clean-cockpit order stands).
- Wrapped-thumb steering still misfires by design (M3.8 caveat).

# M4 PILOT BODY (PROPOSED, BLOCKED ON FILE)

## User order (2026-10-08)
Evaluate as visible pilot body (note: user wrote "XWAQuest";
interpreted as XWADuelVR: seat/HMD/tracking/levers all live
here; XWAQuest stays read-only):
https://sketchfab.com/3d-models/star-wars-x-wing-rebel-flight-suit-rigged-efa36772d0984634ac6ed8a0e95f8d14
- Stage 1: seated pilot (torso/legs/boots visible looking down),
  head/helmet hidden in first person, seat+HMD untouched.
- Stage 2 (after validation): drive pilot hands from existing
  tracking (+finger bones/weights), IK arms shoulder->hand, keep
  grabs/lever controls, never double-draw hands.
- Keep Quest perf; no physics/camera/flight-control changes.
- License/format/rig audit BEFORE any repo inclusion; explain
  simplification or GitHub-blocking license first.

## File search (2026-10-08 23:11)
- Workspace `assets/`: ship glTFs only, no pilot model.
- `Downloads` (filter + last 14 days): no .glb/.gltf/.fbx/.zip
  pilot match. MODEL NOT LOCAL.

## Next step
USER: download the Sketchfab model (glTF/GLB preferred + its
license text) and drop it in `C:\OpenXWA\XWADuelVR\evidence\
pilot-inbox\` (create the folder if missing), then say "listo".
Muse then runs the M4.0 audit (format, skeleton, finger bones,
textures, tri count, license/GitHub verdict) before touching
any game code. No implementation until the file exists.

# M5 SKIRMISH (2026-10-08/09, built + installed, HMD test pending)

## User order (2026-10-08)
- Enemy easier to catch/kill (still glued behind, impossible).
- Same change: MORE enemy ships + ALLIES fighting.
- Player bolts must NOT damage friendlies; must damage ALL
  enemy ships.
- Separately: transient hand pops above the view, appears and
  vanishes.

## Design
- 3 TIEs (loose vee ahead) vs player + 2 X-wing allies
  (echelon flanks). Wave resets when the player dies OR the
  last TIE dies; dead allies stay dead until reset (stakes).
- Team-only bolts, no shooter IDs: rebel bolts (player +
  allies) test TIEs only; imperial bolts test player +
  allies. Friendly fire impossible in both directions.
- TIE softened: MAX 120 (was 135), TURN 0.75 (was 0.9),
  close-chase throttle 0.5 (was 0.6): the player outruns
  (160) and out-turns every TIE; tails become passes.
- Each TIE chases its NEAREST rebel (attention splits across
  the furball); allies plain-chase nearest TIE (X-wing guns,
  0.70 s cd, 8 dmg), formate on the player when clear.
- Diamond + lead pip + head arrow + red HP bar follow
  Duel_AutoTarget (nearest living TIE); TIE scream follows
  nearest living TIE to the listener.
- Allies reuse XwingExterior (pushed to device storage; the
  ready gate does NOT require it: guarded draw).

## Hand-pop fix
- Root cause: the platform mesh drew on bare isActive. A
  1-2 frame partial track (hand entering the frustum) left
  wrist/palm joints garbage, and the wrist-fallback skin
  teleported verts (flash above the view).
- Fix: palm(0)+wrist(1) valid + n_valid>=6, held 3 straight
  frames before a side appears; hides instantly on invalid.
  Grab/fire logic untouched (already per-joint gated).

## Files
- `src/duel_sim.h/.c`: ties[3], allies[2], ai/ai_t/evade
  arrays; duel_face_quat; duel_update_tie(s,idx),
  duel_update_ally; duel_fire(s,owner,idx); team laser
  collision (duel_swept_hit/duel_damage); wave phase;
  Duel_AutoTarget/TiesAlive/AlliesAlive.
- `src/flight_config.h`: TIE 120/0.75; DUEL_ALLY_* block.
- `src/duel_render.c`: per-ship FX memory + loops; fresh-wave
  rearm guard; target-driven cue/diamond/lead/bar; furball
  draw (xwing guarded); xwing load/destroy; hand-pop gate.
- `src/duel_audio.h/_core.c`: per-ship fire/hit/death; ally
  X-wing gun positional; R2 on TIE kills only; scream
  follows nearest living TIE.
- `src/duel_main.c`: DUEL log tracks auto-target + counts.
- `app/build.gradle`: versionName 3.5-M5-skirmish.
- `tests/test_duel.c`: migrated (ties[0] semantics) + 4 new
  M5 tests (no-friendly-fire, imperial bolts rebels-only,
  autotarget, ally kills).
- `tests/test_render_rules.py`: 4 strings updated, TIE const
  rule -> M5 values, loads rule allows XwingExterior
  (allies-only draw pinned), +6 M5 rules.

## Tests (Quest 3487C10H9T0BKZ, 2026-10-09)
- `/data/local/tmp/test_duel_m5`: ALL DUEL TESTS PASS
  (incl. m5_no_friendly_fire steps=195, m5_tie_bolts,
  m5_autotarget, m5_ally_kills 210->100).
- `test_render_rules.py`: ALL RENDER RULE TESTS PASS.
- Fixes during run: spawn-index swap (formation wrote
  ties[1] left, test assumed right); lifetime silencing
  (furball guns fire inside the wait window).

## Device assets
- Pushed `assets/raw/XwingExterior` (59 files, 450379 B:
  .gltf + .bin + 57 textures) to
  `files/duel_assets/XwingExterior` via run-as cp.

## APK
- `XWADuelVR-M5-SKIRMISH-debug.apk`, v3.5-M5-skirmish,
  2217443 bytes,
  SHA-256 `168BF1A77761433DCB13EF5C03F93745760F0D7B206308C97BB13F6BDD09F61B`.
- Installed via adb USB; dumpsys confirms
  versionName=3.5-M5-skirmish.
- .so verified fresh (M5 strings present, no stale-.so).
- Headless launch: clean exit, no FATAL (XR frames need HMD).

## Quest validation (pending user in HMD)
- TEST 1: 3 TIEs visible ahead, 2 allies on flanks.
- TEST 2: TIEs catchable (player faster, out-turns them).
- TEST 3: bolts kill every TIE; allies never take
  player-bolt damage.
- TEST 4: wave resets after last TIE or player death.
- TEST 5: no hand flash above view.
- TEST 6: fps ~72 with the furball.

## Next step
USER IN HMD: run the 6 M5 tests above and report.

# M6 AXIS SPLIT (2026-10-09, built + installed, HMD test pending)

## User order (2026-10-09)
- Right lever: fwd/back = pitch; left/right = YAW (nose
  turns, no bank).
- Left lever: fwd/back = throttle; left/right = ROLL.
- Remove stick-lateral roll + bank coordination entirely.
- Yaw and roll independent. On release pitch/yaw/roll
  return to neutral; throttle holds last value.
- (Supersedes the interim "stick pitch-only" answer: the
  user then specified the full 4-axis split.)

## Design
- Sim: `Duel_HandStick(active, pitch, yaw)` replaces roll
  with yaw (hand_roll -> hand_yaw); new independent
  `Duel_HandLever(active, roll)` channel (hand_lev_*). Each
  hand overrides only its own axes; both levers at once.
- `yaw += DUEL_COORD_YAW * roll` deleted; the define is
  gone from flight_config.h.
- Signs verified: stick right (+rd) = nose right with
  level wings; lever left (-ld) = bank left (ship CCW in
  view, the user's "contra manecillas").
- Lever roll is latch-relative (own anchor per side, same
  7 cm DeflRel range as the stick) and spring-decays on
  release; throttle stays absolute via the pinch midpoint
  and holds on release (existing friction behavior).
- Lever grip follows slot + roll swing 1:1
  (DuelLever_GripShip); handle visual reshaped from the
  gray box into a slim vertical lever grip (same gray
  physical material, not a marker).
- Free grab unchanged (either hand, either lever); fire,
  seat, camera, sim/AI, audio untouched.

## Files
- `src/duel_sim.h/.c`: hand_yaw + hand_lev_*; split
  overrides; Duel_HandLever.
- `src/flight_config.h`: COORD_YAW removed.
- `src/duel_hands.h/.c`: DuelLever_GripShip.
- `src/duel_render.c`: defl_ld + lever anchors; latch,
  track, decay; DuelRender_HandLever; 3 grip sites;
  lever-handle visual; GRAB log + lev field.
- `src/duel_render.h`, `src/duel_main.c`: lever publish.
- `tests/test_duel.c`: stick test -> pure yaw; coord test
  REPLACED by no-coord + lever + lever-grip tests.
- `tests/test_render_rules.py`: split/no-coord/grip rules.

## Tests (Quest 3487C10H9T0BKZ, 2026-10-09)
- `/data/local/tmp/test_duel_m6`: ALL DUEL TESTS PASS
  (hand_stick_sim, hand_no_coord_sim, hand_lever_sim,
  lever_grip all ok).
- `test_render_rules.py`: ALL RENDER RULE TESTS PASS.

## APK
- `XWADuelVR-M6-AXISSPLIT-debug.apk`, v3.6-M6-axissplit,
  2218055 bytes,
  SHA-256 `E28B730EB04BA0E6D198AC80DD536A1CA1A27A3382E2FD90245F5CC979EFBD9E`.
- Installed via adb USB; dumpsys confirms
  versionName=3.6-M6-axissplit. .so verified fresh.

## Quest validation (pending user in HMD)
- TEST 1: right lever fwd/back = pitch, lateral = nose
  turns with level horizon.
- TEST 2: left lever fwd/back = throttle (holds), lateral
  = roll (world rotates).
- TEST 3: both levers at once (pitch + roll together).
- TEST 4: release -> pitch/yaw/roll neutral, throttle
  stays.
- TEST 5: no coordinated turn anywhere (roll never yaws).

## Next step
USER IN HMD: run the 5 M6 tests above and report.

# M6 APPROVED (2026-10-09)
User: "excelente! milleston aprobado! completado ahora si es un
juego. Gracias!!" M6 v3.6 (`E28B730E…`) is the frozen
playable baseline. Continuation (menu + pilots + options):
see CHECKPOINT-XWADUELVR-M7-MENU.md (stage 1 done).
