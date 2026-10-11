# CHECKPOINT XWADuelVR M27-SKY (fondos random por partida)

## Fecha
2026-10-10/11. Quest 3487C10H9T0BKZ (WiFi 192.168.68.100:5555). APK
`XWADuelVR-M27-SKY-debug.apk` (2247007 B, SHA256
`45BB0F0B0A9E58C64908CFECCC58455E2D08120032BB04B7363543D9404E707B`),
instalado OK + boot OK (72 fps, sin crash; verificado en el boot M28,
mismo código de render/env).

## Qué hace
- 3 escenas precargadas al arranque: arena (`space_env.png`, la de
  siempre), Death Star (`deathstar_env.png`, disco r~12° en
  u=0.40/v=0.49) y nebulosa (`nebula_env.png`, sin cuerpo, sol rojo
  tenue). Tabla en `src/duel_env.h/.c`.
- Cada partida/ronda elige escena con `DuelEnv_Pick` (LCG XWA, puro:
  misma semilla = misma escena). El swap es solo un índice
  (`DuelRender_SetEnv`): sin cargas GPU a mitad de frame.
- Un VBO de estrellas por escena (el cull del disco sigue a cada
  cuerpo; env 0 conserva los números M2.2 verbales). Nebulosa: cull
  desactivado (cull_dot 2.0). Log: `DUEL_SKY env=N ... culled_body=N`.
- Envs generados con `evidence/m27-sky/mk_envs.py` (Death Star aprobado
  en preview, nebulosa aclarada 1.6x). Los 3 PNGs viven en
  `duel_assets/env/` del Quest (los .hdr originales de
  spacespheremaps.com llegan en otro milestone).

## Validación en dispositivo (log)
- `DUEL_ENV space_env.png 2048x1024` +
  `deathstar_env.png 2048x1024` + `nebula_env.png 2048x1024`.
- `DUEL_SKY env=0 drawn=838 culled_body=16`, `env=1 drawn=828
  culled_body=26`, `env=2 drawn=854 culled_body=0`.
- `DUEL_ENV active=0 file=space_env.png` al arrancar.
- Suite: render-rules ALL PASS (2 checks M27 nuevos) + device
  `test_duel_m27` ALL PASS (incluye `env_pick`).

## Tocados
`src/duel_env.h/.c` (nuevos), `src/duel_render.c/.h`, `src/duel_main.c`,
`tests/test_duel.c`, `tests/test_render_rules.py`, `app/build.gradle`
(3.30-M27-sky), `CMakeLists.txt`, `evidence/m27-sky/mk_envs.py`.

## Validación pendiente en visor (usuario)
- Probar varias rondas: ¿salen las 3 escenas? ¿El disco de la Death
  Star se ve bien de tamaño/posición? ¿La nebulosa brilla suficiente?
