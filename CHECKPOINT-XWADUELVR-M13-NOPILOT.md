# CHECKPOINT XWADuelVR M13 — pilot removed (back to pre-M4)

Version **3.20-M13-nopilot**, APK `XWADuelVR-M13-NOPILOT-debug.apk`
(2237063 B, SHA256 `D498EC062A699DE1908EF23317911BE4BFD6D019725AD130672DB0A2D17DD55A`).
Installed on Quest 3487C10H9T0BKZ, dumpsys `versionName=3.20-M13-nopilot`.

User request: remove the visible pilot body entirely, app back to the
state before any pilot attempt (M12 + menu/home/ghost fixes kept).
Next: user will source GLOVES so tracked hands don't look plain white.

## What changed

- Deleted: `src/duel_pilot.c/.h`, `assets/pilot/`.
- `src/duel_render.c`: removed pilot include, `PilotArm` struct,
  pilot/arm model fields, mesh whitelist, skin/bake/load/draw/arm
  functions, load/draw/upload/destroy hooks. Restored the original
  upload-gate condition.
- `src/duel_hands.c/.h`: removed `DuelHands_Local2Ship` (arms-only).
- `CMakeLists.txt`: `duel_pilot.c` out of sources.
- `tests/test_duel.c`: removed body tests
  (pose/root/frame/skin/boot/arm) + local2ship + helpers; menu pilot
  profiles (roundtrip/scan) KEPT.
- `tests/test_render_rules.py`: M4/M4c/M4e-root/M4h rules replaced by
  M13 no-pilot regression rules (touch rays stay off).
- Menu pilot profiles (M7), Easy default, home touch/sizes, near
  panel, ghost fix: untouched.

## Preserved (not deleted)

- `evidence/pre-nopilot-backup/`: full worktree diff M9..M4h +
  `duel_pilot.c/.h` + restore README.
- `evidence/pilot-inbox/`: source GLB + license + converter (assets
  regenerable).
- M4..M4h APKs + checkpoints at repo root.

## Verification

- Native: `Build-Target.ps1` clean.
- Render rules (host): ALL RENDER RULE TESTS PASS.
- Device CPU suite `/data/local/tmp/test_duel_m13`: ALL DUEL TESTS PASS.
- Install Success, dumpsys 3.20-M13-nopilot.
- Device cleaned: app `files/duel_assets/Pilot/` + `/data/local/tmp/Pilot.*` gone.

## Visor test (PENDING user validation -- not claimed)

1. Launch XWADuelVR: cockpit with NO body (as in M12), tracked hands only.
2. Menu touch, duel start, pause, restart, menu return: all as validated.
3. Report anything pilot-related still visible (there should be nothing).
