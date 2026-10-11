# Checkpoint M37 — Punto del rayo + crucero que falla en fácil

## Versión
- `versionName 3.30-M37-kind`.
- APK `XWADuelVR-M37-KIND-debug.apk`, SHA256
  `414EF7647AD45F84914C467401B86BDD31E05C5491EC9D701EA7446FB8A0C889`.
- Instalado OK (`Success`), Quest `3487C10H9T0BKZ` @ `192.168.68.101:5555`.

## Pedidos (M36 aprobada)
1. Rayos con un punto sobre el menú (que se sienta que están ahí).
2. El crucero es demasiado bueno destruyendo aun en fácil: que falle.

## 1. Punto del rayo
- `src/duel_render.c`: donde el rayo toca el panel se dibuja un diamante
  blanco brillante de 8 mm, 1 mm sobre la capa de glifos (mismo pase
  depth-tested, solo con RAYO MANDO on). Sensación de cursor real.

## 2. Crucero obedece dificultad
- Causa: el volley usaba wobble y cadencia FIJOS en toda dificultad
  (fácil solo debilitaba a los TIE) — por eso fácil se sentía igual.
- `src/duel_sim.c` (`duel_cruiser_volley`): cadencia × dcd (rookie 1.6×
  lento, ace 0.8×) + wobble ×2.0 rookie / ×0.6 ace (pilot = legacy).
  Daño intacto (10: si te da, cuenta).
- Medido en Quest (blanco quieto, 20 volleys): rookie 10 hp (1 hit),
  pilot 40, ace 50. En fácil, un jugador que se mueve casi no recibe.

## Tests
- Reglas: ALL PASS (pin M37: punto + cadencia×dcd + wobble).
- `evidence/test_duel_m37` en Quest: ALL DUEL TESTS PASS — bloque cruiser
  extendido (20 volleys × 3 dificultades, conteo por turret_next, daño
  acumulado con top-up; orden estricto 10<40<50 + rookie<=20 + ace>=50).

## Boot
`am start` OK: 72 fps, sin FATAL.

## Validación en visor (pendiente)
1. Punto blanco donde apunta cada rayo; sigue a la fila.
2. Crucero en fácil: perdona (ver fallar las andanadas); pilot/ace intactos.
