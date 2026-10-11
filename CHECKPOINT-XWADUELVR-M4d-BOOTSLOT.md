# CHECKPOINT XWADuelVR M4d — BOOTS INTO THE VISIBLE SLOT

Date: 2026-10-10 ~00:10. Device: Quest 3S `3487C10H9T0BKZ`.
APK: `XWADuelVR-M4d-BOOTSLOT-debug.apk` (2242423 B,
SHA256 `EE9B237EF3A3C77274161298A4BB60A0F7D61D51617D80B112643A8ACE21421F`),
`versionName=3.15-M4d-bootslot`, installed `install -r`.
No asset changes (Pilot.* on device unchanged from M4c).
Prior builds/APKs preserved; no reset/clean.

## Visor report on M4c (user-tested)
- Body WITH colors (M4c texture fix works). No arms, hands only
  (stage 1 as designed).
- Feet NOT visible.

## Root cause
- Ankle + midfoot were robustly visible (27/27 eye-box), but the M4c
  flat-foot fix moved the TOE to (0.234,0.039,1.621), behind the
  console-lip wall (mesh01 hit at (0.217,0.091,1.566)): toe blocked
  27/27 even from the design eye. Never re-verified after M4c (gap).
- Full visibility atlas of the footwell (x 0.15/0.20/0.25, y/z grid)
  shows a visible low channel at x~0.15-0.20; x=0.25 is walled off.
  Lesson: verify the WHOLE foot (ankle+mid+toe) across an eye box,
  not single rays from one eye.

## M4d changes (`src/duel_pilot.c` leg quats only)
- Ankles (+-0.16,0.25,1.38) (up-back into the wide channel slot),
  knees IK (+-0.084,0.458,0.956), toes (bind orientation)
  (+-0.194,0.149,1.521). Feet dangle relaxed, soles down-forward.
- Verified vs live cockpit (20949 tris): knee/ankle/mid/toe ALL
  CLEAR with ~5 cm margins; eye box +-8 cm: 0/27 blocked on all
  four; thigh/shin/foot segments clip-free; 25 cm knee headroom;
  soles ~18 cm over the hull; side wall 8 cm outboard; stick
  cleared by 14 cm.

## Verification (host + device, all green)
- `tests/test_render_rules.py`: 153/153 PASS.
- Device CPU suite `/data/local/tmp/test_duel_m4d`:
  `ALL DUEL TESTS PASS` incl. `pilot_boot` with new ankle asserts.
- Device bake probe: joints exactly on targets, boots bounds
  y 0.14..0.44 / z 1.16..1.60, no NaN.

## Visor test (PENDING user validation -- not claimed)
1. Launch XWADuelVR. Look down ~30-45 deg: thighs, knees, shins,
   and now BOOTS (dark, low in the footwell, toes forward-down).
2. Knees sit slightly higher/closer than M4c (deeper look-down).
3. Start combat: body rides with the ship; hands/levers unchanged.
4. Report: boots visible? recognized as boots? clips? If clean,
   stage 1 is DONE -> stage 2 arms (follow hands, no dup hands).
