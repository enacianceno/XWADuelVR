# CHECKPOINT XWADuelVR MP-A-DUEL (3.30-MPA-duel)

## Fecha
2026-10-11. Quest 3487C10H9T0BKZ (WiFi 192.168.68.100:5555). APK
`XWADuelVR-MPA-DUEL-debug.apk` (2242627 B, SHA256
`CA27D3B3735DEE8C7E30E4D6BD9D0FD1B41E0A7F87BB07D67C9C8CA5B0B63A6B`),
dumpsys `versionName=3.30-MPA-duel`. Instalado OK.

## Alcance (Fase A, sim solamente, sin cambio visible en visor)
Base del multijugador decidido por el usuario: duelo PvP 1v1 por
internet con código de sala, combate + voz. Diseño completo en
`docs/MULTIPLAYER.md` (fases A/B/C/D).

- `DuelShipKind` + `DUEL_SHIP_PEER`: segundo humano, geometría
  X-Wing (muzzles/radio/player gun) pero equipo imperial para
  colisiones, así los dos humanos se pegan entre sí.
- `DuelState.peer/duel_mode/duel_winner`; `Duel_ResetDuel`
  (frente a frente a 1000 m, IA aparcada) y
  `Duel_UpdateDuel(p1, p2)` (sin IA, sin auto-reset; ganador 1/2,
  0 en doble KO, latcheado hasta ResetDuel).
- `duel_update_player` refactorizado a `duel_update_human(...
  use_hands)`: el local conserva overrides de manos (wrapper con
  1), el peer vuela con input crudo.
- Rayos rebeldes prueban ties + peer; rayos peer entran a la rama
  imperial con daño de jugador. Peer aparcado en escaramuza = no-op.
- Render sin cambios: rayos peer salen verdes (rama imperial).

## Tests
- `tests/test_render_rules.py`: ALL PASS. (2 reglas fijaban el
  literal `if (s->hand_...)` de un sim de un humano; se actualizaron
  a fijar el contrato: overrides presentes + wrapper local con
  `use_hands=1`. Comportamiento idéntico, verificado por los tests
  de manos en dispositivo.)
- Device `/data/local/tmp/test_duel_mpa`: ALL DUEL TESTS PASS,
  incluyendo nuevo `test_duel_pvp` (spawn enfrentado, inputs
  independientes, kill cruzado ambos lados, winner latcheado sin
  auto-reset, ResetCombat aparca al peer).

## Archivos
- `src/duel_sim.h`, `src/duel_sim.c`, `tests/test_duel.c`
- `tests/test_render_rules.py`, `docs/MULTIPLAYER.md`
- `app/build.gradle` (3.30-MPA-duel)

## Siguiente (Fase B)
Transporte UDP + descubrimiento LAN + snapshots 20 Hz. Requiere
2 Quest en el mismo WiFi para validar en visor.
