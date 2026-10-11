# CHECKPOINT XWADuelVR M17 — tablet in hand (frame grab + rigid follow)

Version **3.24-M17-tablet**, APK `XWADuelVR-M17-TABLET-debug.apk`
(2240159 B, SHA256 `1E600E74F902501D0AE1121F70F6545FDAA92B7F4E97E8F5197DB3B79A0B5E84`).
Installed on Quest 3487C10H9T0BKZ, dumpsys `versionName=3.24-M17-tablet`.

User report on M15/M16: still hard to grab (can't take it where the
menu frame ends; the hand latches from below out of nowhere). Want:
the menu moves WITH the holding hand — turn the wrist and it turns.
Feel: a tablet in the hand.

## What changed

- Frame-only latch (`duel_panel_frame`, pure + tested): pinch latches
  on the 4 edges (3.5 cm band) + handle strip + small tolerances, max
  12 cm in front / 8 cm behind. The row interior stays tap-only;
  empty space below/front never latches. The M15 box (+10 cm, 25 cm
  front) is gone.
- Rigid tablet follow while held: at latch the hand game quat +
  pinch->center + panel game pose are captured; every frame the wrist
  delta since latch swings the panel about the pinch (absolute
  `duel_panel_place`, no drift). Position AND orientation track the
  hand; reach clamp still guards the center. Wristless frames hold
  still; first valid frame re-bases (no jump).
- Drop keeps the hand pose exactly (M15 re-face snap removed).
  Winner suppression, drop quiet, keep-across-screens and
  RECENTRAR re-home unchanged.
- `duel_panel_undrag` removed (delta follow superseded); its device
  roundtrip removed with it. `unrot`/`clamp` stay (live use).

## Tests

- `tests/test_render_rules.py`: M15 drag rule updated to the place
  mechanism (noted supersede), M15 drop rule renamed, +3 M17 (frame
  latch, rigid follow, drop keeps pose). Fail-first: 4 FAIL, then
  ALL RENDER RULE TESTS PASS.
- `tests/test_duel.c`: +`panel_follow` (9 zone cases, place
  roundtrip vs neutralize+eye path, handq/unrot roundtrip).
- Device CPU suite `/data/local/tmp/test_duel_m17`: ALL DUEL TESTS
  PASS.

## Verification

- Native: `Build-Target.ps1` clean; gradle-8.5 offline
  `assembleDebug` BUILD SUCCESSFUL; install Success, dumpsys OK.

## Visor test (PENDING user validation -- not claimed)

1. Pinch the amber handle or any panel edge: latches at the frame,
   panel never jumps.
2. Pinch empty space below/in front, or a row interior: NO grab
   (rows still tap).
3. Held: move the hand — panel rides rigidly; turn the wrist — panel
   turns about the pinch like a tablet. No drift, no lag feel.
4. Drop: stays exactly where/how you left it; lift finger, tap works.
5. Screens/pause keep the spot; RECENTRAR re-homes.
