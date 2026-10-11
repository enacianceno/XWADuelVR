# CHECKPOINT XWADuelVR M26-TXT (3.30-M26-txt)

## Fecha
2026-10-11. Quest 3487C10H9T0BKZ (USB). APK
`XWADuelVR-M26-TXT-debug.apk` (2242007 B, SHA256
`DE068FF124BA622EE8BA49A3223C26EAB68B0A6E66FEDA67B9E24171C02CAADE`),
dumpsys `versionName=3.30-M26-txt`. Instalado OK.

## Cambio (correccion del usuario)
60% mas pequeno que los 8 mm de M24 = 3.2 mm (M25 habia quedado
en 2.4 mm). Texto ~16 mm de ancho bajo el boton de 18 mm, 13 mm
bajo su centro. A 0.53 m son ~34x7 px, legible y discreto.

## Tests
- `tests/test_render_rules.py`: ALL PASS (check M19/M26 fija
  `"PAUSA", 0.0f, -0.013f, 0.0032f`).
- Device `/data/local/tmp/test_duel_m26`: ALL DUEL TESTS PASS.

## Archivos
- `src/duel_render.c`, `tests/test_render_rules.py`
- `app/build.gradle` (3.30-M26-txt)
