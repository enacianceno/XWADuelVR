# CHECKPOINT XWADuelVR M25-TXT (3.30-M25-txt)

## Fecha
2026-10-11. Quest 3487C10H9T0BKZ (USB). APK
`XWADuelVR-M25-TXT-debug.apk` (2242007 B, SHA256
`3B99A8F08B189361A296C53B7EFC29E859C82BA586C61DBEBD0E9F1305D5B969`),
dumpsys `versionName=3.30-M25-txt`. Instalado OK.

## Cambio (pedido del usuario)
Texto PAUSA del boton de pausa mucho mas pequeno: M24 lo habia
dejado en 8 mm (llenaba la vista). M25 lo baja a 2.4 mm (~70% mas
pequeno, ~12 mm de ancho bajo el boton de 18 mm; a 0.53 m son
~26x5 px, legible pero discreto). Offset 16 mm -> 12 mm bajo el
centro del boton. "701% mas pequeno" literal daria tamano negativo;
1.14 mm (/7.01) volveria a ser subpixel como en M23, por eso 2.4 mm.

## Tests
- `tests/test_render_rules.py`: ALL PASS (check M19/M25 fija
  `"PAUSA", 0.0f, -0.012f, 0.0024f`; literal viejo ausente).
- Device `/data/local/tmp/test_duel_m25`: ALL DUEL TESTS PASS
  (sin cambios en test_duel.c; etiqueta no cubierta ahi).

## Archivos
- `src/duel_render.c` (linea PAUSA + comentario)
- `tests/test_render_rules.py`, `app/build.gradle` (3.30-M25-txt)
