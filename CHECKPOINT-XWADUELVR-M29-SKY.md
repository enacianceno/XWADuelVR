# CHECKPOINT XWADuelVR M29-SKY (mirror escaramuza + cielos del sitio)

## Fecha
2026-10-11. Quest 3487C10H9T0BKZ (WiFi nueva IP 192.168.68.101:5555).
APK `XWADuelVR-M29-SKY-debug.apk` (2247931 B, SHA256
`40A7C77FEDF75B9FA00C4997C2F10C8C5C16DFF5C308953F8A01999CC4AAC226`),
instalado OK (`versionName=3.30-M29-sky`).

## Qué hace
- FIX (reporte usuario: "el enemigo seleccionado no cambió"): los picks
  solo aplicaban al duelo; en escaramuza (modo por defecto) seguían
  saliendo TIEs. Ahora el mirror es total: ties vuelan ENEMIGA, aliados
  MI NAVE (exterior + cañones + brief). Balance congelado: HP/IA/radio
  siguen siendo de rol (M5 intacto).
- Muzzles estrictamente por airframe (`(void)owner`): un X-Wing enemigo
  en rieles IA dispara desde wingtips, no desde chin TIE.
- Brief escaramuza sigue el setup ("%s VS %d %s", "ALIADOS: %d %s").
- Envs: fuera Death Star (se veía mal en visor) y nebulosa procedural;
  dentro 2 Blue Nebulae LDR de spacespheremaps.com (PNG 10K → 2048x1024
  verbatim, `/tmp/cook_site_envs.py`, flare = píxel más brillante
  medido: u=0.2373/v=0.4707 y u=0.3066/v=0.4902). Cull desactivado
  (sin cuerpos). `duel_assets/env/`: space + site_blue1 + site_blue3.

## Validación
- render-rules ALL PASS (pins TIE/aliados actualizados + check M29).
- device `test_duel_m29` ALL PASS (mirror: ties[0].craft,
  muzzle len > 4; brief sigue setup; muzzle_world por craft).
- Boot OK vía adb (tras descartar Guardian): 72 fps, 4 modelos
  badidx=0, envs space + site_blue1 + site_blue3 (2048x1024),
  `DUEL_SKY` env0 838/env1-2 854, `DUEL draws` OK, sin FATAL.

## Tocados
`src/duel_sim.c` (mirror + muzzle), `src/duel_menu.c` (brief),
`src/duel_render.c` (draws tmod/amod), `src/duel_env.c` (tabla sitio),
`src/duel_main.c` (comentario), `tests/test_duel.c`,
`tests/test_render_rules.py`, `app/build.gradle` (3.30-M29-sky).

## Validación pendiente en visor (usuario)
- Descartar diálogo Guardian, lanzar, confirmar boot.
- Escaramuza con ENEMIGA = X-WING: enemigos X-Wing visibles.
- MI NAVE = TIE INT: aliados interceptor.
- Rondas bajo los 3 cielos (arena + 2 nebulosas del sitio).
