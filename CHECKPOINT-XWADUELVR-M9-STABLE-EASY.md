# M9 STABILITY + EASY COMBAT (2026-10-09/10, código + APK, validación física pendiente)

Status: BUILT, host-verified. NOT installed (other session may own the
Quest; install/run needs a coordinated window). Milestone NOT
physically validated: user tests in visor.

Session scope: XWADuelVR ONLY. XWAQuest + shared C:\OpenXWA libs are
read-only in this session (shared .so files only COPIED from, never
modified). No reset/clean, no overwritten builds.

## 1. Preservation

- Git `d44c46f` + uncommitted M9 work (8 files, left for review):
  `app/build.gradle`, `src/duel_main.c`, `src/duel_menu.h/.c`,
  `src/duel_profile.c`, `src/duel_sim.c`, `tests/test_duel.c`,
  `tests/test_render_rules.py`. Plus untracked `evidence/pilot-inbox/
  CREDITS.md`, `evidence/test_duel_m9`, this checkpoint.
- Installed on Quest 3487C10H9T0BKZ (verified 2026-10-10, read-only):
  `v3.7-M7-menu-b` (M7-MENUB), first install 2026-10-06, last update
  2026-10-09 10:00. No DuelVR install performed this session.
- Prior APKs intact with frozen sizes: M7-MENUB 2234735, M6 2218055,
  M3.8c 2212083 (bytes, verified).
- Controls (frozen, unchanged): right lever pitch + pure yaw, left lever
  throttle (holds) + roll, release -> neutral except throttle; free grab
  either hand; touch-fire thumb-on-index; head look-cue + diamond + lead
  pip follow auto-target. Player ship constants untouched.

## 2. Log review (freeze/close report)

- `dumpsys dropbox`: NO DuelVR crash/ANR entry (only XWAQuest entries).
- `logcat -d`: last DuelVR session 18:47-18:49 ran ~105 s, then a CLEAN
  activity CLOSE ("Duplicate finish request", switch to vrshell) and a
  later routine empty-kill. Zero DUEL log lines survive in the buffer.
- Verdict: the available logs do NOT contain the failure. No deterministic
  crash/freeze found in code review either (menu/sim/input/XR/audio paths
  audited; panel anchor, hover, pinch, focus, swapchain acquire/release,
  file I/O all bounded; no dangling pointers found).
- Repro plan: M9 adds event logs (menu click screen/row/act, pause reason,
  resume, quit-to-menu, exit-requested, panel re-place) + keeps the 1 Hz
  DUEL fps heartbeat, so a freeze vs quiet vs clean-exit is
  distinguishable in the next capture. Capture protocol in §7.

## 3. Stability + menu fixes (M9A)

1. Highlight == action: new `DuelMenu_Highlighted()` (hover when
   pointing, else sel); trigger/menu-button activates the VISIBLE
   highlight (was: sel always, highlight could disagree).
2. Sticks take over from stale hover: first stick edge syncs sel=hover
   then clears hover, so the last-used input owns the highlight.
3. Prefs saves throttled: slider drags applied live, persisted at 2 Hz,
   flushed immediately on any screen transition (was: file write per
   frame while dragging).
4. Boot panel re-place: the first panel is placed before seat-cal capture
   (~2 s settle); when cal generation lands (0->N), the panel is
   re-placed ONCE from the current head pose (fixes pre-cal misplaced
   panels; one-shot, world-locked design kept).
5. Simple native menu kept (no original-XWA menu integration, per order).

## 4. Easy combat by default (M9B)

- Selector default FACIL: `DuelMenu_Init` + new-pilot `last_diff` =
  ROOKIE. Engine baseline stays PILOT (`Duel_Init` diff=1, pinned by
  rule; START always applies the menu value). Existing pilots keep
  their recorded `last_diff`. No pilots exist on device yet.
- Rookie-only AI envelope (`s->diff <= 0` branches; pilot/ace identical):
  - Mults: turn 0.8->0.65, cooldown 1.3x->1.6x (HP 0.7x / speed 0.85x
    unchanged: fair fight, not target practice).
  - Breaks earlier/longer: evade 80 m->130 m, 2.0 s->3.0 s.
  - Runs through passes: close-chase throttle 0.5->0.65, attack
    throttle 0.70->0.85 (overshoots cross the nose, no tail glue).
  - Calmer guns: fire cone dot 0.965->0.980 (~15->~11 deg), range
    700 m->500 m (entry + fire + fallback consistent).
  - Crossing reacquire: steers to a point 250 m ahead of the target's
    nose +/- 150 m lateral (per-ship side) instead of beelining;
    exits on facing>0.9, dist<150 m, or 6 s cap (no stuck state).
- Enemy indication kept: head arrow + diamond + lead pip + HP bar follow
  auto-target; player controls/response untouched.

## 5. Tests

- `tests/test_render_rules.py` (host): 137/137 PASS (130 kept + 7 M9:
  highlight activation, stick takeover, prefs throttle, panel
  re-place, easy default, rookie-only envelope, crossing exits). One
  M5 rule updated to pin the rookie-gated attack throttle ternary
  (baseline 0.70 preserved).
- `tests/test_duel.c`: +2 tests (`menu_highlight`, `easy_envelope` with
  4 rookie-vs-pilot behavior cases); `menu_rows` default expectation
  updated PILOT->ROOKIE (requested behavior change). Compiled for
  Android arm64 (`evidence/test_duel_m9`, 174864 bytes, EXIT 0).
  DEVICE RUN PENDING (coordinated window): push + run + expect
  ALL DUEL TESTS PASS (101 tests).
- Native `libXWADuelVR.so` rebuilt (M9 strings verified in the binary
  AND inside the APK; staged .so identical size).

## 6. Build

- `XWADuelVR-M9-STABLE-EASY-debug.apk`, v3.8-M9-stable-easy,
  2235551 bytes,
  SHA-256 `D4A9C363C7ADB39C113C2717D0C40433946D2E17B409490036823622FAC055DB`.
- Built: Build-Target.ps1 (native) + cached gradle-8.5 offline
  assembleDebug -x lint (BUILD SUCCESSFUL). No XWAQuest wrapper used.
- INSTALLED 2026-10-09 20:02 (`install -r`, prefs/pilots intact):
  dumpsys confirms `versionName=3.8-M9-stable-easy` on 3487C10H9T0BKZ.
  CPU suite on device: ALL DUEL TESTS PASS (incl. `menu_highlight`,
  `easy_envelope`). App label in Quest: "XWADuelVR" (Library ->
  Unknown Sources). Physical validation: PENDING user visor run (§7).

## 7. Visor test protocol (user, after coordinated install)

1. Boot: menu appears readable 1.1 m ahead within ~3 s (watch for the
   one-time re-place jump; report if the panel starts misplaced).
2. Navigate with sticks AND with point+pinch: highlight always matches
   the activated row; sliders drag smoothly (controllers: stick moves
   highlight, trigger/menu activates).
3. BRIEF shows DIFICULTAD: FACIL by default. INICIAR -> combat.
4. Combat (easy): TIEs make visible passes across the nose; none glued
   to the tail; head arrow + diamond + lead pip track the target;
   TIE fire feels calmer. Player handling identical to M6.
5. Pause (menu button) -> REANUDAR / REINICIAR / OPCIONES / MENU; RESULT
   -> REPETIR / MENU. Then SALIR from main menu (clean exit expected).
6. If anything freezes/misbehaves: NOTE the wall-clock time + what was
   on screen, keep the HMD on, and tell Muse BEFORE closing, so logs
   (DUEL click/pause/resume/fps lines) can be pulled fresh.

## 8. Pilot body audit (M4 prep, files present, NO code yet)

- Source: `evidence/pilot-inbox/source/
  sw-x-wing-rebel-flight-suit-rigged.glb` (8.06 MB) + `textures/*.jpeg`
  + `LICENSE-pilot.txt.txt` (CC-BY 4.0, tegnemaskin, https://skfb.ly/oNNPt).
  Attribution registered in `evidence/pilot-inbox/CREDITS.md`.
- Format: valid glTF 2.0 binary (Blender 3.6.27 export), self-contained
  (geometry + skin + 5 embedded JPEGs ~5.9 MB in one BIN chunk).
  extensionsRequired=KHR_texture_transform is used ONLY on the
  metallicRoughness texture (loader-ignored): safe. Transmission/ior on
  the visor (stage-1-hidden): safe.
- Skeleton: Mixamo rig, 52 joints, full fingers (3 per digit), arms,
  spine chain, legs/feet/toes, neck/head; inverseBindMatrices present.
  No animations (seated pose must be procedural/static).
- Meshes: 10 parts, ~34.8k tris total (boots 4.5k, gloves 5.6k, helmet
  7.6k, suit 3.7k, harness 4.9k, vest 1.3k, databox 2.3k, leg-thingy
  2.6k, arm-rods 1.7k, visor 0.6k rigid). 9 skinned, visor rigid under
  Head. Quest-light.
- Textures: atlas diffuse/normal/metal-rough + visor normal +
  imperfections; main material double-sided.
- Stage-1 fit notes: suit is ONE skinned mesh incl. arms -> arms need
  static seated bone angles (or joint-collapse) even in stage 1; hide
  helmet/visor (+gloves until stage 2); seat/HMD/cal untouched; platform
  hand meshes never double-drawn (gloves hidden while hand meshes live).
- Next (after stability/easy validate): M4 integration milestone.

## Next step

ONE: user runs the §7 visor protocol on installed M9 and reports
(menu, easy combat feel, pause/restart/result/exit). No physical
validation declared until the user reports. If freeze/misbehavior:
note time + screen, keep HMD on, tell Muse before closing.
