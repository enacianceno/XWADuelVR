# CHECKPOINT XWADuelVR M4 — PILOT BODY STAGE 1 (seated torso/legs/boots)

Date: 2026-10-09 ~22:40. Device: Quest 3S `3487C10H9T0BKZ`.
APK: `XWADuelVR-M4-PILOT-debug.apk` (2242431 B,
SHA256 `01667221E79D0C8BCFA014D9FA4B36CB71CCA499D682EC343938C588B2F19B18`),
`versionName=3.12-M4-pilot`, installed `install -r` (data preserved).
Prior builds/APKs preserved; no reset/clean.

## What M4 adds
- Seated pilot body baked ONCE at load into a static VBO in ship space;
  rides the same ship matrix as the cockpit (`draw_pilot` next to the
  cockpit draw; no per-frame skinning cost).
- Stage-1 mesh whitelist: `suit` (arm tris cut), `boots`, `harness`,
  `vest`, `databox`, `leg-thingy`. Helmet/visor/gloves/arm-rods never
  load. Head collapsed to 0.001 scale (hidden in first person).
- Seated pose (solver-baked local quats): hips on seat
  `(0,0.10,-0.80)` ship, spine -8°, knees `(±0.20,0.24,-1.18)`,
  ankles `(±0.285,0.091,-1.626)` (ship frame, forward = -Z).
  Root `Ry(PI)+off(0,-0.960038,0.793693)`.
- Suit tris with ≥2/3 arm-dominant verts are skipped (stage 2 will add
  arms following the hands; no duplicate hands — platform hands
  untouched, HMD eye untouched).
- Optional load: if `Pilot/` files are absent the cockpit flies empty
  and the ready gate is unaffected (`DUEL Pilot missing` log).

## Source model + credits
- `evidence/pilot-inbox/source/sw-x-wing-rebel-flight-suit-rigged.glb`
  (8.06 MB, glTF 2.0, Mixamo 52-joint rig, 10 meshes ~34.8k tris).
- Attribution: `evidence/pilot-inbox/CREDITS.md`
  (tegnemaskin, CC-BY 4.0, https://skfb.ly/oNNPt).
- Device files converted by `evidence/pilot-inbox/convert_pilot.py`:
  `assets/pilot/Pilot.gltf` (29031 B), `Pilot.bin` (8424848 B),
  `Pilot.jpg` (1996185 B) → pushed to
  `files/duel_assets/Pilot/` via `/data/local/tmp` + `run-as cp`
  (sizes verified before AND after `install -r`).

## Verification (host + device, all green)
- Native `libXWADuelVR.so` rebuilt with `src/duel_pilot.c` (2.03 MB).
- `tests/test_render_rules.py`: 150/150 PASS (147 + 3 new M4 rules:
  static bake at load, stage-1 whitelist, head collapse + arm defer).
- Device CPU suite `/data/local/tmp/test_duel_m4`:
  `ALL DUEL TESTS PASS` incl. `pilot_pose`, `pilot_root`, `pilot_skin`.
- Knee/ankle clearance verified against 370970 cockpit verts (probe
  scripts in /tmp: `pilot_seat.py`, `pilot_clear.py`).

## Visor test (PENDING user validation — not claimed)
1. Launch XWADuelVR from the Quest library (label `XWADuelVR`).
2. At the menu, look DOWN: expect orange flight-suit torso, legs to
   the rudder area, boots; NO helmet/head in view.
3. Start combat (Easy default): body rides with the ship; hands and
   levers behave exactly as M12; HMD view unchanged.
4. Report: clipping through seat/console? floating knees? seam at
   shoulders where arms will attach? If clean → stage 2 arms.

## Risks / known stage-1 limits
- Arm-cut shoulder seam visible (expected; stage 2 covers it).
- `KHR_texture_transform` on metallic-roughness ignored (matte suit).
- Pose CPU-verified only; real-body fit confirmed by look-down test.
