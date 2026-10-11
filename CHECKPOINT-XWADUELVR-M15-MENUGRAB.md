# CHECKPOINT XWADuelVR M15 — grabbable menu (pinch, drag, drop re-face)

Version **3.22-M15-menugrab**, APK `XWADuelVR-M15-MENUGRAB-debug.apk`
(2239603 B, SHA256 `BE50F0AD8D3F56A775ACC8E03E346A72B19E6D89A26B6C547FEB12DAA9EFB608`).
Installed on Quest 3487C10H9T0BKZ, dumpsys `versionName=3.22-M15-menugrab`.

User report on M14: fingers now correct, but the cockpit covers part of
the panel and it shows some noise. Request: mobile panel, grabbable
with hands/fingers. User choices: pinch to grab, translate + face on
drop, spot kept until RECENTRAR.

## How it works

- Pinch (thumb+index <= 5.5 cm) with the pinch midpoint near the panel
  (bounds + 10 cm, -12..+25 cm off the plane) grabs it; opening the
  pinch (>= 8.5 cm) drops it. One holder (R wins ties); the other hand
  can still tap rows while holding.
- Drag is 1:1: the game cursor delta returns to the LOCAL anchor
  through the exact inverse eye path (`duel_panel_undrag`), so the
  panel stays cockpit-fixed, inside a 0.30..1.00 m shell around the
  neutral head point (`duel_panel_clamp`: never lost, never in-face).
- Drop re-faces the panel toward the user where it lands
  (`duel_panel_unrot`); orientation stays put while dragging.
- Clicks are safe: the grabbing hand can't hover/click, and after a
  drop the finger must truly lift (> 5 cm) before it can click again
  (no drop-click).
- The spot survives every screen change + pause (`DUEL menu kept
  dragged`); RECENTRAR (menu or Meta) forgets it and re-homes the
  panel. Pre-cal (~2 s boot): touch only, no grab.

## What changed

- `src/duel_math.h`: `duel_panel_undrag/unrot/clamp` + reach defines.
- `src/duel_hands.h/.c`: `DuelPanelGrab` machine + own pinch band.
- `src/duel_render.c`: grab/drag/drop in the touch block (idempotent
  across eyes), keep-if-moved in `PlaceMenuHome`, re-home on RECENTER
  capture in `Calibrate` (also fixes the latent panel jump on
  recenter), grab reset in `SetMenuPanel`.
- `tests/test_duel.c`: +2 (`panel_grab`, `panel_drag`).
- `tests/test_render_rules.py`: +4 M15 rules (fail-first observed).
- `app/build.gradle`: `versionName 3.22-M15-menugrab`.

## Verification

- Render rules (host): ALL RENDER RULE TESTS PASS (incl. 4 M15).
- Device CPU suite `/data/local/tmp/test_duel_m15`: ALL DUEL TESTS
  PASS (incl. `panel_grab`, `panel_drag`).
- Native: `Build-Target.ps1` clean; gradle-8.5 offline
  `assembleDebug` BUILD SUCCESSFUL; install Success, dumpsys OK.

## Noise note (not separately fixed)

The M14 "ruido" is most plausibly shimmer at the panel/cockpit
occlusion boundary (grazing depth edge + head micro-motion). Moving
the panel to clear space should remove it. If noise persists with the
panel fully clear of the cockpit, report where + when (which screen,
finger near or not) and it gets its own fix.

## Visor test (PENDING user validation -- not claimed)

1. Boot: menu home as M14; tap a row (touch unchanged).
2. Pinch near the panel (not on a row): panel follows the hand 1:1
   with no jump; open the pinch: panel stays, facing you.
3. Drop with the finger on a row: NO click must fire; lift and tap:
   click works.
4. New screen / pause / resume: spot kept. RECENTRAR: back to center.
5. Park it clear of the cockpit: fully visible? noise gone?
