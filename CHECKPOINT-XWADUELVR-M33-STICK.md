# Checkpoint M33 — Expo en palanca (mira fina)

## Versión
- `versionName 3.30-M33-stick`.
- APK `XWADuelVR-M33-STICK-debug.apk`, SHA256
  `D5FFCAA04AC183C9B7BC7E9877D3D8D940F8E11E444BE03D6481D964C63253DC`.
- Instalado OK (`Success`), Quest `3487C10H9T0BKZ` @ `192.168.68.101:5555`.

## Pedido del usuario
Centrar un objeto en la mira es muy difícil; la palanca de giro está muy sensible.

## Causa
Mapeo lineal puro (deflexión × rate) con deadzone de corte duro en 0.15:
a 20% de palanca el pitch ya gira a 18°/s, y en el borde de la deadzone el
rate saltaba 0 → 0.15×rate de golpe.

## Implementación
- `src/flight_config.h`: `DUEL_STICK_EXPO 0.6` (única perilla de tuning).
- `src/duel_sim.c/.h`: `Duel_StickCurve(x, dead)` — reescaleo de deadzone
  (continuo, sin salto) + blend lineal/cúbico. Aplicado a pitch/yaw/roll de
  palanca física Y palancas de mano en `duel_update_human` (jugador + peer MP-A).
- Números: deflexión 0.5 → 0.207 rate; topes ±1 intactos (misma agilidad máxima).
- Rates base sin cambio (pitch 1.6 / yaw 0.9 / roll 2.2 rad/s).

## Tests
- `tests/test_render_rules.py`: ALL PASS (pin M33; pin M6 actualizado al nuevo
  contrato `yaw = Duel_StickCurve(s->hand_yaw...)` — el split sigue intacto).
- `evidence/test_duel_m33` en Quest: ALL DUEL TESTS PASS, incl. nuevo
  `stick_expo ok (half 0.207)`: ceros, borde continuo, topes, simetría impar,
  integración (media palanca ≪ mitad de giro) y paridad palanca↔mano.
- Tests viejos intactos: todos usan deflexión total (±1) donde la curva = identidad.

## Boot
`am start` OK: menú, 72 fps, ties=3 allies=2, sin FATAL (el usuario tocó SALIR
al final del log — interacción en visor, no error).

## Validación en visor
PENDIENTE: sentir la mira fina al centrar blancos + revalidar M32 (subsistemas).
Si aún se siente rápido: bajar `DUEL_STICK_EXPO` no — subir expo (0.7) o bajar
rates; si se siente muerto al centro: bajar expo (0.4-0.5).
