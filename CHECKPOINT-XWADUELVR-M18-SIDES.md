# CHECKPOINT XWADuelVR M18 — layered paint + sides-only grab

Version **3.25-M18-sides**, APK `XWADuelVR-M18-SIDES-debug.apk`
(2240271 B, SHA256 `70258B8C63856A7C0F4A8FBDF4F9C1BB7A74859C9E37491061262ACE373C2433`).
Installed on Quest 3487C10H9T0BKZ, dumpsys `versionName=3.25-M18-sides`.

User reports on M17: (1) moving diagonal lines in the menu colors;
(2) grab from the SIDES only — no corners, no bottom, no top.

## 1. Diagonal noise: coplanar z-fighting (confident diagnosis)

Since M14 the menu bg/rows/borders/slider ride EXACTLY coplanar with
depth write on. Same plane + different triangles = ~1-ulp depth
differences per pixel, so overlapping quads flicker with the viewpoint
(diagonals = quad triangulation p0-p1-p2/p0-p2-p3; motion = head
micro-motion). History fits: noise first reported right after M14
enabled menu depth; M16 opacity couldn't fix it (it's depth, not
blend); shaders/blend verified clean.

Fix: stack the paint in mm toward the user (parallax invisible at
0.5 m, fights impossible): bg 2.0, rows/borders 3.0, slider track 3.5
/ fill 4.0, all glyphs 5.0.

## 2. Sides-only grab

- `duel_panel_frame` narrowed: left/right bands (25 mm inside + 30 mm
  outside tolerance), mid-height only (25 mm short of top/bottom = no
  corners), same 12/-8 cm plane band. Rows start 28.6 mm inside >
  25 mm band, so rows are 100% tap-only (side touches are gap presses
  = click-safe by M11 semantics).
- M16 top strip + "AGARRA PARA MOVER" removed (would lie about top
  grab); `DUEL_MENU_HANDLE_H` deleted. New affordance: 3 dark grip
  ticks centered on each side border (`duel_menu_seg` helper).
- Tablet follow, drop-keeps-pose, quiet, keep-across-screens,
  RECENTRAR re-home: unchanged.

## Tests

- `tests/test_render_rules.py`: M16 handle rule repointed to side
  grips, M17 latch rule repointed to the sides-only call, +2 M18
  (tri layers, glyph top layer). Fail-first: 4 FAIL, then ALL PASS.
- `tests/test_duel.c`: `panel_follow` frame cases rewritten (3 latch
  + 8 reject incl. corners/top/bottom), HANDLE pin dropped.
- Device CPU suite `/data/local/tmp/test_duel_m18`: ALL DUEL TESTS
  PASS.

## Verification

- Native: `Build-Target.ps1` clean; gradle-8.5 offline
  `assembleDebug` BUILD SUCCESSFUL; install Success, dumpsys OK.

## Visor test (PENDING user validation -- not claimed)

1. Colors: flat fills, NO moving diagonals anywhere (rows, sliders,
   borders, text) — stare + move head slightly.
2. Grip ticks visible centered on both side borders.
3. Pinch a side middle: latches, tablet-follows; pinch a corner, top,
   bottom, row interior or empty air: NO grab (rows tap as before).
4. Drop keeps pose; screens/pause keep spot; RECENTRAR re-homes.
