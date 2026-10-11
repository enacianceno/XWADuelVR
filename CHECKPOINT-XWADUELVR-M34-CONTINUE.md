# Checkpoint M34 — CONTINUE? al morir (seguir en el mismo combate)

## Versión
- `versionName 3.30-M34-continue`.
- APK `XWADuelVR-M34-CONTINUE-debug.apk`, SHA256
  `E4827CD66C2E09A54505D279BF6FD324ED41699F6CB140EEF33A924C74198AA2`.
- Instalado OK (`Success`), Quest `3487C10H9T0BKZ` @ `192.168.68.101:5555`.

## Pedido del usuario
Al morir, preguntar "continue?" para seguir en combate.

## Diseño
- Al morir vs IA (escaramuza/crucero): tras el beat de muerte la batalla se
  CONGELA (sin auto-reset) y el RESULT pregunta `CONTINUAR?` con filas
  CONTINUAR / REPETIR / MENU.
- CONTINUAR = respawn del jugador (full HP, spawn) en LA MISMA batalla:
  ties/aliados/crucero/subs/bajas intactos, misma ronda, serie sin tocar.
- REPETIR = ronda nueva fresca (avanza serie); MENU = salir. Victoria y el
  duelo PvP decidido sin cambios (VICTORIA / DERROTA, REPETIR/MENU).
- Fix de paso: el RESULT leía `player.alive` post-reset, así que vs IA siempre
  decía VICTORIA (DERROTA inalcanzable). La derrota ahora desvía antes del reset.

## Implementación
- `src/duel_sim.h/.c`: `result_hold` en DuelState (DESTROYED salta auto-reset
  en `Duel_Update` + `Duel_UpdateCruiser`); `Duel_RespawnPlayer` (jugador fresco,
  resto intacto, COMBAT, hold liberado).
- `src/duel_menu.h/.c`: `DUEL_ACT_CONTINUE`; título derrota-IA `CONTINUAR?`
  (ASCII: `¿` se vería en blanco, la fuente es CAPS-ASCII); fila CONTINUAR solo
  en derrota vs IA (duel_mode != 1).
- `src/duel_main.c`: `defeat_hold` local; edge de muerte congela; RESULT derrota
  tras el beat (registra piloto, sin punto de serie); dispatch CONTINUE
  (respawn + COMBAT + need_fire_release); START/RESTART/REPEAT limpian el flag.
- Render: sin cambios (título + filas genéricas).

## Tests
- Reglas: ALL PASS (pin M34: acción/fila/título/respawn/hold/edges/log).
- `evidence/test_duel_m34` en Quest: ALL DUEL TESTS PASS, incl. nuevo
  `continue ok`: auto-reset sin hold intacto; derrota sostenida intacta
  200 frames; respawn fresco con batalla intacta (skirmish + crucero con
  daño de casco/subs); filas/título/acción por modo (0/1/2, win/lose).

## Boot
`am start` OK: 72 fps, sin FATAL (SALIR tocado por el usuario al final).

## Validación en visor
PENDIENTE: morir → `CONTINUAR?` → CONTINUAR vuelve al mismo combate;
REPETIR/MENU intactos; victoria intacta.
