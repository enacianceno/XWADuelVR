# CHECKPOINT XWADuelVR M16 — small panel + grab handle + opaque paint

Version **3.23-M16-menuhandle**, APK `XWADuelVR-M16-MENUHANDLE-debug.apk`
(2239847 B, SHA256 `A2B22E185885130123DABB915AC0712BD6D6B5222FB5B7FF0FB595AA83D8A6EF`).
Installed on Quest 3487C10H9T0BKZ, dumpsys `versionName=3.23-M16-menuhandle`.

User report on M15: hard to know where to grab; panel too big; noise
in the colors.

## What changed

- 30% smaller panel (user order): one knob `DUEL_MENU_SCALE 0.7f`
  over the M11 design values (`duel_menu.h`). Layout (`duel_menu.c`)
  scales automatically (defines only); render literals (text sizes,
  slider insets, border, hint offset) scale at each site. DIST stays
  0.5 m; depth offsets, grab box and touch thresholds stay absolute
  (hand-scale). Panel now 0.541 m wide, rows ~3.3 cm.
- Grab handle: amber strip above the panel top with dark centered
  text "AGARRA PARA MOVER" (`DUEL_MENU_HANDLE_H`). Outside the touch
  rows (never clicks), inside the M15 grab box (grabs). Pinch-anywhere
  still works; the strip is the explicit affordance.
- Opaque paint: bg/row alpha 0.93/0.90 -> 1.0. Diagnosis: blend mode
  is standard non-premultiplied and shaders carry no dither/noise, so
  the color noise was the cockpit bleeding through the translucency
  (7-10%) over a detailed background. Pipes untouched.

## Tests

- `tests/test_render_rules.py`: +3 M16 (scale knob, handle emit,
  opaque paint). Fail-first observed (3 FAIL), then ALL PASS.
- `tests/test_duel.c`: honest pin updates for the requested scale
  (PANEL_W 0.541, brief PanelH 0.35..0.46) + SCALE/HANDLE pins.
- Device CPU suite `/data/local/tmp/test_duel_m16`: ALL DUEL TESTS
  PASS. M10 hint text untouched (no rule churn).

## Verification

- Render rules (host): ALL RENDER RULE TESTS PASS (incl. 3 M16).
- Native: `Build-Target.ps1` clean; gradle-8.5 offline
  `assembleDebug` BUILD SUCCESSFUL; install Success, dumpsys OK.

## Visor test (PENDING user validation -- not claimed)

1. Boot: panel visibly smaller (~54 cm wide), readable at 0.5 m.
2. Amber strip on top reads "AGARRA PARA MOVER": pinch it, drag,
   drop (re-face); rows still tap as before.
3. Colors: flat, no bleed-through of the cockpit behind; report any
   remaining noise (where: rows/bg/text/handle; when: still/moving).
4. Reach check again at the smaller size: corner rows easy to touch?
