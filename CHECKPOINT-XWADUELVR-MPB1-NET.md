# CHECKPOINT XWADuelVR MP-B1-NET (3.30-MPB1-net)

## Fecha
2026-10-10/11. Quest 3487C10H9T0BKZ (WiFi 192.168.68.100:5555). APK
`XWADuelVR-MPB1-NET-debug.apk` (2246555 B, SHA256
`AA78E8A3E805B035066A2678B57C446210C05B1E840122C7D5D6D039E6232D20`),
instalado OK + boot OK (DUEL draws, sin crash).

## Alcance (Fase B1: duelo por red vs dron, sin 2do Quest)
El usuario no tiene otro visor: el rival es un dron headless por
UDP localhost. El jugador lo ve (X-Wing + diamante + barra),
el dron lo persigue y dispara, y el duelo termina con ganador.

- `src/duel_net.h/.c`: protocolo LE (snap 100 B @20 Hz, input 32 B
  @30 Hz, magic+proto), UDP no-bloqueante (POSIX + glue Winsock),
  `DuelDrone_Step` (persecución body-frame + throttle bang-bang +
  cono de fuego). Ruteo por longitud entre snap/input.
- `src/duel_drone.c`: binario headless (Quest/PC): recibe snaps,
  vuela, devuelve input. `src/duel_nettest.c`: arnés de integración
  (sim lado-juego + UDP real vs dron real).
- Juego: fila MODO (ESCARAMUZA/DUELO) en BRIEF + `menu.duel_mode`;
  START/REPEAT/RESTART eligen Reset; `Duel_UpdateDuel` con input de
  red; snapshots @20 Hz a localhost; edge de resultado por winner
  latcheado (sin auto-reset); permiso INTERNET.
- Render: exterior del peer + cue/diamante/pip + barra de casco +
  chispas + muzzle + explosión + rearm, todo espejo de allies.

## Tests
- `tests/test_render_rules.py`: ALL PASS (+2 checks MP-B1: MODO +
  peer render).
- Device `/data/local/tmp/test_duel_mpb1`: ALL DUEL TESTS PASS
  (+`test_net_codec`, `test_drone_ai`; conteos BRIEF 8->9).
- **Loopback real** (`duel_nettest` vs `duel_drone` en el Quest,
  90 s): NETTEST PASS — rx_in=2625, tx_snap=1800, peer voló
  12.7 km, 31 disparos, **el dron mató al P1 scripteado
  (winner=2)**. Dron terminado limpiamente después.

## Archivos
- Nuevos: `src/duel_net.h/.c`, `src/duel_drone.c`,
  `src/duel_nettest.c`, `evidence/duel_drone`, `evidence/duel_nettest`
- Tocados: `src/duel_main.c`, `src/duel_render.c`, `src/duel_menu.c/.h`,
  `tests/test_duel.c`, `tests/test_render_rules.py`,
  `app/src/main/AndroidManifest.xml`, `app/build.gradle`, CMakeLists

## Validación pendiente en visor (usuario)
Arrancar el dron (`adb shell /data/local/tmp/duel_drone &`), poner
MODO=DUELO en BRIEF, INICIAR, pelear. Esperado: X-Wing rival con
diamante rojo que persigue y dispara verde; al morir alguien,
RESULTADO con VICTORIA/DERROTA.

## Siguiente
Fase B2 (descubrimiento LAN) o C (internet + salas con código +
servidor) + D (voz). Humano-vs-humano real necesita 2do visor
(candidato: un tester del UAT).
