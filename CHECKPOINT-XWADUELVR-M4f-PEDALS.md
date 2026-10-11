# CHECKPOINT XWADuelVR M4f — FEET STRETCHED TO THE PEDALS

Date: 2026-10-10 ~00:40. Device: Quest 3S `3487C10H9T0BKZ`.
APK: `XWADuelVR-M4f-PEDALS-debug.apk` (2241919 B,
SHA256 `E5A201F28D9B84ABACE232FF5A8A351A65CFBFEC51E71504F5383764B352A957`),
`versionName=3.17-M4f-pedals`, installed `install -r`.
No asset changes. Prior builds/APKs preserved; no reset/clean.

## Visor report on M4e (user-tested)
- Rays GONE (touch menu clean).
- Leg SIZE fine, but feet don't reach the ship's pedals: pilot
  should stretch the legs more (or sit forward). Chose stretch:
  moving the seat forward would perch the buttocks off the pan
  (hips already 7 cm ahead of the backrest front).

## M4f changes (leg quats only, scale stays 0.90)
- Ankles (+-0.170,0.200,1.410) (was 0.144,0.235,1.322),
  knees IK (+-0.091,0.375,1.025), toes (+-0.201,0.109,1.537),
  tips ~1.57: braced ~1-2 cm off the firewall face (mesh01 hit
  at (0.214,0.067,1.580) along the toe ray; wall is lumpy).
- Knee flexion eased ~15 deg (more extended, X-wing-like).
- Verified vs live cockpit: all joints CLEAR (4.5-4.8 cm);
  eye box +-8 cm: 0/27 except toe 1/27 (extreme corner);
  segments free; headroom clear. Residual risk: outer toe
  corner may graze a wall lump by ~1 cm in the dark corner.

## Verification (host + device, all green)
- `tests/test_render_rules.py`: 154/154 PASS.
- Device CPU suite `/data/local/tmp/test_duel_m4f`:
  `ALL DUEL TESTS PASS` (new ankle asserts + quat pins).
- Device bake probe: joints exactly on targets, boots
  y 0.10..0.37 / z 1.21..1.60, no NaN.

## Visor test (PENDING user validation -- not claimed)
1. Launch XWADuelVR. Look down into the well: boots extended
   forward, toes near the front wall/pedals (not dangling short).
2. Legs should read as comfortably stretched, not cramped.
3. Report: reach the pedals visually? toe clip into the wall?
   If clean, stage 1 is DONE -> stage 2 arms (follow hands).
