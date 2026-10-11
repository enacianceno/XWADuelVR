# CHECKPOINT XWADuelVR M4b — PILOT FRAME FIX + IK LEGS (raycast-verified)

Date: 2026-10-09 ~23:30. Device: Quest 3S `3487C10H9T0BKZ`.
APK: `XWADuelVR-M4b-PILOTFRAME-debug.apk` (2242415 B,
SHA256 `03B7A73DE0572DEE0FAFA78FD318E5CE9A5623D933E147DD6F8E689C8853EBA5`),
`versionName=3.13-M4b-pilotframe`, installed `install -r`.
Pilot asset files unchanged on device (`files/duel_assets/Pilot/`).
Prior builds/APKs preserved; no reset/clean.

## Why M4a was invisible (root cause, definitive)
- All model VBOs hold MODEL-frame verts (+Z nose); `duel_model_matrix`
  (= ship * Ry(PI), see `duel_math.h:154-161`) applies the OPT->ship
  half-turn at draw. The M4a pilot bake ALSO applied Ry(PI) in
  `DuelPilot_RootMatrix` -> double half-turn -> the body drew 1.6 m
  BEHIND the seat, buried in the tail (model z=-0.80, behind the R2
  unit). Invisible by construction; hands/menus unaffected.
- Why tests missed it: the bake probe verified VBO *contents* (correct
  ship-frame numbers) but never the draw composition (VBO frame x
  `duel_model_matrix` basis). Fixed by `test_pilot_frame` (full chain).

## M4b changes (`src/duel_pilot.c`, `src/duel_pilot.h`)
- `DuelPilot_RootMatrix` is now a PURE translation
  `(0,-0.960038,+0.793693)`: hips bind `(0,1.0600376,0.0063065)` lands
  on the seat pan `(0,0.10,0.80)` MODEL frame; draw turns it to ship
  `(0,0.10,-0.80)` under the validated eye `(0,0.75,-0.88)`.
- Legs re-solved (analytic 2-bone IK, min-twist branches, FK-verified):
  knees `(+-0.109,0.384,1.080)`, ankles `(+-0.20,0.14,1.48)`, toes
  relaxed-point `(+-0.02,-0.35,0.94)`. Spine -8 deg and collapsed head
  unchanged. Arms still cut for stage 2.
- Placement verified against the LIVE cockpit (`assets/raw-live`,
  20949 occluder tris, glass excluded) with exact eye raycasts:
  chest/shoulders/thighs/knees/shins/ankles/toes ALL CLEAR from the
  eye (look-down 42-103 deg, knees at 61); thigh/shin/foot segments
  clip-free (8 cm margin); 25 cm headroom over knees; boot soles
  11 mm above the hull; pelvis naturally tucked behind the console
  rear; stick (x=0,z=1.27) cleared by 15 cm.

## Verification (host + device, all green)
- `tests/test_render_rules.py`: 151/151 PASS (+1 M4b rule: root has no
  yaw, draw owns the half-turn).
- Device CPU suite `/data/local/tmp/test_duel_m4b`:
  `ALL DUEL TESTS PASS` incl. new `pilot_frame` (bind->root->opt2ship
  = ship seat; model z>0 nose-side guard) and pinned IK quats.
- Device bake probe (real C code + real Pilot files): joints land
  exactly on targets, all 6 meshes clean, no NaN.
- NOTE: the app emits NO logcat lines on this Quest (verified: zero
  lines from the app pid over a full session), so placement was proven
  geometrically (raycasts + FK + bake probe), not via logs.

## Visor test (PENDING user validation -- not claimed)
1. Launch XWADuelVR. Look slightly down: knees (orange) low in view.
2. Look down ~30-45 deg: thighs, shins, boots in the footwell; chest
   below the chin at deeper look-down. NO head/helmet in view.
3. Start combat (Easy default): body rides with the ship; hands and
   levers exactly as before.
4. Report: any clipping (seat/console/panel)? floating feet? twisted
   thigh (candy-wrap)? shoulder seams? If clean -> stage 2 arms.
