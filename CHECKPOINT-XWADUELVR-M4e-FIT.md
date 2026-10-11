# CHECKPOINT XWADuelVR M4e — SMALLER FIT + NO FINGER RAYS

Date: 2026-10-10 ~00:20. Device: Quest 3S `3487C10H9T0BKZ`.
APK: `XWADuelVR-M4e-FIT-debug.apk` (2241919 B,
SHA256 `EE039847667BB655C4372B1968A20E49A3357750DBA10E919C5210FA5D2105E5`),
`versionName=3.16-M4e-fit`, installed `install -r`.
No asset changes (Pilot.* on device unchanged from M4c).
Prior builds/APKs preserved; no reset/clean.

## Visor report on M4d (user-tested)
- Legs + feet VISIBLE with colors; touch menu great.
- Body feels like "a very big person in a very small space".
- Arms still missing (stage 2, expected).
- Index-finger rays still show on both hands: must go (touch UI).

## M4e changes
1. FIT: root now scales 0.90 uniform ABOUT THE HIPS
   (`DUEL_PILOT_SCALE`, t = HT - s*HB): hips stay exactly on the
   pan (0,0.10,0.80); shoulders drop to y~0.57 (18 cm under eye);
   knees (0.076,0.422,0.941), ankles (0.144,0.235,1.322), toes
   (0.175,0.144,1.449). Pose quats untouched (angles are
   scale-free). Tune: change the one #define (0.85-0.95 sane).
2. RAYS: finger-ray aim visuals fully removed (compute + draw +
   struct fields + stale comments). Touch needs no aim aid.
   Verified: zero `menu_ray` occurrences in duel_render.c.

## Verification (host + device, all green)
- Scaled placement raycast vs live cockpit: knee/ankle/mid/toe/
  chest/shoulder ALL CLEAR (3.8-4.4 cm margins); eye box +-8 cm:
  0/27 blocked except toe 1/27 (extreme slouch corner only);
  segments free; headroom clear; soles over hull.
- `tests/test_render_rules.py`: 154/154 PASS (M4e scale pins +
  rays-gone rule; M4b pure-translation rule superseded).
- Device CPU suite `/data/local/tmp/test_duel_m4e`:
  `ALL DUEL TESTS PASS` (new root values, frame chain still
  lands on pan, boot test with scaled ankle asserts).
- Device bake probe: joints exactly on scaled targets, no NaN.

## Visor test (PENDING user validation -- not claimed)
1. Launch XWADuelVR. Index rays GONE from both hands in menu.
2. Look down: smaller pilot, roomier feel; knees/boots visible.
3. Start combat: body rides with the ship; hands/levers unchanged.
4. Report: fit good / still big / too small? rays gone? If fit is
   good, stage 1 is DONE -> stage 2 arms (follow hands, no dups).
