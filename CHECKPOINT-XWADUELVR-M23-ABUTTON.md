# CHECKPOINT XWADuelVR M23 — pause on the A button

Version **3.30-M23-abutton**, APK `XWADuelVR-M23-ABUTTON-debug.apk`
(2242011 B, SHA256 `29110E53C0180139EEB5614F46ED4C62AFC4ED9DAB7037E7AB6EDE2C0B6250D7`).
Installed on Quest 3487C10H9T0BKZ (WiFi; USB still down), dumpsys
`versionName=3.30-M23-abutton`.

User clue settling M21/M22: the wanted reds carry an A-like triangle
and a C-like arc — wire the A.

## Audit resolution

- Enlarged Tex17 top row confirms: left red trapezoid = dark
  triangle-over-arc (reads "A"), right red = dark arc (reads "C").
- Indexed barycentric search over every Tex17 prim: the A/C cells
  map ONLY on mesh40p1 (front-right sloped panel; flanks verified
  empty, center panel empty). M21 had the pair but wired the C;
  M22's flank squares were a miss.
- A: ship (-0.024, +0.429, -1.455), 8-13 mm tris; mate C 9.7 cm
  away at (+0.070, +0.451, -1.468). ~25 cm past the stick grip,
  ~75 cm from the eye (lean-reach, same panel the user described:
  sloped, greens beside via Tex16).

## What changed

- `DUEL_PAUSEBTN_SHIP`: (0.398, 0.429, -0.881) -> (-0.024, 0.429,
  -1.455); radii back to PRESS 0.03 / NEAR 0.06 (9.7 cm isolation).
- Label auto-follows (no render change); touch/edge/main untouched.

## Tests

- `tests/test_render_rules.py`: button value pin -> A coords.
  Fail-first: 1 FAIL, then ALL RENDER RULE TESTS PASS.
- Device CPU suite `/data/local/tmp/test_duel_m23`: ALL DUEL TESTS
  PASS (rebuild gate).

## Verification

- Native: `Build-Target.ps1` clean; gradle-8.5 offline
  `assembleDebug` BUILD SUCCESSFUL; install Success, dumpsys OK.

## Visor test (PENDING user validation -- not claimed)

1. Combat: amber PAUSA floats off the LEFT red of the A/C pair
   (triangle button, front panel past the stick).
2. Touch the A: white < 6 cm, green + pause on contact.
3. Touch the C: nothing (solo uno).
4. RESUME with the finger on it: no instant re-pause.
