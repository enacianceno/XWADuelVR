# CHECKPOINT XWADuelVR M31-FORCES (batallas asimétricas + escoltas)

## Fecha
2026-10-11. Quest 3487C10H9T0BKZ (WiFi 192.168.68.101:5555).
APK `XWADuelVR-M31-FORCES-debug.apk` (2251203 B, SHA256
`65C699CE4EF741F3B02A93AC10102EF85BBE3B73C486D469A9026987D714947B`),
instalado OK (`versionName=3.30-M31-forces`) + boot OK vía adb a 72fps.

## Qué hace
- Adiós BATALLA simétrica: filas ALIADOS (0..4) + ENEMIGOS (0..5),
  BRIEF 12 filas. Combinaciones: 1v3, 4v1, lobo solitario 1v1,
  1v0 vs el crucero. Brief "X-WING+2 VS 3 TIE".
- Escaramuza exige >= 1 enemigo (0 se muestra/juega como 1).
- Crucero: ENEMIGOS = escoltas TIE del ISD (0 = solo cañones, el
  1v0 pedido), ALIADOS = escoltas propios. Brief crucero:
  "TORR:10+TIE:n" o "TORRETAS: 10".
- Aliados: primero dogfight a escoltas, al ISD con cielo limpio.
- Raspado de casco aplica a todos (escoltas incluidas).
- Default intacto: 3v3 (2 aliados + 3 enemigos).

## Validación
- render-rules ALL PASS (counts + forces + M31 check).
- device `test_duel_m31` ALL PASS (fuerzas, clamp, 1v0, escoltas,
  filas, wraps, brief asimétrico).
- Boot log: `ties=3 allies=2`, 72fps, sin FATAL.

## Tocados
`src/duel_sim.h/.c` (allies_n/foes_n, SetForces, Wanted),
`src/duel_menu.h/.c` (filas + brief), `src/duel_main.c`,
`tests/test_duel.c` (índices +1, 12 filas), `tests/test_render_rules.py`,
`app/build.gradle` (3.30-M31-forces). Render sin cambios.

## Validación pendiente en visor (usuario)
- 1v3 escaramuza, 4v1, crucero 1v0 y con 3 escoltas.
- Siguiente en cola: asteroides sólidos, más cielos, push/UAT.
