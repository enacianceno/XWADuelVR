# M12 GHOSTFIX (2026-10-09/10, código + APK, validación física pendiente)

Status: BUILT, host + device-CPU verified, INSTALLED, PHYSICALLY
VALIDATED by the user in visor (2026-10-09/10: "quedo excelente";
veil gone, near panel good).

Session scope: XWADuelVR ONLY (XWAQuest + shared libs read-only).
No reset/clean, no overwritten builds.

User report on M11: app ended alone; menu still very far; an orange
layer stuck and never went away.

## 1. Preservation

- Git `d44c46f` + uncommitted M9..M12 work (left for review).
- Installed on Quest 3487C10H9T0BKZ: `v3.11-M12-ghostfix`
  (`install -r` 21:58, prefs/pilots intact). Prior APKs intact.

## 2. Root cause: ghost explosions (orange layer) CERTAIN

- The M10 parked-dead ships (battle size < 5v5) re-fired the one-shot
  explosion FX EVERY frame: trigger (`!alive && !boom`) fired, then
  the wanted-count rearm cleared `boom`, then the trigger fired again.
  Each firing set `r->flash = 1.0`, so the fullscreen orange flash
  triangle (alpha 0.55) NEVER decayed: the stuck orange layer.
- Fix: one-shot gate `duel_fx_boom(seen, alive, boom)` (pure, CPU-
  tested): fires once when a SEEN-ALIVE ship dies; parked ships
  (never seen) stay silent; rearm snapshots seen=alive + boom=0.
  Real kills still explode + flash exactly once per death.
- Pools were always bounds-safe (slot search + drop when full), so
  this bug explains the veil, NOT a crash.

## 3. "Terminó sola": no app crash; device rebooted

- No DuelVR entry in dropbox; crash buffer holds only an unrelated
  Amazon AVOD MediaCodec crash; no DUEL lines survive (fresh log).
- During diagnosis both adb transports dropped (USB, then WiFi
  "offline") and a kernel boot line appears in the log: the Quest
  rebooted (or equivalent device-side event), which ends every app.
  A user-space app cannot reboot the device; cause is external
  (system / power / possibly the Path-B session's own testing).
- If M12 ever "ends alone" again: note the time, leave the Quest
  alone, and report before relaunching, so fresh logs can be pulled.

## 4. "Menu muy lejos": pending clean retest

- Placement math is unchanged from M11 and CPU-verified (home anchor
  roundtrip exact; placement always sees validated views — the whole
  frame body is gated on `have_views`). Through a 0.55-alpha orange
  veil the dimmed panel plausibly reads as distant, so the veil may
  have been the whole complaint. M12 adds a placement-branch log
  (`DUEL menu placed home/head dist=0.50`) for proof on the next run.
- If the panel still reads far with a clean screen: report whether it
  is centered (then: shrink below 0.77 m wide / pull under 0.5 m) or
  off to the side (then: calibration timing issue -> recenter flow).

## 5. Tests

- `tests/test_render_rules.py` (host): 147/147 PASS (145 + 2 M12:
  seen-alive gating, placement log).
- `tests/test_duel.c` on device (`/data/local/tmp/test_duel_m12`):
  ALL DUEL TESTS PASS, incl. new `fx_boom` (parked silent across
  rearms, kill fires exactly once, rearm+revive+kill repeats).
- Native rebuilt; installed APK verified by dumpsys version.

## 6. Build

- `XWADuelVR-M12-GHOSTFIX-debug.apk`, v3.11-M12-ghostfix,
  2237635 bytes,
  SHA-256 `57470C5B9390DD19D915AA42B1A1988C47293436703451EE7DFDA8534713990C`.
- Built: Build-Target.ps1 + cached gradle-8.5 offline assembleDebug
  (BUILD SUCCESSFUL). Installed with `-r`.

## 7. Visor test protocol (user)

1. Boot: clean space, NO orange layer anywhere (menus or combat).
2. Menu at ~50 cm, centered; selections stay put; touch works.
3. Kill a TIE: exactly ONE brief orange flash (~0.6 s), then gone.
4. Die: death flash, RESULT, REPETIR returns home view (M10).
5. Report: veil gone? panel distance/centering? any lone exit + time?

## Next step

DONE: user validated §7 in visor ("quedo excelente"). Next: pilot body
stage 1 (M4) and/or easy-combat feel check, per user order.
