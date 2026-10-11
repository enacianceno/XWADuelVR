# Checkpoint M36 — Sticks diestros, escudo de respawn, mano zurda

## Versión
- `versionName 3.30-M36-hands`.
- APK `XWADuelVR-M36-HANDS-debug.apk`, SHA256
  `CABB09A8D23ACDE4EC561CF4F3506FF6DCFB76364F62E21DE36A0B3CD3A73C41`.
- Instalado OK (`Success`), Quest `3487C10H9T0BKZ` @ `192.168.68.101:5555`.

## Pedidos
1. Controles al revés: intercambiar lo que hace cada mando.
2. CONTINUAR vuelve al punto de explosión y muere otra vez: debe volver al
   mismo punto SIN daño, como nuevo.
3. Juego para diestros y zurdos (opción de mano).

## 1+3. Sticks + mano
- Causa del "revés": los sticks volaban con la IZQUIERDA (pitch/yaw) mientras
  las palancas de mano vuelan con la DERECHA — inconsistentes.
- `src/duel_input.c`: DIESTRA (default) = stick derecho vuela (yaw/pitch),
  izquierdo = roll/throttle; ZURDA espeja (mapeo legacy). Setter
  `DuelInput_SetLeftHanded`, publicado por main antes de cada poll.
- Fuego: CUALQUIER gatillo dispara (histéresis .45/.55 por gatillo) — la mano
  que vuela dispara en ambos modos y cada rayo clickea con su gatillo.
- Fila OPCIONES `MANO: DIESTRA/ZURDA` (índice 6; sliders 0-4 intactos).
- Sin cambios: A/B (físicos del mando derecho), botón menú (solo existe en el
  izquierdo — restricción de hardware), palancas de mano (ya diestras).

## 2. Escudo de respawn
- `DuelShip.shield_t` + `DUEL_RESPAWN_SHIELD_S 3.0`: CONTINUAR despierta en el
  sitio con HP lleno y 3 s sin daño (bolts fizzlean al contacto, raspar
  frena pero no quema). Decae en `duel_update_human`.
- Resto intacto: misma batalla, misma ronda, serie y bajas.

## Tests
- Reglas: ALL PASS (pin M36; 4 reglas del mapeo viejo re-apuntadas al
  contrato handed + select DIESTRA-derecha-vuela).
- `evidence/test_duel_m36` en Quest: ALL DUEL TESTS PASS — `continue ok`
  extendido (escudo 3.0, fizzle + ticking, daño al caer, scrape frena sin
  quemar), `menu_ray ok` extendido (MANO DIESTRA/ZURDA/click, 9 filas).

## Boot
`am start` OK: 72 fps, sin FATAL.

## Validación en visor (pendiente)
1. Stick derecho vuela / izquierdo roll+throttle; OPCIONES→MANO→ZURDA espeja.
2. Ambos gatillos disparan y clickean su rayo.
3. Morir→CONTINUAR: mismo punto, 100%, 3 s para salir del furball.
