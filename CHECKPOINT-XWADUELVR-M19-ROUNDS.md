# CHECKPOINT XWADuelVR M19 — small menu + rounds + cockpit pause button

Version **3.26-M19-rounds**, APK `XWADuelVR-M19-ROUNDS-debug.apk`
(2242543 B, SHA256 `6C199713F6915B4BFE3C71BC389FE8E3E497D3DD2C85CF6C4A230C6BF7C76A38`).
Installed on Quest 3487C10H9T0BKZ, dumpsys `versionName=3.26-M19-rounds`.

User order: 40% smaller menu; round count in the menu; a button on the
ship's right side to pause and bring the menu back.

## 1. Scale x0.42 (40% smaller than M16, literal reading)

`DUEL_MENU_SCALE 0.7 -> 0.42`. Panel 0.325 m wide, rows 2.0 cm. All
render literals already hang off the knob (no emit change). Touch
targets are smaller but usable; the tablet can be pulled closer
(0.30 m shell). If it reads too small, one line reverts/tunes.

## 2. Rounds series

- BRIEF gains a RONDAS choice (1/3/5, default 3; click cycles like
  DIFICULTAD/BATALLA). BRIEF is now 8 rows (INICIAR row 6).
- RESULT gains the series line "RONDA X DE N GANADAS: W" under the
  pilot line (refreshed on every screen change).
- Flow (main, sim untouched): START opens round 1; REPEAT advances
  (a fresh series after the last round, so infinite play survives);
  RESTART replays the current round; wins counted at RESULT time.
  Combat-start log carries round/best/won.
- Session-only (like difficulty/skirmish: prefs persist audio/sens/
  dead + last pilot only, unchanged).

## 3. Cockpit pause button (right console)

- Ship-frame `(0.38, 0.50, -0.91)`: mirrors the left quadrant slot;
  placement is a principled guess (symmetric cockpit) — user confirms
  it sits on visible console in visor.
- Solid depth-tested box + PAUSA label facing the pilot; amber idle,
  bright < 10 cm, green on press. Combat only (hidden with menu up).
- Index-tip ship distance (seat + neutralized LOCAL, like grabs);
  press < 5 cm, either hand. Edge consumed by main; level memory
  survives ClearGrabs (resume with the finger down re-arms, never
  re-fires). Main pauses with reason "pause button" (same PAUSED
  screen as the menu button: REANUDAR/REINICIAR/RECENTRAR/...).

## Tests

- `tests/test_render_rules.py`: M16 scale rule -> 0.42, +3 M19
  (rounds, button emit+touch, main poll). Fail-first: 4 FAIL, then
  ALL RENDER RULE TESTS PASS.
- `tests/test_duel.c`: menu_rows/menu_values pins updated (8 rows,
  INICIAR 6, RONDAS cycle/values), home_anchor pins (0.42 / 0.325 /
  0.25..0.29), +`series` (defaults, rows, line). Fail-first observed
  (compile), then device green.
- Device CPU suite `/data/local/tmp/test_duel_m19`: ALL DUEL TESTS
  PASS. No device test for the button proper (render-side touch
  level, like menu_touch; rules pin the wiring).

## Verification

- Native: `Build-Target.ps1` clean; gradle-8.5 offline
  `assembleDebug` BUILD SUCCESSFUL; install Success, dumpsys OK.

## Visor test (PENDING user validation -- not claimed)

1. Menu clearly smaller; rows/text still readable and tappable.
2. BRIEF: RONDAS cycles 1/3/5; INICIAR starts "RONDA 1 DE N".
3. Play 3 rounds: RESULT shows round + GANADAS each time; after the
   last, REPETIR opens a fresh series; RESTART (pause) replays round.
4. Combat: amber PAUSA button on the right console; approach: bright;
   touch: green + instant pause menu. RESUME with the finger still on
   it: must NOT instantly re-pause.
5. Button placement: sits on console, reachable, not floating oddly?
