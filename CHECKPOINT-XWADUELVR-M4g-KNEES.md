# CHECKPOINT XWADuelVR M4g — KNEES FORWARD + POV BACK 10 CM

Date: 2026-10-10 ~01:00. Device: Quest 3S `3487C10H9T0BKZ`.
APK: `XWADuelVR-M4g-KNEES-debug.apk` (2241919 B,
SHA256 `1060C956544EAC355B539D281C5AB05D4427C904BF77FCBB8E487C052C945FDA`),
`versionName=3.18-M4g-knees`, installed `install -r`.
No asset changes. Prior builds/APKs preserved; no reset/clean.

## Visor/user requests
- Knees ~20 cm further forward (proportions otherwise good).
- POV 10 cm back.

## M4g changes
1. POV: `DUEL_SEAT_POS` (0,0.75,-0.88) -> (0,0.75,-0.78) ship.
   Eye model (0,0.75,0.78): 5 cm ahead of the seat-back front
   (0.73), inside all DUEL_SEAT_CAL test bounds. test_seat() +
   POV render rule updated in sync (seat-sync rule green).
2. KNEES (best effort, see limits): ankles (+-0.171,0.163,1.448),
   knees IK (+-0.100,0.343,1.064) (+4 cm fwd, -3 cm down vs M4f),
   toes (+-0.202,0.072,1.575), tips ~1.60 (6 cm off firewall).
   Combined with the POV move, knee-eye distance grows 0.40 ->
   0.50 m (+24% apparent depth).
3. Verified with the NEW eye: all joints CLEAR (4.5+ cm); eye
   box +-8 cm: 0/27 blocked on knee/ankle/mid/toe; segments
   free; headroom clear. Side bonus: chest now ahead-below the
   eye (easier look-down than M4f's behind-below).

## Why not the full 20 cm (hard limits, all measured)
- Thigh 44 cm + fixed hips: knee z maxes at 1.19 even with a
  horizontal thigh (needs 1.225 for +20 cm).
- Feet must stay in the well (firewall z~1.58-1.66 lumpy,
  hull y=-0.03): every cm of knee-forward costs ~1 cm of
  foot-forward into the wall.
- Moving the seat forward instead: pelvis would float 20+ cm
  off the pan and/or feet would crash the firewall.
- Delivered the max compatible with feet-on-pedals + visible
  + unclipped. Knee flexion eased (straighter legs).

## Residuals
- Boot outer edge (x=0.256) buries ~4 cm into the dark side
  wall (x=0.216) at the forefoot corner; fully occluded (same
  as M4f, unreported). Narrowing the stance would knock-knee.
- test_duel.c:410/446 still pin stale P0.5b seat literals
  (self-consistent, green; pre-existing, untouched).

## Verification (host + device, all green)
- `tests/test_render_rules.py`: 154/154 PASS (POV rule updated).
- Device CPU suite `/data/local/tmp/test_duel_m4g`:
  `ALL DUEL TESTS PASS` (seat sync, new ankle asserts/quats).
- Device bake probe: joints exactly on targets, no NaN.

## Visor test (PENDING user validation -- not claimed)
1. Launch XWADuelVR. View sits 10 cm further back (roomier).
2. Look down: knees further/lower, legs more extended, boots
   still at the pedals, all textured.
3. Report: knee position acceptable now? POV good? clips? If
   clean, stage 1 is DONE -> stage 2 arms (follow hands).
