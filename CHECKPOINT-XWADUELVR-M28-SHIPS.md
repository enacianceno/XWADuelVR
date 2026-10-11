# CHECKPOINT XWADuelVR M28-SHIPS (setup XWA: MI NAVE + ENEMIGA)

## Fecha
2026-10-11. Quest 3487C10H9T0BKZ (WiFi 192.168.68.100:5555, tras reboot
+ red + diálogo del sistema descartado por el usuario). APK
`XWADuelVR-M28-SHIPS-debug.apk` (2247967 B, SHA256
`1A2F884914C9E332583AAAB73D7F2E59573F583748B979633DFEA70D22267105`),
instalado OK (`versionName=3.30-M28-ships`) + boot OK a 72 fps.

## Qué hace
- Setup estilo XWA en el BRIEF: filas MI NAVE + ENEMIGA (X-WING / TIE /
  TIE INT, BRIEF ahora 11 filas) + línea de versus
  ("X-WING VS TIE INT"). Duelo por defecto: X-Wing vs TIE Interceptor.
- `DuelCraft` separa ROL (kind: quién pega a quién) de AIRFRAME (craft:
  stats/geometría/muzzles). La cabina VR sigue siendo X-Wing; el pick
  cambia el modelo de vuelo en todos lados y el exterior en peer/IA.
- TIE Interceptor (XWAU `TIEINTERCEPTOR.OPT` 2.4 MB → glTF vía
  opt2gltf compilado para Android y corrido EN el Quest): 185 m/s,
  pitch/yaw/roll 1.9/1.1/2.6, 60 HP sin escudos, radio 5.0, 4 cañones
  wingtip medidos del glTF (x=±3.14, y=±1.05, z=6.71).
  `TieInterceptor/` instalado en `duel_assets/` (gltf+bin+32 tex).
- Peer del duelo usa el exterior de ENEMIGA (fallback a TIE si falta
  el modelo); barra de casco + bursts dimensionados por nave
  (`Duel_CraftHP/Radius`). Picks aplican al DUELO; escaramuza sigue
  X-Wing+aliados vs TIEs (mirror pendiente).

## Validación en dispositivo (log + suites)
- `DUEL model TieInterceptor: 56876 verts 25260 idx 86 prims 26 texs
  24 meshes ... badidx=0`.
- `DUEL draws: exterior=0 cockpit=1 ties=3 allies=2 xwingvbo=1`.
- render-rules ALL PASS (pin MODO/peer actualizado + 2 checks M28);
  device `test_duel_m28` ALL PASS (incluye `craft`: tabla, clamp,
  picks sobreviven resets, envelope INT > XW, muzzles INT + chin TIE).

## Tocados
`src/duel_sim.h/.c`, `src/duel_menu.h/.c`, `src/duel_main.c`,
`src/duel_render.c`, `src/flight_config.h`, `tests/test_duel.c`
(BRIEF 9→11, INICIAR fila 9), `tests/test_render_rules.py`,
`app/build.gradle` (3.30-M28-ships), `evidence/m28-ships/`,
`evidence/opt2gltf` (conversor Android).

## Validación pendiente en visor (usuario)
- Duelo XW vs INT: ¿se ve el Interceptor enemigo? ¿Sus 4 láseres salen
  de los wingtips? ¿Se siente más rápido/frágil?
- Cambiar MI NAVE/ENEMIGA y confirmar stats + brief.
- Siguiente en cola: asteroides sólidos (Asteroid01-03.OPT localizados)
  y más fondos de spacespheremaps.com.
