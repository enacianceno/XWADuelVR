# CHECKPOINT XWADuelVR M20 — sticky lever hold (wider release)

Version **3.27-M20-griphold**, APK `XWADuelVR-M20-GRIPHOLD-debug.apk`
(2242551 B, SHA256 `578F6CB9606C4FE06E4A5BA5C00D1A814D875E2A508336B75744CD6156DCF446`).
Installed on Quest 3487C10H9T0BKZ (WiFi transport; USB dropped mid-
session, serial re-verified), dumpsys `versionName=3.27-M20-griphold`.

User report: the stick releases at a certain distance; widen it so a
hard yank with the fist still closed keeps moving the levers.

## What changed (one define)

- `DUEL_GRAB_LOSE_R 0.20 -> 0.45` (`duel_hands.h`): while HELD the
  machine only watches grip-open + this radius (palm hysteresis is
  NEAR-only), so both levers (stick fist + quadrant pinch) now hold
  to 45 cm from the moved grip. Latch stays deliberate (9/16 cm);
  grip-open (5 frames) and tracking-loss release unchanged.
- Machine logic untouched (all call sites/tests use the symbol).

## Tests

- `tests/test_duel.c`: +M20 pin in `grab_release` (30 cm yank with
  grip shut holds — failed pre-change, FAR), `grab_fist` yank case
  repointed from hardcoded 0.25 to symbolic `DUEL_GRAB_LOSE_R`
  (intent preserved: yank past release = instant FAR).
- Device CPU suite `/data/local/tmp/test_duel_m20`: ALL DUEL TESTS
  PASS. Render rules untouched, ALL RENDER RULE TESTS PASS.

## Verification

- Native: `Build-Target.ps1` clean; gradle-8.5 offline
  `assembleDebug` BUILD SUCCESSFUL; install Success, dumpsys OK.

## Visor test (PENDING user validation -- not claimed)

1. Grab the stick, yank hard toward the chest with the fist closed:
   holds, full deflection sustained, no drop.
2. Same for the throttle quadrant (pinch held).
3. Open the fist mid-pull: releases promptly as before.
4. Normal flying: no stuck grabs, latch still needs a deliberate
   grab (no air-latch regression).
