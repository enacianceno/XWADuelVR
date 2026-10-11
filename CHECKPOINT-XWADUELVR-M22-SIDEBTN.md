# CHECKPOINT XWADuelVR M22 — pause on the window-side red button

Version **3.29-M22-sidebtn**, APK `XWADuelVR-M22-SIDEBTN-debug.apk`
(2242015 B, SHA256 `F5C39656C372D62480A11B5388136D2CE317CC30063EDFB09D6965641AB4BB6E`).
Installed on Quest 3487C10H9T0BKZ (WiFi; USB still down), dumpsys
`versionName=3.29-M22-sidebtn`.

User correction on M21 (front-panel pick was wrong): the buttons are
by the RIGHT WINDOW, tilted; 2 green horizontal buttons beside them.
Wire either red — still only ONE.

## Hunt (evidence/m22-sidebtn/)

- Tex15 greens land on mesh02p3 right flank (ship +0.357, +0.426,
  -0.990, tight 3.6 cm cluster) — the corroborating greens.
- Barycentric UV->3D (`sidebtn_map.py`, reproducible) on mesh02p7
  (RIGHT flank; p6 mirrors on the left): Tex17 B-squares resolve to
  B-L (+0.416, +0.433, -0.883) and B-R (+0.398, +0.429, -0.881).
- Wired B-R (inner of the pair, nearer the stick hand), 53 cm from
  the eye — comfortable reach, no lean needed (vs M21's 75 cm).
- Mate sits 1.9 cm away: PRESS tightened 3.0->1.8 cm, NEAR 6->3 cm.
  Best-effort isolation at finger scale; borderline presses may
  cross — report cross-fire (fallback: shrink more, swap, or wire
  both).

## What changed

- `DUEL_PAUSEBTN_SHIP`: (0.070, 0.451, -1.468) -> (0.398, 0.429,
  -0.881); radii PRESS 0.018 / NEAR 0.030 (`duel_hands.h/.c`).
- Label auto-follows the const (no render change); touch/edge/main
  mechanics untouched.

## Tests

- `tests/test_render_rules.py`: button rule gains the audited value
  pin (`0.398f, 0.429f, -0.881f` in hands.c). Fail-first: 1 FAIL,
  then ALL RENDER RULE TESTS PASS.
- Device CPU suite `/data/local/tmp/test_duel_m22`: ALL DUEL TESTS
  PASS (rebuild gate; no logic change).

## Verification

- Native: `Build-Target.ps1` clean; gradle-8.5 offline
  `assembleDebug` BUILD SUCCESSFUL; install Success, dumpsys OK.

## Visor test (PENDING user validation -- not claimed)

1. Combat: amber PAUSA floats by the tilted red pair at the right
   window (greens beside); confirm it's THE pair you meant.
2. Touch the INNER red (nearer center/stick): white < 3 cm, green +
   pause on contact.
3. Touch the OUTER red: nothing (solo uno) — report any cross-fire.
4. RESUME with the finger on it: no instant re-pause.
