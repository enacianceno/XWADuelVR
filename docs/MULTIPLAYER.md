# XWADuelVR multijugador — diseño

Decisiones del usuario (2026-10-11): duelo **PvP 1v1**, por
**internet con código de sala**, combate **+ voz**.

## Restricciones reales

- Un solo cockpit (X-Wing): el duelo es **X-Wing vs X-Wing**
  (distinguidos por marcas/color de equipo). No hay cockpit TIE.
- Un solo Quest para probar: las fases A/B se validan en sim
  (loopback) + suite en dispositivo; el visor real con 2 Quest
  llega en la fase C.
- Sin servidor todavía: la fase C necesita decidir el host
  (VPS pequeño o túnel desde el PC del autor).

## Fases

- **A. Duelo local en sim (esta fase):** segundo piloto humano en
  `DuelState` (`peer`, kind `DUEL_SHIP_PEER`, equipo imperial para
  colisiones pero geometría X-Wing), `Duel_ResetDuel` (frente a
  frente a 1000 m) y `Duel_UpdateDuel(p1, p2)` con ganador.
  Sin cambios de render ni red. Testeable en PC/dispositivo.
- **B. Red local:** transporte UDP + descubrimiento LAN, snapshots
  de estado a 20 Hz (pos, orient, throttle, eventos de disparo),
  predicción del propio piloto. Requiere 2 Quest en el mismo WiFi.
- **C. Internet:** señalización + relay/NAT traversal, salas con
  código de 4 letras, lista y join. Requiere host del servidor.
- **D. Voz:** captura de micro + Opus + transporte por el mismo
  relay, push-to-talk o voz abierta.

## Protocolo (esbozo, fases B/C)

- Snapshot no fiable 20 Hz: seq, time, pos(3), orient(quat),
  throttle, alive. Cada cliente simula al rival por extrapolación.
- Eventos fiables ordenados: disparo (muzzle, t), impacto/muerte,
  inicio/fin de ronda. El host (creador de sala) arbitra daño.
- Feuer: el daño lo aplica el sim local con los snapshots; el
  host resuelve desacuerdos por muerte (winner).

## Reglas de equipo (sim)

- Rebeldes: PLAYER + ALLY. Imperiales: TIE + PEER.
- Disparos rebeldes pegan a ties + peer; imperiales a player +
  allies. En escaramuza el peer está aparcado/muerto: sin cambios.
- En duelo (`duel_mode=1`) no hay IA viva; el ganador se queda
  latcheado hasta `Duel_ResetDuel` (sin auto-reset: el menú
  muestra el resultado).
