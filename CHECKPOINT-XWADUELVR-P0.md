# XWADuelVR P0

## Goal
Native Quest VR combat prototype: ONE duel, X-Wing (player, cockpit) vs TIE
Fighter (AI), space + stars + planets. No menus — boot straight into the
cockpit. On either ship's destruction: ~2.5 s explosion, automatic reset.

## Architecture
- `src/duel_main.c`: SDL entry, XR frame loop (poll → wait/begin → locate →
  input → sim → render eyes → end).
- `src/duel_sim.c`: player flight model, TIE AI (ACQUIRE/CHASE/ATTACK/EVADE/
  REACQUIRE), laser pool (swept sphere collision), damage, reset loop.
- `src/duel_xr.c`: OpenXR session on SDL_GPU/Vulkan (ported from vr-probe).
- `src/duel_input.c`: OpenXR actions → M8-faithful mapping.
- `src/duel_render.c`: SDL_GPU stereo renderer (lit ships, procedural planet/
  moon, star points, laser quads, particle points, line reticle+diamond,
  quad HUD bars, flash quad).
- `src/duel_math.h`: vec/quat/mat/frustum, zero deps.
- `src/flight_config.h`: all tuning constants.
- `tools/duel_cook*`: OPT→glTF converter (runs on Quest, NDK clang arm64).
- `tests/test_duel.c`: 7 offline tests, run on Quest via /data/local/tmp.

## Independence from XWA engine
Zero XWA sim/engine linkage: no `xwa_*`/`aeron_*` archives, no GameData, no
Wine/Box64, no `XwaPort`/`XwaFlightTask`. Runtime links ONLY: `libSDL3.so`
(M2 build, read-only ref), `libopenxr_loader.so`, `libc++_shared.so`,
system `log/android/m/dl`. Vendored single-file libs (with LICENSE):
`src/cgltf.h` (glTF parse), `src/stb_image.h` (PNG decode).
Provenance note: `tools/cook_include/*` (opt.h, opt2gltf.h, cgltf_write.h,
stb_image_write.h, aeron/log.h + mesh_normals.h headers) are compile-time
ONLY for the cook tool; no engine code ships in the APK.

## Reference code from XWAQuest
READ-ONLY, ported with adaptation (no runtime dependency):
- `vr-probe/vr_openxr.c` → `src/duel_xr.c`: session/swapchain/frame flow,
  100 ms acquire timeout, begin-retry loop. Aeron XR probe REPLACED by SDL3
  XR GPU-device properties (`SDL_PROP_GPU_DEVICE_CREATE_XR_*`).
- `m8integrated1/input_xr.c` → `src/duel_input.c`: action set, oculus touch
  bindings (`/user/hand/{left,right}/input/{thumbstick,trigger/value}`),
  sync/get-state pattern, not-focused neutral.
- `m8integrated1/touch_state.h TouchConvert` → mapping: yaw=lx, pitch=-ly,
  roll=rx, throttle=ry lever @0.5/s, RT fire hysteresis .45/.55, .15
  deadzone, neutral-arming.
- `m8integrated1/vr_eye_math.h` + `vr_stereo.c`: per-eye asymmetric frustum
  approach (reimplemented as `duel_frustum_xr`, Vulkan 0..1 + Y-flip).
- `aeron/tools/opt2gltf/*` → `tools/duel_cook_main.c` (CLI copy) +
  `cook_support.c` (cgltf impl + `Aeron_Log{ Warn,Debug}` stubs).
- `m8integrated1/.../AndroidManifest.xml` → XR permissions/features/queries.

## XWAUP assets
Source (READ-ONLY, never modified):
`C:\games\XWAU-Mods\XWAU2025\Backup\2026-02-04_22.23.37_XWAU 2025\Flightmodels\`
- `XwingExterior.opt` (494531 B) — cooked, NOT shipped P0 (cockpit view only).
- `TieFighter.opt` (369017 B) → `assets/raw/TieFighter/`: 3 meshes, 9 prims,
  18 textures, 17 materials.
- `XwingCockpit.OPT` (525414 B) → `assets/raw/XwingCockpit/`: 6 meshes,
  63 prims, 37 textures.
Cook: `duel_cook --emissive` on Quest (3 ok, 0 failed). Total 118 files,
~1.1 MB. Bounds verified: TIE ±3.5/±4/±3.3, cockpit x±5.1 y−1.9..1.7
z−6..2.8 (muzzle ±5 X and radii 5/6 consistent). License: local prototype
use of user's own install; assets NOT for public redistribution.

## OpenXR
Stereo `XrCompositionLayerProjection`, LOCAL space, per-eye color swapchain
(runtime format) + D32 depth. Session begin retried 10× on NOT_READY.
Input actions attached post-session. View validity bits required.

## Controls
Left stick: yaw (X) + pitch (−Y; push forward = nose down, flight-sim
convention). Right stick: roll (X) + throttle lever (Y integrates 0..1).
Right trigger: fire (hysteresis). Head: free look only — never steers.

## Flight model
Throttle→speed lerp (40..160 m/s player), accel-capped; intrinsic yaw→
pitch→roll quaternion rates (0.9/1.6/2.2 rad/s); velocity = forward×speed.

## TIE AI
Steering via shortest-arc slerp capped at 1.1 rad/s; deterministic wobble
(two sines) breaks perfect orbits; EVADE latches sideways+up dir on
overshoot (<60 m); fires inside 15° cone <700 m; preferred 350 m.

## Weapons
Pooled projectiles (64, 2.5 s life): player red 600+ship m/s from 4
alternating wing muzzles, TIE green 550+ship m/s from 2 chin muzzles.

## Collision
Swept segment-vs-sphere per step (endpoint test tunneled at 700 m/s —
caught by tests, fixed in sim). Radii: player 6, TIE 5, laser 1.2.

## Damage
Player/TIE 100 HP; 12/10 per hit. Hit sparks on HP decrease; death =
120-particle burst + screen flash + ship hidden.

## Explosion
CPU particle points (additive) + fullscreen flash quad + 2.5 s DESTROYED
phase (world keeps drifting), then `Duel_ResetCombat()` (session, assets,
swapchains kept).

## Reset loop
BOOT → COMBAT ⇄ DESTROYED (2.5 s) → COMBAT… kills tallied, logged 1 Hz.

## Stars
Own procedural field: 1600 LCG points on r=14000 sphere, temp variance,
2.5 px, depth-test-on/write-off. No XWAQuest star code.

## Planet
Procedural gas giant (r=1500 at (3500,900,−9500)) + moon (r=420):
UV-sphere mesh, band/limb/day-night fragment shader. XWA backdrop asset
deferred (FUTURE.md).

## Cockpit
XWAUP `XwingCockpit.OPT` via lit pipeline in ship frame; camera =
ship × seat × hmd. `DUEL_SEAT_POS = (0, 0.4, −1.0)` initial guess from
bounds (cockpit glass extends to −Z ⇒ nose=−Z); VERIFY on first visual,
tune constant. Near 0.05 / far 30000.

## Performance
P0 estimate: ~180 draws/eye worst case (100+63+9 prims unbatched + sky/
fx), <100k tris. Unmeasured — first Quest run logs fps + counts (1 Hz).
No MSAA P0; batching deferred.

## Offline tests
`tests/test_duel.c` (quat/flight/AI-steer/laser-collision/laser-lifetime/
reset-loop/spawn): built with NDK clang arm64, run on Quest:
`ALL DUEL TESTS PASS` (TIE dies in 97 steps sustained fire; climb
0→1; swept fix verified). Controller mapping implicitly covered by
input constants (live-verify feel on device).

## APK
- `C:\OpenXWA\XWADuelVR\XWADuelVR-P0-debug.apk` (2.1 MB),
  SHA-256 `9482ED29D003A60BBE42D5C34274AEA230C6688E6E9574D2AE288A25A3D84713`
  (v3: black-screen fix + live XWAUP assets),
  package `org.openxwa.xwaduellvr`, versionName `0.1-P0-duel`, versionCode 1,
  arm64-v8a, NDK 28, debug-signed. Native `libXWADuelVR.so` 1.3 MB.
- Git: repo initialized, NO commits (no history write requested).
- INSTALLED on Quest (user-authorized) with live assets; awaiting user-run.

## First Quest runs (2026-10-06 21:15–21:19)
- v1: `SDL_Init(0)` → GPU device "Video subsystem not initialized" → instant
  quit. Fixed: `SDL_INIT_VIDEO`.
- v2: "No supported SDL_GPU backend" → instant quit. Fixed: declare
  `SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN`.
- v2b: session begun, input ready, models loaded — but user saw only darkness.
  Root cause: **copy pass nested inside the render pass** in `DuelRender_Eye`
  (dynamic-buffer upload sat between begin/end render). SDL_GPU forbids
  nesting; all draws died silently. Fixed: build dyn verts → copy-pass upload
  → begin render → draw. Regression guard: `tests/test_render_rules.py`
  (5 checks, laptop-runnable; verified FAIL on reintroduced bug, PASS fixed).
- Asset upgrade: first cook used `Backup/` VANILLA models (TIE 471 verts).
  Recooked from live `XWAU2025/FlightModels/`: TIE 2.9 MB OPT → 150 prims/
  40 tex; cockpit 27 MB OPT → 295 prims/143 tex (`assets/raw-live/`, 187
  files, ~19 MB on device). Bounds consistent with sim radii/muzzles.
- Diagnostics added: 5 s "DUEL waiting" heartbeat when views invalid,
  rate-limited command-buffer failure log.

## Quest validation plan
1. `adb install XWADuelVR-P0-debug.apk` (solo-SDL rule: force-stop others).
2. Push assets: `assets/raw/*` + `shaders/*.spv` → run-as
   `files/duel_assets/{TieFighter,XwingCockpit,../shaders}` (layout mirrors
   `load_shader`/`load_model` paths: shaders at `<asset_dir>/../shaders/`).
3. Launch, wear headset: expect cockpit + stars + planet + TIE ahead.
4. 12-step success criteria (§41): look, fly, chase, fire, take fire, kill/
   die, explosion, auto-reset.
5. Tune from observation: `DUEL_SEAT_POS`, muzzle Y, light dir, AI rates.

## Known limitations
- Silent (no audio).
- Seat/camera offset unverified visually.
- Ships unbatched (1 draw/prim); no MSAA; no text overlay (logcat stats).
- Cockpit glass transparency: OPT alpha planes render opaque P0 (classify
  `OPT_GLTF_ALPHA_*` follow-up).
- Player exterior never rendered; S-foils static; TIE studio lighting fixed.

## v3 user run (2026-10-06 ~21:25)
- User reports STILL BLACK in headset with v3 (pass-nesting fix + live assets).
- Hypotheses for next diagnosis round (untested, app NOT launched by me since):
  1. Frames never submit with rendered=1 (views invalid? acquire failing?) —
     new "DUEL waiting" heartbeat + fps logs will discriminate; capture logcat
     right after user run.
  2. Uniform/sampler binding mismatch vs SDL_GPU Vulkan expectations (VS set 1,
     FS samplers set 2 / uniforms set 3) — draws dropped silently.
  3. Camera inside cockpit geometry (seat offset) — but HUD bars (NDC,
     depth-off) should still show; their absence points at (1)/(2), not camera.
  4. Swapchain format 52 handling (sRGB? yields dark but not pure black).
- Next step: capture fresh logcat from user's v3 run (fps/waiting/acquire
  lines), then bisect: NDC-only test frame (HUD bars + flash, no 3D) to isolate
  submit-vs-draw failure.

## Render Recovery

### Historical XWAQuest path
M1 (`C:\OpenXWA\XWAQuest\app\src\main\cpp\main.c`, SDL XR spinning-cube
adaptation): SDL_GPU/Vulkan + OpenXR, user-confirmed visible colored cube on
dark blue (REPORTE-M0-M1.md: "veo el cubo de colores correctamente", APK
`6B80E1C6…`, 2400+ frames `xrEndFrame=0`). No hello_xr/GLES prototype exists
in-tree (searched; only M0 milestone refs outside XWAQuest).

### Known-good physical evidence
M1 logs: `BUILD backend=vulkan`, `XR Session begun!`, `View count: 2`,
swapchains 1680×1760×3 images + depth, `FRAME submitted=N views=2 cubes=1
shouldRender=1 xrEndFrame=0` to frame 2400, plus the user's visual quote.
No Vulkan/OpenXR errors, no crash/ANR.

### Graphics API
Vulkan via SDL_GPU on both sides (M1 AND DuelVR). Decision: KEEP Vulkan —
no API migration needed; the known-good path uses our exact stack.

### Current XWADuel divergence
| # | M1 known-good | DuelVR v3 | Verdict |
|---|---|---|---|
| 1 | VS uniform `register(b0,space1)` = SPIR-V set 1 (disassembled) | VS set 1, FS samplers set 2, FS uniforms set 3 (disassembled) | MATCH — bindings exonerated |
| 2 | Explicit viewport + scissor per eye | None (defaults assumed) | FIXED: explicit per eye |
| 3 | Depth D24_UNORM, LESS_OR_EQUAL, `cycle=true` | D32_FLOAT, LESS, no cycle | FIXED: D24/LEQUAL/cycle |
| 4 | XR_INFINITE_DURATION acquire wait | 100 ms timeout | FIXED: infinite |
| 5 | Explicit `"vulkan"` backend prop | Backend by default | FIXED: explicit |
| 6 | Transfer buffer released AFTER submit | Released BEFORE submit (per-eye alloc) | FIXED: persistent tbuf, init-time alloc |
| 7 | Copy pass only at init (never nested) | v2 nested copy inside render (fixed v3) | Already fixed in v3 |
| 8 | Projection: NO Y-flip, GL-style depth | Y-flipped, Vulkan 0..1 depth | INTENTIONAL: M1's cube is vertically mirrored (invisible for a cube); DuelVR flips so up is up. Covered by render_math test |
| 9 | Session begin on READY event | Retry loop (works: "session begun") | Kept (proven working) |
Most likely v3 black cause: #2 and/or #6 (unset viewport on XR targets;
pre-submit transfer release racing the GPU). #1 confirmed innocent by
disassembly, closing the leading hypothesis.

### Root cause
Not single-point proven (no on-device run by me); P0.1 eliminates every
mechanical divergence vs the physically-visible M1 path AND adds the M1
cube itself as an in-frame oracle: cube visible + scene black = content/
camera issue; cube black too = submit-level issue. Bisection in one build.

### Renderer changes
- `duel_xr.c`: NAME=vulkan, D24 depth, infinite acquire wait.
- `duel_render.c`: LESS_OR_EQUAL + D24 pipelines, depth `cycle=true`,
  explicit viewport+scissor per eye, persistent dyn transfer buffer,
  R0 M1-cube oracle (verbatim M1 math + SDL cube bytecode
  `shaders/m1_cube_{vert,frag}.spv`, local (0,0,−2), slow spin), drawn
  FIRST each eye before planet/ships/HUD.
- Sim/input/math/config: UNTOUCHED (except tests).
- Shaders staged on device: 8 .spv in `files/shaders/` (incl. m1 pair).

### Coordinate conventions
- OpenXR LOCAL: +X right, +Y up, −Z forward (right-handed, Y-up).
- DuelVR sim/ships: same (+X right, +Y up, −Z forward); models authored
  nose≈−Z (cockpit glass extends −Z).
- Vulkan NDC: x left→right, y TOP→BOTTOM (screen-up = negative), z 0..1.
- M1 oracle intentionally uses M1's unflipped projection (mirrored cube,
  faithful); scene uses `duel_frustum_xr` flipped (up is up).

### Primitive test
R0 = M1 cube in-frame (no separate build): visible spinning colored cube
at 2 m = R0 PASS. Quest validation pending.

### TIE test
R1/R2: live XWAUP TIE (150 prims/40 tex, already on device) via lit
pipeline in the same frame as the cube. Quest validation pending.

### Cockpit test
R3: live XWAUP cockpit (295 prims/143 tex) in ship frame; seat
(0,0.4,−1.0). If it blinds the camera despite working submit, disable
temporarily (one-line). Quest validation pending.

### Simulation preservation
YES: `duel_sim.c`, `duel_input.c`, `duel_math.h`, `flight_config.h`
byte-identical except test additions. Suite 8/8 PASS on Quest (quat,
flight, AI-steer, laser-collision, laser-lifetime, reset, spawn,
render_math new) + `test_render_rules.py` 5/5 on laptop.

### APK
- `XWADuelVR-P0.1-RENDERRECOVERY-debug.apk` (build 2, XrView fix),
  SHA-256 `2B5FEA426D7E087EFA567E6DDF43CFAAAEBA6E793CDB017464563F78BB8E2DB2`
  (build 1 was `A776BEC2…`, same versionName),
  versionName `0.1-P0.1-renderrecovery`, same package/ABI/NDK.
- INSTALLED (user-authorized "instala"); user runs it themselves.

### P0.1 build-1 user run (2026-10-06 21:51)
- Still black. Logs decisive: `should_render=1` + `DUEL waiting` every 5 s,
  models loaded (TIE 68883 verts/150 prims/33 tex, cockpit 370970 verts/
  295 prims/143 tex), process alive, no crash, no fps lines.
- Root cause: `XrView` structs passed to `xrLocateViews` WITHOUT `.type`
  set (stack garbage → validation failure → zero views → empty frames).
  M1 sets `views[eye].type = XR_TYPE_VIEW` every frame; DuelVR didn't.
- Fix: type-init + memset in `DuelXr_LocateViews` + failure logging.
  Regression: `test_render_rules.py` check #6.
- Unrelated: 21:47 SIGSEGV pid 3990 = `m8integrated1.starprobe` (XWAQuest
  line), not DuelVR. Not touched.
- P0 v3 APK preserved (`XWADuelVR-P0-debug.apk`, `9482ED29…`).
- Modified: `src/duel_xr.c`, `src/duel_render.c`, `tests/test_duel.c`,
  `app/build.gradle`; added `shaders/m1_cube_{vert,frag}.spv`,
  `tests/test_render_rules.py`. No commits (no history write requested).

### P0.1 build-2 user run (2026-10-06 21:55) -- FIRST PIXELS
- User: visible but wrong -- not in cockpit, ship flipped, world seems to
  recenter on head-turn. Saw shots, stars, planet, color cube. 3 photos
  pulled (user-authorized) to `evidence/p0.1-build2/duel{1,2,3}.jpg`.
- Photos decisive: reticle + stars + planet + M1 cube all render. Camera
  sits INSIDE the rear fuselage looking BACKWARD at the tail: engine
  nozzles face the camera, wings frame the view. Cockpit OPT identity
  basis + seat (0,0.4,-1.0) + sim forward -Z vs cooked nose +Z.
- "Head glue" = misread of the exterior tail view (view math already
  covered by the head-turn test); re-verify after the camera fix.

## Camera fix (P0.1 build 3, 2026-10-07)

### Measured cooked frames (glTF meters, identity nodes, read-only audit)
- All three OPT cooks share: nose/front = +Z, up = +Y, right-handed.
- XwingExterior: fuselage z -5.88/+6.48 (nose +6.48), wingtip laser
  hardpoint_0 (-5.27,+0.05,0), canopy glass y +1.24/+1.57 z -2.07/-1.64.
- XwingCockpit mesh00 cage: x -0.57/+0.53, y +0.15/+1.14, z -1.09/+1.11;
  6x hardpoint_20 (instruments) centroid ~(0,+0.37,+0.16); rear canopy
  arch (mesh05) z -1.65/-1.19 y +0.89/+1.23. Cage top (+1.14) meets
  exterior glass bottom (+1.24): shared frame confirmed.
- TieFighter: ball r~1.31; 2x hardpoint_3 chin guns (+/-0.33,-0.80,+1.06)
  => TIE front = +Z.
- M8 reference (read-only): `T3_SeatMatrix` = ship matrix translated by
  -(hardpoint+pan/16); eye stays at ship origin, mesh offset. DuelVR
  equivalent: eye moved to measured station, mesh keeps ship frame.

### Fix (sim frame UNCHANGED, forward -Z)
- `src/duel_math.h`: new `duel_q_opt2ship()` = R_y(PI) + `duel_model_matrix()`
  = rigid(orient,pos) x R_y(PI). Nose +Z -> ship -Z; craft-right -X -> +X.
- `src/duel_render.c`: TIE + cockpit drawn via `duel_model_matrix()`
  (only OPT draws; procedural planet/cube/stars untouched). Seat
  `DUEL_SEAT_POS` (0,0.4,-1.0) -> (0,+0.80,+0.55) = R_y(PI) x eye-model
  (0,+0.80,-0.55): inside cage, above instruments, below glass, forward
  of rear arch, facing nose.
- `src/duel_sim.c`: TIE muzzle -> measured chin (+/-0.35 right, -0.80 up,
  1.5 fwd, clear of ball r=1.31). Player wingtips unchanged (visually
  sound: x +/-5, y +/-0.8, 4 fwd clears the wing leading edge at 2.5 fwd).
- Watch-item (not blocking): black rectangle in duel1.jpg -- probably an
  untextured/black cockpit mesh piece; re-check after seat fix.

### Tests
- `tests/test_duel.c`: seat updated in test_view_matrix; new
  test_model_basis (nose/wingtip/chin mapping at identity + yawed ship,
  seat back in model frame inside cage bbox). Built NDK clang arm64,
  run on Quest /data/local/tmp: 10/10 `ALL DUEL TESTS PASS`
  (laser_collision still 97 steps).
- `tests/test_render_rules.py`: 6/6 PASS (incl. XrView.type #6).

### APK
- `XWADuelVR-P0.1-CAMERAFIX-debug.apk`, 2.07 MB,
  SHA-256 `6D522B4F360DCC953AF5692CF96A4D425EBE0B282500095682E5E0E9F26C362C`,
  package `org.openxwa.xwaduellvr`, versionCode 1,
  versionName `0.1-P0.1-camerafix`, arm64-v8a, NDK 28.2.13676358.
- INSTALLED 2026-10-06 22:17 (user-authorized "instala"); user launches it
  themselves. Verified on-device: versionName `0.1-P0.1-camerafix`.
- Build-2 APK preserved (`XWADuelVR-P0.1-RENDERRECOVERY-debug.apk`,
  `2B5FEA42…`, still on Quest); P0 v3 preserved (`9482ED29…`).
- Modified: `src/duel_math.h`, `src/duel_render.c`, `src/duel_sim.c`,
  `tests/test_duel.c`, `app/build.gradle`. No commits.

### Quest validation pending
User installs + runs build 3 when ready: (1) inside cockpit over the
dashboard? (2) nose forward, TIE ahead at spawn? (3) head-turn world
stable? (4) TIE bolts from chin? Photos decide; seat tunable +/-0.15 m.

### P0.1 build-3 user run (2026-10-06 22:19) -- CAMERA FIX VERDICT
User verbatim: controls now move the ship; ship looks badly built; seat
is floating, no cockpit; Star Wars letters floating; color cube still
there; when turning the head, what look like X-wing parts move WITH
the head.
- 6 photos pulled (user-authorized) to `evidence/p0.1-build3/cam3_*.jpg`
  (221912/221916/221920/221938/221950/222011). NOT yet analyzed --
  checkpoint requested BEFORE any fix work.
- Standing hypotheses (unverified, photo analysis pending):
  (a) some cockpit primitives may render with a stale/identity MVP
  (e.g. NDC-path leak) => head-locked parts + floating seat/letters;
  (b) black-rectangle watch-item from build 2 may be the same defect;
  (c) "badly built ship" could be TIE deform and/or cockpit misassembly.
- NO code changes made after this report. NO install pending (build 3
  is the installed one).

## P0.2 Cockpit Space Fix

### Build-3 photo analysis (evidence/p0.1-build3/cam3_*.jpg, all 6 read)
- 221912/221916: dark mass center (rear canopy/seat area from inside),
  gray band top (canopy frame 0.3 m away), planet+moon+stars+reticle OK.
- 221920: same + M1 cube huge bottom-left + floating black quad right.
- 221938: wingtip cannons close left, M1 cube center, green TIE bolt,
  floating "UPGRADE X-WING ALLIANCE" decal plate bottom-right.
- 221950: FORWARD view: engines flank symmetrically, planet right, cyan
  cube face left, red target diamond on TIE direction. Nose forward OK.
- 222011: looking back-down: dark mass, gray band horizontal, hull
  close-up, cube corner. All consistent with a seated eye turning head.

### Visible object classification
- stars/planet/moon: A (world-locked) in all 6.
- reticle: D (screen-locked by design: 8 m ahead of EYE, not nose).
- target diamond: A (TIE world pos billboard).
- TIE bolts: A (sim world positions; green bolt = TIE fired <700 m).
- cockpit meshes 0-5 (cage/wings/engines/cannons/arch): B (ship-locked;
  single shared ship-matrix MVP in code; symmetric flanking in photos).
- M1 cube: A (XR-ROOM-fixed at (0,0,-2), ignores ship entirely).
- decal plate / black quad: B by code (same MVP as all cockpit prims);
  visually detached due to seat/materials, NOT a separate transform.
- NOTHING is C (head-locked) by code: no HMD term in any model matrix.

### Head-locked primitives
NONE by construction. Perceived head-lock = (1) REAL view-rotation bug
below during maneuvering + (2) violent near-field parallax (nearest cage
surface 0.29 m from eye, measured in cooked .bin) + (3) dark interior
(ambient 0.35 x dark albedo ~= black) + gray canopy frame filling view.

### Transform hierarchy before fix
- cockpitWorld = ship x optToShip: CORRECT (duel_model_matrix, tested).
- cameraWorld = ship x seat x hmd: CORRECT composition...
- ...but view rotation comes from matrix->quat extraction with
  `else identity` fallback (duel_render.c): combined ship x hmd rotation
  beyond 120 deg from origin SNAPS THE VIEW ROTATION TO IDENTITY while
  the cockpit keeps its correct ship matrix => world freezes to the face
  and ship parts swing/orbit around the view. Matches "de repente se
  centra" (build 2) and "orbit when sticks rotate" (build 3) EXACTLY.

### Root cause
View-matrix eye-rotation extraction fallback, NOT the cockpit/model path.
Fix: exact closed-form eye pose (ship Q x hmd Q, ship pos + shipR(seat +
hmd pos)); delete the extraction. Seat value itself stays per spec.

### Uniform/pipeline audit
- draw_model pushes ONE MVP (vp x model) for ALL prims; per-prim only
  texture binds change => per-prim MVP scatter IMPOSSIBLE; stale-uniform
  scattering EXONERATED by code structure.
- Push slots all 0, match shader decls (render_rules #4 green).
- pipe_lit CULL_NONE/depth LESS_OR_EQUAL/D24; no HUD/NDC contamination
  (pipelines + buffers + samplers rebound at every section).
- NDC HUD bars not visible in photos: watch-item (verify next run).

### glTF node transforms
All cockpit/TIE node TRS identity (measured in JSON); loader bakes
node TRS correctly anyway. Single shared MVP per model is correct.

### Player exterior audit
PLAYER_EXTERIOR_DRAWN=0 by construction: DuelRender holds only
`tie` + `cockpit`; load_model called only for those two. TIE drawn
`if alive`, cockpit always. Log line to be added as proof.

### Audit-time standing puzzles (instrument, don't block)
- Cube-near paradox: world-fixed M1 cube ~1.2 m away across photos
  spanning 51 s while sim min-speed is 40 m/s. Next build logs ship
  pos+yaw at 1 Hz to settle empirically (suspend/clamp vs relaunch).
- USB ADB dropped mid-session (22:3x); WiFi 192.168.68.103:5555 alive.

### New transform hierarchy (implemented, P0.2)
- `duel_math.h`: new `duel_eye_pose()` closed form Q=ship x hmd,
  P=ship+R(ship)(seat+hmd). `DuelRender_Eye` uses it; the trace/identity
  extraction block is DELETED. HMD appears ONLY in camera/view.
- cockpitWorld = ship x optToShip unchanged (was already correct).
- Seat value UNCHANGED per spec (tuning comes after hierarchy proof).

### CPU tests (tests/test_duel.c, +7 = 17 total)
- test_head_does_not_change_cockpit_world / _view_relative (eyespace
  shift 9.94; old-extractor replica asserts the shipped identity snap
  at 150 deg while new code returns the true quat, dot=1.0).
- test_ship_rotates_cockpit_world (nose swing exactly 45 deg),
  test_seat_rigid_to_ship, test_opt_to_ship_basis,
  test_player/tie_muzzle_independent_of_hmd (aim law = ship forward,
  deterministic across runs).
- Expectations cross-checked with an independent Python
  implementation (all 8 numeric probes pass).

### Diagnostic cockpit colors
- No shader compiler on laptop (no glslc/VulkanSDK), so no SPV change:
  6 runtime 1x1 color textures + ambient=1/dir=0/emis=0 via existing
  uniforms = EXACT flat albedo (verified against lit.frag.glsl math).
- Loader records mesh_idx per prim; DUEL_COCKPIT_MESH_MASK=0x3F selects
  meshes (single-bit rebuild isolates one mesh). TIE keeps textures.

### APK
- `XWADuelVR-P0.2-COCKPITSPACE-debug.apk`, 2.07 MB,
  SHA-256 `78DA594E79D188243F9C3A7DAAE2D3989DD4077EB8631A64FB388E93D38814D0`,
  package `org.openxwa.xwaduellvr`, versionCode 1,
  versionName `0.2-P0.2-cockpitspace`, arm64-v8a, NDK 28.2.13676358.
- DUEL_M1_ORACLE=0 (block kept behind define). One-time log
  `DUEL draws: exterior=0 cockpit=1 ...`; fps line extended with
  ppos/pspd/tpos at 1 Hz (settles the cube-near paradox empirically).
- INSTALLED (user-authorized "instala"); verified on-device
  versionName `0.2-P0.2-cockpitspace`. ADB was down at build time; user
  reconnected USB, WiFi re-established at NEW IP 192.168.68.100:5555
  (was .103 — IP drift confirmed).
- Previous APKs preserved (P0 `9482ED29…`, build2 `2B5FEA42…`,
  build3 `6D522B4F…` still installed on Quest).
- Modified: `src/duel_math.h`, `src/duel_render.c`, `src/duel_main.c`,
  `tests/test_duel.c`, `tests/test_render_rules.py`, `app/build.gradle`.

### Verification status (honest)
- `test_render_rules.py`: 9/9 PASS on laptop (new #7 no-exterior,
  #8 no-identity-fallback, #9 uses-duel_eye_pose).
- C suite: 17/17 `ALL DUEL TESTS PASS` on Quest via USB (recompiled
  fresh, -Wall clean). Eyespace shift 9.94 and nose swing 0.785 rad match
  the independent Python probe exactly.
- Math expectations independently verified (see above).

### Quest validation (pending user)
Install P0.2 (needs order + ADB): (1) cockpit = 6 flat colors forming
ONE rigid ship object? (2) head-turn = free-look, no snap/orbit?
(3) send photos + `DUEL draws`/`DUEL fps` log lines.

### Remaining visual issues (post-hierarchy, in order)
Textures restore -> lighting -> alpha/glass -> seat tune. Reticle is
eye-locked by design (revisit: ship-locked gunsight?). HUD bars absent
in build-3 photos (watch-item).

## P0.2b Pitch-mirror fix (2026-10-07)

### P0.2 user run (7 photos, evidence/p0.2-build1/p02_*.jpg)
User verbatim: colored objects rigid but "not attached to anything";
free-look smooth, NO snaps (P0.2 eye fix CONFIRMED working); BUT pitch
inverted (look up => view goes down), yaw fine.
- Photos: mesh1 green (right wing thru canopy opening), mesh4 cyan,
  mesh0 RED cage wall 0.3 m at frame edge (photo 4) => SEAT CORRECT,
  eye inside cage. Mesh5 magenta arch, mesh3 yellow wing visible.
- TIE renders CORRECTLY (ball + panels + red diamond, photo 7).
- "Detached" feeling = flat colors kill depth cues + views through
  canopy openings; geometry itself exonerated (rigid + well placed).
- Black quad STILL unexplained: appears 2/7 photos, texture-independent
  (diag build too), intermittent per view. NaN normals EXCLUDED (7958/7958
  cockpit + 471/471 TIE normals finite+nonzero, scanned .bin). Node
  matrices: none (all TRS identity). Next probe: mesh mask 0x00 shows
  whether it survives with cockpit fully masked out.
- HUD bars STILL absent in all 20 photos to date: watch-item (possibly
  mirror-crop of the 95%-height strip).
- DUEL draws/fps log lines MISSED (logcat rotated; user had exited to
  another app). Lesson: pull logcat within minutes of future runs.

### Root cause: projection Y-sign vs SDL NDC
- SDL_gpu.h "Coordinate System" (ground truth): SDL NDC is Y-UP
  (lower-left (-1,-1), upper-right (+1,+1), Z [0,1]); SDL converts for
  Vulkan internally -- app must output Y-up clip with NO flip.
- duel_frustum_xr had m[5] NEGATED (raw-Vulkan-textbook flip that does
  not apply under SDL_GPU) => vertical mirror => inverted pitch feel
  with correct yaw. Masked in build 3 by the head-glue bug; unmasked
  once P0.2 fixed the view.
- Same audit found m[8] NEGATED (should be +(tR+tL)/(tR-tL) per
  glFrustum): horizontal shift/compression on asymmetric FOVs (measured:
  right edge mapped to +0.507 instead of +1.0 on the test frustum).
  m[0]/m[9]/m[10]/m[14] verified correct (Z row matches SDL Z [0,1]).

### Fix + tests
- duel_math.h: m[5] = +2/(tanU-tanD), m[8] = +(tanR+tL)/(tanR-tanL).
- New test_frustum_asymmetric_signs (edges L/R/T/B + off-center axis):
  6/6 FAIL on old code (authentic failure observed on Quest), exact
  edges (L -1.000 R 1.000 T 1.000 B -1.000) after fix.
- Old Y expectations in test_render_math/test_view_matrix updated to
  SDL Y-up (they encoded the mirrored convention).
- Suite: 18/18 ALL DUEL TESTS PASS on Quest. Rules: 11/11 (new #10 pins
  both signs statically).

### APK
- `XWADuelVR-P0.2-PITCHFIX-debug.apk`, 2.07 MB,
  SHA-256 `2622366A821F642DAA0A209A8C7C2E63A2AB26521E1735294194D9C803E0A154`,
  versionName `0.2-P0.2-pitchfix`. INSTALLED (user-authorized
  "instala"); verified on-device.
- Modified: `src/duel_math.h`, `tests/test_duel.c`,
  `tests/test_render_rules.py`, `app/build.gradle`.

## P0.2b user run (2026-10-07 17:22, pitchfix installed)
User verbatim: PITCH NORMAL (fix CONFIRMED); looking left: incomplete;
looking right: wings almost complete; ahead: green, looks like the seat.
- 3 photos: evidence/p0.2b-pitchfix/pf_*.jpg. Green (mesh1 LEFT wing)
  centered looking left; cyan+red looking down-right; magenta arch +
  red + yellow looking back. NO forward-dashboard photo taken.
- Log gold (pulled in time): fps=72.0 ROCK SOLID; player flies straight
  (ppos 0,0,-66 -> -1465, x=y=0: user never turns, only looks); TIE
  chases to 77 m directly behind, fires, kills player 100->0 in ~5 s;
  DESTROYED freeze + auto-reset VERIFIED (ppos back to origin). Full
  game loop proven end-to-end. AI cycles 0->2->3->4->2 correctly.
- Left = green + BLUE (mesh2, upper). Blue NEVER appears in ANY of 13
  photos (left views show green + stars above) => mesh2 SUSPECTED
  ABSENT (not drawn). Right = yellow + cyan both visible ("almost
  complete" ✓). Geometry per-eye: photos consistent with correct seat.
- "Green ahead like the seat": ambiguous (no forward photo; possibly
  wing-root intruding at forward-left, or ship rolled -- orientation
  was NOT logged in this build).

## P0.2c mesh-audit (no behavior change, instrumentation only)
- Loader logs per-mesh prim histogram + mesh_idx range validation:
  `DUEL model ... meshprims=[..] badidx=N`. Expected cockpit:
  [12,12,11,11,11,6] = 63 (measured in glTF; all prims indexed;
  nodes 1-6 -> meshes 0-5 in order). A 0 entry = mesh can never render.
- fps log extended with ship attitude: pfwd + pup vectors (resolves
  roll/yaw questions for future photos).
- Render rules #11 (2 checks): meshprims log + range validation pinned.
- C suite untouched since 18/18 (no re-run needed); rules 13/13 PASS.
- APK `XWADuelVR-P0.2-MESHAUDIT-debug.apk`, 2.07 MB,
  SHA-256 `4E966C97C81F3DA6FFAFE02CDB71C2C44E722853CDCE7513106E5AD0581FD775`,
  versionName `0.2-P0.2-meshaudit`. INSTALLED (user-authorized
  "instala"); verified on-device.
- Device: logcat ring enlarged to 16 MB (was ~256 KB) so boot lines
  (DUEL model/draws) survive future runs.
- Honest scope note: loader logging is not unit-runnable headless
  (needs SDL+GPU); verification = runtime log on next Quest run.

## P0.2c verdict: device cook is raw-live, loader EXONERATED
- `DUEL model XwingCockpit: 370970 verts 79989 idx 295 prims 143 texs
  49 meshes` -- device loads `assets/raw-live` (49/295/143 match exactly),
  NOT `assets/raw` (6 meshes/63 prims). All P0.2 bbox/eye/seat analysis
  was measured on the WRONG (stale vanilla) file.
- Predicted histogram from raw-live JSON reproduces the device log
  exactly (all 49 entries). Every mesh referenced, single scene root.
- `badidx=114` + `1065353216` (1.0f bits) in the log were MY diagnostic
  bugs, not loader corruption: hist[32] cap flagged meshes 32-48 (their
  baked-prim sum is exactly 114) and the print loop over-read the stack.
- BLUE-missing fully explained, no loader bug: of 8 blue-mod meshes,
  only mesh02 passes the 6-mesh-era mask 0x3F (occluded behind the tub);
  mesh44 (big blue canopy structure) is masked OUT by mi&31 aliasing;
  08/14/20 are distant guns/wings, 26 is above-head, 32/38 are 3 cm dots.
- Green T ahead (pf_172249, ma_181217) = tub nose mesh01 + dash mesh43,
  opaque in diag mode. Eye was buried: model (0,0.80,-0.55) sits 12 cm
  under the fuselage shell top (mesh00 y=0.92). RED slabs = shell
  backfaces (CULL_NONE diag artifact; correct interior behavior).
- Magenta "window cutout" (pf_172307) = real window aperture showing sky.
- Black quad: prime suspect = close-range unlit TIE wing panel (AI chases;
  intermittent, world-locked). UNPROVEN (tpos log lost to rotation).
  Verify in textured build; defer if it resolves into the TIE.
- Logcat 16 MB ring ROTATED in <10 min (Quest system spam ate the fps
  lines). New discipline: `logcat -c` at install, pull DUEL lines FIRST
  (before photos) on every "ya corri".
- Evidence: `evidence/p0.2b-pitchfix/pf_*.jpg` (3),
  `evidence/p0.2c-meshaudit/ma_*.jpg` (2) + device glTFs + audit scripts.

## P0.2d seatfix: live-cook seat + ship-locked gunsight + textures
User report: "la cosa verde de enfrente no deja ver, la mira se mueve
con la cabeza; debe estar a donde llegan los rayos, con la nave."
Changes (`src/duel_render.c` + tests only; sim/AI/input frozen):
1. Seat -> ship (0,+1.05,+0.20) = model (0,1.05,-0.20): above shell top
   0.92, ahead of headrest cluster (z>-0.37), 0.25 m behind dash front.
2. Reticle ship-locked: center = ship_pos + ship_fwd*8, frame =
   ship right/up (roll-aligned). GPU frustum-clips when looking away
   (real gunsight behavior); lines pipeline has no depth test.
3. `DUEL_COCKPIT_DIAG_COLORS 0`: textures + lighting restored.
4. `DUEL_SKIP_BLEND_PRIMS 1` (cockpit only, TEMP): canopy glass
   Tex104/106 avg RGB (16,16,16) would render as an opaque black wall
   through pipe_lit (no alpha blend). Skipped => see-through windshield
   hole until step C (alpha pipeline). Logged as `blendprims=N`.
5. hist[64] + print bound (closes the badidx/over-read artifact);
   mesh mask expression fixed to 64-bit (inert while diag=0).
Tests: C suite 18 -> 19 (`test_reticle_ship_locked`: centered-below on
align, slides left on head-yaw, follows ship-yaw; seat cage updated to
live-cook bounds; analytically safe margins >=0.16). Render rules
13 -> 17 (reticle ship-lock x3, seat/test literal sync). Python 17/17
PASS; C suite 19/19 `ALL DUEL TESTS PASS` verified ON-DEVICE
(`reticle_ship_locked ok ndc 0.000,-0.187 -> head -0.534 ship -0.776`).
APK `XWADuelVR-P0.2-SEATFIX-debug.apk`, 2175035 bytes,
SHA-256 `B41F9B68A91CA00F6F39B22A8D7AC48CE4441173EF0CDDFAFBADBA644F08231A`,
versionName `0.2-P0.2-seatfix` (aapt + dumpsys verified). INSTALLED
(user-authorized "instala"); logcat ring 16 MB + cleared. WiFi ADB
restarted (tcpip 5555, .100 connected); USB + WiFi both live.
Quest validation protocol: install -> `logcat -c` -> run -> pull DUEL
lines FIRST -> photos forward/left/right -> check: dash below sightline,
windshield see-through, reticle follows ship, TIE visible textured.

## P0.2d validation: textured cockpit, ship-lock PROVEN, eye too high
- 3 photos (`evidence/p0.2d-seatfix/sf_*.jpg`): FULLY TEXTURED XWAUP
  cockpit (dash, gauges, sticks, canopy frame, hull rivets); glass-BLEND
  holes correctly see-through (stars through side windows); nose cowling
  + panel lines ahead; red player bolts in flight (trigger works).
- sf_184334 (look right): reticle at LEFT EDGE half off-screen while
  looking away => SHIP-LOCK VISUALLY CONFIRMED. No reticle complaint
  this run => user-accepted. Starboard laser cannons visible (outside).
- User verdict: sticks wrong (see P0.2e) + "no esta la camara dentro
  del cockpit, estoy afuera". Photos agree: eye 1.05 above the canopy
  rim, looking DOWN into the tub (dash-top fills bottom half).
- DUEL ring log LOST again (16 MB rotates in ~4-5 min of Quest spam;
  the "pull FIRST" discipline still missed it). Permanent fix: persistent
  file logger `logcat -f /data/local/tmp/duel.log -r 5120 -n 2 -v time
  -T 1 SDL/APP:V *:S` (chain proven with SELFTEST line; /sdcard failed
  with SELinux AVC denial on rotate ioctl). Protocol: logger runs
  continuously; pull the FILE (primary) + ring (backup) after each run.
- Black quad absent in all 3 textured photos (supports TIE-wing theory:
  unlit dark wing now textured/l readable; keep watch).
- HUD bars still not visible in screenshots (2.5%-height bottom strip;
  likely mirror-crop; in-headset status unknown, low priority).

## P0.2e pitfix: stick swap + eye drop (user spec)
User spec: right stick up/down=throttle, L/R=turn(yaw); left stick
up/down=pitch (kept, "bien"), L/R=roll (what right L/R did).
- `duel_input.c`: yaw lx->rx, roll rx->lx (2 lines + comment); pitch
  -ly and throttle ry untouched (up=+ry=accel already; flight-sim
  pitch sign kept per "bien"). Sim yaw+/roll+ directions unchanged
  (stick-right still turns/rolls right, now on the other hand).
- Seat y 1.05 -> 0.88 (ship (0,0.88,0.20) = model (0,0.88,-0.20)):
  4 cm under shell top, dash top edge ~+8 deg, frame just above.
- Tests: C 19/19 on-device (`reticle_ship_locked ndc 0.000,-0.157 ...`,
  seat cage now y in (0.80,0.92)); rules 17 -> 21 (rule #14 pins the
  4 stick axes in source; input is XR-only, headless-untestable).
- APK `XWADuelVR-P0.2-PITFIX-debug.apk`, 2175035 bytes,
  SHA-256 `BC86E3842D2B26AD6742A09937C1E8901CE312FBBFFA7DA109C920BCE5C80090`,
  versionName `0.2-P0.2-pitfix` (aapt + dumpsys verified). INSTALLED
  (user-authorized "instala"); file logger restarted clean.

## P0.2e validation: file-log works, seat fore-aft error found, 0-5
- File logger delivered the full run (`evidence/p0.2e-pitfix/duel.log`,
  175 DUEL lines): BOOT x2 (first session doffed), hist64 EXACT match to
  prediction (all 49), `badidx=0 blendprims=10`, `draws ... diag=0
  skipblend=1`, fps 72.0 solid, AI states 0/3/4 cycling, reset loop OK.
- Combat score 0-5 (12 death samples): user could not fight back --
  6 deg sight parallax (see below) + swapped sticks. TIE balance
  re-measured only after aim is fixed (no tuning on bad aim).
- Photos 1-2 (`pf_185640/185701`): eye staring at the glareshield lip
  wall (mesh48 top 1.03 just 0.25 m ahead, fills to +35 deg). TIE +
  red diamond visible over the rim (diamond works); planet; bolts.
- Photos 3-4 (`pf_185744/185832`, user leaned back-UP into the seat):
  PERFECT pit -- full panel (radar scope, gauges, switches), stick,
  pedals, canopy bows, windshield with stars + 2 planets. THE TARGET.
- Root cause: P0.2e drop to 0.88 went the WRONG way (height); the real
  error was FORE-AFT (z-0.20 perched on the dash). Height 1.0-1.05 was
  always fine. Fix = back into the seat, not down.
- pf_185744 brown tint = death-flash overlay (works).

## P0.2f gunsight: photo seat + M8 revert + collimated XWA sight
User: copy XWAQuest controls; recenter returns outside; sight must aim
where bolts go; use the XWAUP gunsight.
1. Seat -> ship (0,+1.01,+0.34) = model (0,1.01,-0.34): lip at +3 deg,
   panel ~0.4 m, 4 cm ahead of seat-back front, 1 cm under crown.
   Recenter-proof: correct without any lean.
2. Input reverted to XWAQuest m8integrated1 TouchConvert verbatim
   (yaw=lx pitch=-ly roll=rx thr=ry): the P0.2e swap failed its flight
   test; muscle memory wins. Deadzone/hysteresis/arming already matched.
3. Sight collimated 8 m -> 120 m: v1 parallax was 6.3 deg high (eye
   0.88 over the ship axis); v2 0.4 deg. Same angular size (x15),
   + center dot. No reticle-art texture exists in the cook (Tex88 is a
   lens flare; XWA draws its sight in-engine), so XWA look = procedural
   circle + cross + dot, kept green.
4. Lead pip: green R=4 diamond at `duel_lead_point` (first-order,
   bolt 600 + shooter speed, no lateral inherit -- matches duel_fire),
   same billboard frame as the red TIE box. Aim green to connect.
5. Tests: C 20/20 on-device (new test_lead_point 12.86; reticle ndc
   -0.012 collimated); rules 23/23 (D=120 + lead pins, M8 stick pins).
- APK `XWADuelVR-P0.2-GUNSIGHT-debug.apk`, 2175631 bytes,
  SHA-256 `5C3F964CC21B4F647DDB47C7DECA856803D38A9EAE7EDE334FAAE5EB2EFED04F`,
  versionName `0.2-P0.2-gunsight` (aapt + dumpsys verified). INSTALLED
  (user-authorized "instala"). File logger restarted clean.

## P0.2f validation: sight good, seat meters off (hmd offset, not seat)
- Run 19:19 (48 s, 72 fps): user fired (lasers=12/6 active) but 0 hits
  (thp=100), died once (0-1, better than 0-5). Ship maneuvering hard.
- gs_191928: eye ~5 m BEHIND the cockpit looking forward at it (R2 unit
  + seatback + red S-foil + planet + new gunsight circle/cross/dot all
  visible). gs_191934 (6 s later): face against the dash wall. The user
  walked the room: RAW hmd LOCAL meters added to the seat with no
  neutral reference. The seat constant was never the (main) error --
  all P0.2d/e/f seat zigzag chased an hmd-offset ghost.
- Conclusion: Meta recenter re-zeros LOCAL at the current physical pose
  but the app has no seat anchor for the new origin (zeroing != seating).
  Fix = app-side neutral calibration (P0.4), exactly as tasked.

## Controls + Seat/Recenter Fix (P0.4)
### Current control mapping bug
None in code: GUNSIGHT already = XWAQuest m8integrated1 TouchConvert
verbatim (yaw=lx pitch=-ly roll=rx thr=ry; deadzone/hysteresis/arming
match). The task's swapped symptom matches the PITFIX-era P0.2e build,
already reverted in P0.2f. NO re-swap applied. Full path audited
(OpenXR action -> DuelInput -> DuelSim_UpdatePlayer -> quat).
### XWAQuest control reference
m8integrated1/touch_state.h TouchConvert (read-only): axes[0]=yaw=lx,
axes[1]=pitch=-ly, axes[3]=roll=rx, lever+=ry*dt*0.5, dz 0.15,
hysteresis .45/.55, arming neutral+released. DuelVR matches all.
### Cockpit seat derivation
Kept ship (0,1.01,+0.34) (P0.2f photo+perfect-pit proof). Hardpoint
audit: NO pilot-eye hardpoint exists (`hardpoint_0` x5 = dash/nose
cluster y0.43-0.64 z0.97-1.81, likely emitters; tiny Customs =
guns/engines; none near the eye). Determinism via bounds + runtime
`DUEL_SEAT_CAL` log (computed from baked verts) + bounds tests.
### XWAQuest seat reference
T3_SeatMatrix (read-only): cockpit = ship - (hardpoint + pan/16);
camera = ship x raw LOCAL eye (VrEye_Compose, NO seat in camera);
NO recenter path anywhere (works by launch discipline only).
### Model A/B decision
KEPT Model A (camera = ship x seat x hmd_cal). A+calibration is
mathematically identical to B+calibration; B buys nothing and would
invalidate the camera test suite. XWAQuest's B works by luck of origin.
### Initial HMD calibration
`DuelSeatCal` (neutral_pos + neutral_yaw + valid; pure math in
duel_math.h): capture at combat entry after 150 valid frames (~2 s
donning settle), yaw-only orientation (pitch/roll stay live), position
rebased AND de-rotated into the seat frame. Spawn gaze BECOMES ship
forward (dot 1.0000 in test).
### Reference-space change handling
`DuelXr_PollEvents` (already per-frame) now latches
REFERENCE_SPACE_CHANGE_PENDING; `DuelXr_ConsumeSpaceChange()` single
consumer in render: drop neutral, re-capture 30 frames after the jump.
### Meta recenter behavior
Current spot becomes the seat: corrected pos ~= 0, yaw ~= 0 at capture.
Ship/cockpit never move (cockpitWorld recenter-invariant, tested).
ResetCombat edge re-captures immediately (user seated, poses continuous).
### Control tests
C 20 -> 25 on-device ALL PASS: test_input_axes (yaw/pitch/roll/
throttle isolation + determinism), test_neutral_head_inside_cockpit
(§8 bounds), test_initial_heading (dot 1.0000 + 5 m vanish),
test_free_look_calibrated (+45 deg, ship untouched), test_recenter
(~0 + cockpit rigid + lean de-rotation). Rules 23 -> 28 (sim/head
separation, neutralize wiring, per-frame drive, space-change, SEAT_CAL).
### Seat tests
See above + `DUEL_SEAT_CAL` runtime line (eye/bounds/instrument
centroid/checks DBC LX) computed from the live baked verts every run.
### Muzzles
Re-verified ship-only (player_muzzle/tie_muzzle tests green, §11 grep
clean via sim-separation rule).
### APK
`XWADuelVR-P0.4-SEAT-CONTROLS-debug.apk`, 2177219 bytes,
SHA-256 `29A186EA59624273D750767839BF080D86ACF613747284C57BE88912749BE36A`,
versionName `0.4-P0.4-seat-controls` (aapt + dumpsys verified).
INSTALLED (user-authorized "instala").
Input telemetry added: fps log carries raw=(lx,ly,rx,ry) + ypr mapped
(proves the live mapping on-device, settles the swap question).
File logger restarted clean.
### Quest validation
Pending: TEST A start seated/forward, B free-look, C left yaw+pitch,
D right roll+throttle, E Meta recenter to seat, F reset re-entry.

## Next step
ONE: user orders "instala"; install P0.4, validate A-F in HMD, pull
duel.log (DUEL_SEAT_CAL + DUEL cal lines) + photos.

## P0.5 Pilot Seat + Control Frame

### Latest user photo interpretation
P0.4 photos (`evidence/p0.4-seat-controls/sc_200024.jpg`, `sc_200058.jpg`):
eye outside/behind the cockpit staring at the glareshield lip; user
places the camera "where the astromech sits, behind the cockpit". The
photo-derived P0.4 seat is REJECTED: physical evidence overrides it.

### Current effective eye position
P0.4 seat ship (0,1.01,+0.34) = model (0,1.01,-0.34); calibration
neutralizes to ~0 (P0.4 log), so effective eye == seat exactly.

### Live XWAUP pilot/astromech geometry
LIVE asset only: `assets/raw-live/XwingCockpit/XwingCockpit.gltf`,
SHA-256 `BBD1A1CC...1FF8B`, 233885 B, 49 meshes / 295 prims / 143 tex,
identity nodes (verified). Audit: `evidence/p0.5-seat-frame/seat_geom_live.py`.
R2 cluster mesh24-29: z -0.78..-0.37, y 0.94..1.35 (mesh24 Tex42 blue dome,
mesh25 Tex47 red ring; front face z=-0.37/-0.38). Panel mesh41: z 0.55..1.78,
top y=0.75. Glareshield mesh02 top y=0.50. Roof mesh44/48 y=1.02..1.03.
Seat-back zone z~0.06..0.30, top y=0.92. Exterior: no XWAUP OPT in
workspace (cook was on-device); vanilla `assets/raw/XwingExterior` used as
topology advisory only (R2 behind pilot confirmed, numbers not transferred).

### Pilot-eye derivation
P0.4 root cause: the "-0.38 seat-back front" landmark IS the R2 front
face -- the pilot's face sat 3 cm from the droid, inside its vertical
span. Correct eye MODEL (0,0.85,+0.38) -> SHIP (0,0.85,-0.38): 75 cm ahead
of R2, ~15 cm ahead of the backrest, 17 cm behind the panel rear while
ABOVE the panel top (0.75), 18 cm below the roof, 35 cm above the
glareshield; backrest top (0.92) just over the eye = reclined pilot.
`DUEL_SEAT_CAL` checks rewritten to the true landmarks (PROX letters).

### HMD calibration discriminator
`DUEL_CAL gen=N reason=...` log proves, at each capture, raw vs corrected
(~0 expected): CASE A (neutralization broken) vs CASE B (anchor wrong).
Includes seat, cam_ship (= seat + corrected) and cam_world; ship/cockpit
never move under calibration.

### Calibration generation audit
`DuelSeatCal` += generation (capture count) + reason (INIT/RECENTER/RESET).
Reasons wired at the single capture site; unit asserts pin gen/reason.

### Control mapping
M8 verbatim, frozen, re-verified in `duel_input.c`: yaw=lx, pitch=-ly,
roll=rx, throttle=ry. Render-rules M8 checks still green.

### Quaternion multiplication audit
Convention Hamilton, v' = q v q*. P0.4 `qyaw*(qpitch*(qroll*orient))` is
EXTRINSIC world-axis (the "intrinsic-ish" comment was wrong): exact at
identity, wrong after any maneuver -- the wandering roll axis. P0.5
post-multiplies: `orient = orient * (qyaw*(qpitch*qroll))` (body-local;
associativity makes the combined delta exact for pure inputs). Signs and
axis responsibilities unchanged.

### Local-axis invariants
`test_local_axis_invariants`: 20 deterministic nontrivial orientations
(i=0 is exactly yaw37/pitch28/roll19); pure roll preserves current
forward, pure pitch preserves current right, pure yaw preserves current
up, each with a motion guard. RED on P0.4 code (64 on-device failures),
GREEN after the fix.

### Camera-relative-to-ship invariant
`test_camera_ship_local_stable`: inv(ship)*camera constant across a stick
rotation with fixed head, equals seat+hmd_p (the builder adds the
neutralized offset in ship frame). PASS before and after (eye builder was
already correct). Render rule added: render never reads sticks (§24).

### Root causes
1. Seat: R2 cluster mistaken for the seat-back (landmark error, not a
   tuning error). 2. Controls: world-axis pre-multiplication (frame bug,
   not a mapping swap). 3. Calibration: exonerated (neutralizes to ~0,
   no unexpected recaptures).

### APK
`XWADuelVR-P0.5-PILOTSEAT-CONTROLFRAME-debug.apk`, 2179171 bytes,
SHA-256 `98A53AF8929304EFB0020A036051343176B974270165743389EA4C3DF425A50F`,
package `org.openxwa.xwaduellvr`, versionCode 1,
versionName `0.5-P0.5-pilotseat-controlframe` (aapt verified; fresh .so
confirmed via DUEL_CTRL/DUEL_CAL strings in the zip). Prior APKs preserved.
Git: no commits yet (all untracked); no destructive ops. Tests: C 27/27
ALL DUEL TESTS PASS on-device (Quest 3487C10H9T0BKZ); rules 32/32.
NOT INSTALLED (awaiting user order).

### Quest validation
Pending: TEST 1 seated pilot view (dash below/front, canopy around, space
ahead -- NOT the droid); 2 head free-look rigid; 3 pure yaw; 4 pure roll
stable from arbitrary attitudes (before/after yaw+pitch); 5 Meta recenter
to pilot seat; 6 reset re-entry. Pull duel.log: DUEL_CAL gen/reason +
corr~0, SEAT_CAL PROX, DUEL_CTRL basis lines.

## Next step
ONE: user orders "instala"; install ONLY the P0.5 APK, run TEST 1-6 in
HMD, pull duel.log + photos.

## P0.5b Seat +40cm (user-ordered)

### P0.5 physical result
User in HMD: "casi dentro, quede DENTRO del asiento" -- the P0.5 eye
(0,0.85,0.38) sits inside the mesh45 seat back/headrest box (x +-0.29,
y 0.55..1.00, z 0.32..0.73). Passed its own PRCOX checks yet inside
geometry: box checks are necessary, not sufficient. Log pulled to
`evidence/p0.5-seat-frame/duel.log`: SEAT_CAL eye=(0,0.85,0.38) PRCOX;
DUEL_CAL gen=1..3 (RECENTER,RECENTER,RESET -- all legitimate) with
corr_pos=(0,0,0) corr_yaw=0.00 EXACT: neutralization proven live,
CASE B confirmed (anchor was the only error). 0 DUEL_CTRL lines (sticks
never passed 0.25 in that short seat-evaluation run).
### Fix
User order: camera +40 cm forward, exactly. Seat SHIP (0,0.85,-0.78) =
MODEL (0,0.85,+0.78): 5 cm ahead of the seat-back front (0.73), 10 cm
over the panel top, 18 cm under the roof. SEAT_CAL checks rewritten to
the over-panel reality (SWCOX: seat-back / windshield / roof /
over-panel / center). Nothing else touched (rotation fix, M8, cal intact).
### Tests/APK
C 27/27 on-device, rules 32/32.
`XWADuelVR-P0.5-SEATFWD40-debug.apk`, 2179179 bytes,
SHA-256 `7981B6E0EF0573F3E8714F6107682733A3631A8D45E71DC7E2D60E9679104564`,
versionName `0.5-P0.5-seat-fwd40`. NOT INSTALLED (awaiting order).

## Next step
ONE: user orders "instala"; install ONLY the P0.5b APK, verify seated
pilot view (seat behind, panel below, canopy around), then fly TEST 3-4
(roll axis from arbitrary attitudes) with sticks so DUEL_CTRL logs.

## MILESTONE 1 ACHIEVED (user-validated in HMD)
P0.5b physical test: RESOUNDING SUCCESS. Pilot correctly seated in the
X-Wing cockpit (seat behind, panel below/front, canopy around, space
ahead), M8 controls + body-local yaw/pitch/roll verified by feel.
FIRST MILESTONE COMPLETE. Pull duel.log + any photos to evidence/ as
final record when convenient.

## Next step
ONE: celebrate; then user picks the next milestone (candidates in FUTURE.md).
