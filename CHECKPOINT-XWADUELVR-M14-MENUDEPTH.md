# CHECKPOINT XWADuelVR M14 — menu/hands depth (fingers no longer behind panel)

Version **3.21-M14-menudepth**, APK `XWADuelVR-M14-MENUDEPTH-debug.apk`
(2237147 B, SHA256 `A74811AF6988DCD26EFCC95ACC397B1AF8C8B9BE1C8F5545444D544FAE409CCE`).
Installed on Quest 3487C10H9T0BKZ, dumpsys `versionName=3.21-M14-menudepth`.

User report on M13: no pilot (confirmed), menu touch + position good,
but hands render BEHIND the menu.

## Root cause

The menu panel used the depth-off HUD pipes (`pipe_tri_alpha` /
`pipe_lines`) and was drawn AFTER the hands, so it always overdrew the
fingers even while touching (M11 made it "always on top" deliberately).

## What changed (`src/duel_render.c` only, + version bump)

- New `pipe_menu_tri` (alpha, depth test ON + write ON) and
  `pipe_menu_line` (alpha lines, test ON, write OFF), created
  unconditionally; Ready-check + destroy updated.
- Menu draws moved BEFORE the hands: fingers in front of the 0.5 m
  panel overdraw it, hands behind stay hidden. Cockpit still occludes
  the panel correctly (tested, 0.5 m in front wins).
- HUD/flash keep `pipe_tri_alpha` depth-off (NDC, unchanged).
- `app/build.gradle`: `versionName 3.21-M14-menudepth`.
- `tests/test_render_rules.py`: 3 new M14 rules (menu pipes, menu
  before hands, HUD pipe unchanged). Written first, 2 FAILED pre-fix,
  all pass post-fix.

## Verification

- Render rules (host): ALL RENDER RULE TESTS PASS (incl. 3 M14).
- Native: `Build-Target.ps1` clean.
- APK: gradle-8.5 offline `assembleDebug` BUILD SUCCESSFUL.
- Install Success, dumpsys 3.21-M14-menudepth.

## Visor test (PENDING user validation -- not claimed)

1. Boot: menu at ~50 cm, centered, readable as in M13.
2. Reach a finger toward a row: the fingertip must visibly cover the
   panel as it approaches, click on contact as before.
3. Pull the hand behind the panel plane: panel covers the hand.
4. Pause mid-combat: same depth behavior; resume/restart flow as M13.
