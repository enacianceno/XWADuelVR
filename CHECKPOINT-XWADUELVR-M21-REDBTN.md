# CHECKPOINT XWADuelVR M21 — pause on the real red button

Version **3.28-M21-redbtn**, APK `XWADuelVR-M21-REDBTN-debug.apk`
(2242011 B, SHA256 `8CC662AB2A632BAA2589E4D4676C46107918BA0E1E72B92643457AC1801F2B34`).
Installed on Quest 3487C10H9T0BKZ (WiFi; USB still down), dumpsys
`versionName=3.28-M21-redbtn`.

User report on M19/M20: the floating pause button is not visible;
there ARE two red buttons together at the right hand — enable ONE of
them as pause.

## Hunt (model forensics, committed under evidence/m21-redbtn/)

- All cockpit materials are textured (no red baseColorFactor), so the
  buttons are texels: scanned all 143 cockpit textures for red
  clusters, correlated with geometry.
- Tex17 carries red pairs; UV->3D barycentric map on mesh40p1 (right
  front panel) resolves the pair: A-L ship (-0.024, +0.429, -1.455)
  and A-R ship (+0.070, +0.451, -1.468), 9.7 cm apart (8-13 mm tris).
- Wired A-R (the right one of the pair, nearest the stick hand).
  Scripts: `redbtn_hunt.py` (region scan), `redbtn_map.py` (map).

## What changed

- `DUEL_PAUSEBTN_SHIP`: (0.38, 0.50, -0.91) guess ->
  (0.070, 0.451, -1.468) measured. Radii tightened to isolate the
  wired button from its mate 9.7 cm away: PRESS 5->3 cm, NEAR 10->6.
- Floating box REMOVED (it sat in the wrong place, invisible): the
  real red button is the visual now. Only the PAUSA label floats
  (3 cm toward the pilot, depth-tested, never coplanar), state-
  colored: amber idle / white < 6 cm / green pressed.
- Touch (ship-frame index distance), consumed edge, ClearGrabs level
  memory, main poll + "pause button" reason: unchanged mechanics.

## Tests

- `tests/test_render_rules.py`: M19 button rule repointed (label, no
  box; `btn_range` negatively pinned). Fail-first: 1 FAIL, then ALL
  RENDER RULE TESTS PASS. No device change (render-side touch).
- Device CPU suite `/data/local/tmp/test_duel_m21`: ALL DUEL TESTS
  PASS (rebuild gate).

## Verification

- Native: `Build-Target.ps1` clean; gradle-8.5 offline
  `assembleDebug` BUILD SUCCESSFUL; install Success, dumpsys OK.

## Visor test (PENDING user validation -- not claimed)

1. Combat: amber PAUSA label floats just off the RIGHT red button
   of the pair (front-right panel, ~75 cm: lean-reach).
2. Approach the wired (right) button: label turns white < 6 cm;
   touch it: green + instant pause menu.
3. Touch the LEFT mate: nothing must happen (solo uno).
4. RESUME with the finger still on it: no instant re-pause.
5. If I picked the wrong one of the two, say so: one-line swap.
