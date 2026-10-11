# M10 HOME VIEW + TOUCH + BATTLE SIZES (2026-10-09/10, código + APK, validación física pendiente)

Status: BUILT, host + device-CPU verified, INSTALLED. Milestone NOT
physically validated: user tests in visor.

Session scope: XWADuelVR ONLY (XWAQuest + shared libs read-only).
No reset/clean, no overwritten builds.

User report on M9 (this turn's input): pinch still mis-selected;
wants 1v1 / 4v4 / more duel sizes; touch-the-panel instead of pinch;
MOST IMPORTANT: after a defeat the ship stays rolled, view must
return to the start point (no recenter method felt).

## 1. Preservation

- Git `d44c46f` + uncommitted M9+M10 work (left for review).
- Installed on Quest 3487C10H9T0BKZ: `v3.9-M10-home-touch-sizes`
  (`install -r`, prefs/pilots intact). Prior APKs intact.
- Controls frozen, unchanged (M6 mapping, free grab, touch-fire,
  head cue + diamond + lead pip, easy AI envelope M9).

## 2. Home view on defeat return (most important)

- Root cause: every DESTROYED->COMBAT auto-reset RECAPTURED the seat
  neutral from the death head pose (looking sideways/rolled), so the
  "fresh" battle started with a rolled cockpit and no way back.
- Fix: `DuelSeatCal` keeps a home reference (boot INIT, or the latest
  explicit recenter: manual RECENTRAR or Meta space-change). Death
  resets RESTORE home (`duel_cal_home_restore`, gen++, RESET reason,
  `DUEL_CAL ... HOME-RESET` log line) instead of capturing. Ship
  already reset to origin/identity; now view + ship both return to
  the start point. Mid-combat pause/resume untouched (no reset edge).
- Edge: death before the first capture -> the RESET capture becomes
  home (else home would never exist).
- PAUSED gained a RECENTRAR row (quick recenter without OPTIONS);
  manual recenter redefines home from the current head pose.

## 3. Touch menus (pinch replaced)

- Menus no longer read pinch: the index fingertip position against the
  panel plane drives hover (near zone 30 cm) + press level (press at
  <= 2 cm, 6 cm behind tolerated, release past 5 cm or off-panel).
  Click = press-make edge ON THE ROW TOUCHED NOW (stale hover can no
  longer fire a gap press); sliders drag while pressed. Controllers
  (stick + trigger/menu) unchanged. Combat fist/pinch/lever/fire
  systems untouched.
- Pure helper `DuelTouch_Level` (duel_hands, CPU-tested); render wiring
  pinned by rules. Panel hint now "TOCA LA PANTALLA / GATILLO".
- Latent fix: choice click now acts on the CLICKED row (was: Adjust on
  sel, no-op when they disagreed).

## 4. Battle sizes 1v1..5v5

- BRIEF gained a BATALLA choice (1V1..5V5, default 3V3 = frozen M5/M9
  battle) + dynamic force lines ("X-WING VS n TIE", "ALIADOS: n
  X-WING", "SIN ALIADOS" at 1v1), rebuilt on init/screen/cycle.
- Sim: slots grown to 5 TIEs + 4 allies (max); `skirmish` 1..5
  (menu-owned, survives resets, applied BEFORE the combat reset).
  ResetCombat spawns all slots deterministically (first 3+2 = frozen
  M5 formation verbatim; new wingmen farther, lead tie stays nearest);
  extras parked dead. Wave ends when the WANTED ties die at any size.
- Follow-through: explosion rearm compares wanted (not max) counts;
  audio first poll snapshots silently (parked ships never fake death
  events; loops still start); auto-target/diamond/lead/scream follow
  living ties only (unchanged logic).
- Order fix: difficulty + size are now set BEFORE ResetCombat, so spawn
  HP/counts use the menu values (M7 set difficulty after: first-wave
  HP used the stale diff).

## 5. Tests

- `tests/test_render_rules.py` (host): 143/143 PASS (137 + 6 M10:
  home restore, touch-not-pinch, click-now, size-before-reset,
  paused recenter, audio sync). Two rules updated for the requested
  rewrites (max counts 5/4; touch expression in the LOCAL=>game pin,
  same frame guarantee).
- `tests/test_duel.c` on device (`/data/local/tmp/test_duel_m10`):
  ALL DUEL TESTS PASS, incl. 3 new (`home_view`, `touch_level`,
  `skirmish`) and honest expectation updates for the behavior change
  (default alive = wanted counts; BRIEF 7 rows, START row 5, BATALLA
  cycle + brief lines, PAUSED 5 rows).
- Native rebuilt; M10 strings verified in the .so AND inside the APK.

## 6. Build

- `XWADuelVR-M10-HOME-TOUCH-SIZES-debug.apk`,
  v3.9-M10-home-touch-sizes, 2237031 bytes,
  SHA-256 `09DA87BE20F3334F6DEBBB0D8018862A4A934B81AB6C39B8215A6B459D925BEC`.
- Built: Build-Target.ps1 + cached gradle-8.5 offline assembleDebug
  (BUILD SUCCESSFUL). Installed with `-r` 2026-10-09 (~20:3x).

## 7. Visor test protocol (user)

1. Menus: toca las filas con el dedo (sin pellizco); verifica que lo
   que tocas es lo que se activa; sliders arrastrando el dedo.
2. BRIEF: BATALLA 1V1 (sin aliados) -> INICIAR; luego 4V4 o 5V5.
3. Derrota: muere mirando a otro lado si quieres; al volver (REPETIR)
   la cabina debe aparecer RECTA al frente (punto inicial). Ídem tras
   victoria. Si volteaste la cabeza al morir, endereza y todo cuadra.
4. Pausa: RECENTRAR recentra donde mires (nuevo home); REINICIAR
   vuelve al home, no a la pose de muerte.
5. Controles de vuelo idénticos; reporta qué tamaño se siente mejor.

## Next step

ONE: user runs §7 and reports (touch reliability, home-view return,
1v1/4v4 feel). No physical validation declared until then.
