# Checkpoint M38 — El rayo muere en el punto

## Versión
- `versionName 3.30-M38-dot`.
- APK `XWADuelVR-M38-DOT-debug.apk`, SHA256
  `903090880DA49F7BAEAF7D2EA3E9D829635C774072721B01C985657865B95EC0`.
- Instalado OK (`Success`), Quest `3487C10H9T0BKZ` @ `192.168.68.101:5555`.

## Pedido (resto de M37 aprobado)
El rayo dibuja el punto pero el trazo continúa: debe cortarse en el punto.

## Implementación
- `DuelMenu_RayPlane` (nuevo, puro): intersección rayo→plano (lx, ly, t);
  `DuelMenu_RayHit` = RayPlane + HitRow (sin cambios de conducta).
- Render: una sola prueba de plano maneja ambas — el rayo termina en `t`
  (siempre alcanza el punto) y el diamante se dibuja si hay fila; rayo
  completo de 1.2 m solo si falla el panel.

## Tests
- Reglas: ALL PASS (pin M38).
- `evidence/test_duel_m38` en Quest: ALL DUEL TESTS PASS — `menu_ray ok`
  extendido (t/lx/ly exactos, acuerdo RayHit↔plano, 3 misses de plano).

## Boot
`am start` OK: 72 fps, sin FATAL.

## Validación en visor (pendiente)
Rayo termina exactamente en el diamante blanco; al apuntar fuera, rayo
completo sin punto.
