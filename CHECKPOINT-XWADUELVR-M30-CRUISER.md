# CHECKPOINT XWADuelVR M30-CRUISER (MODO CRUCERO vs ISD)

## Fecha
2026-10-11. Quest 3487C10H9T0BKZ (WiFi 192.168.68.101:5555).
APK `XWADuelVR-M30-CRUISER-debug.apk` (2250859 B, SHA256
`8C98DD60987A81D02BB003619909ED7DD1A8C83A8D77A8DDD0F3237B5973643A`),
instalado OK (`versionName=3.30-M30-cruiser`).

## Qué hace
- MODO 3-way: ESCARAMUZA / DUELO / CRUCERO. En CRUCERO: tú + 2
  escoltas (nave de MI NAVE) vs un Imperial Star Destroyer (XWAU
  `IMPERIALSTARDESTROYER2.OPT` 33.9 MB → glTF vía opt2gltf en Quest:
  71 meshes, 260 prims, 35 texs, bin 1.8 MB) instalado en
  `duel_assets/StarDestroyer/`.
- Casco medido del glTF: 903×431×1612 m, proa +Z. Colisión = 6
  esferas (proa/popas/puente, sobre-cobertura ~15%). Torretas: 10
  (8 trincheras laterales + 2 proa, centros medidos).
- Torretas disparan voleas rotativas (0.5 s) al rebelde más cercano
  con lead + wobble determinista; bolts imperiales 700 m/s, alcance
  2450 m, daño TIE. Respuesta al fuego visible en el casco (bursts
  rotativos del lado del jugador); muerte = explosión por etapas en
  las 6 esferas + casco queda como naufragio.
- Raspado de casco: 25 dps + freno a 40 m/s dentro de esferas.
- Cue/lead-pip/flecha + barra derecha siguen al crucero
  (pseudo-nave estática); barra izquierda del jugador ahora
  dimensionada por nave (fix: interceptor mostraba 60%).
- Flujo escaramuza: victoria = ISD muerto + tú vivo; auto-reset +
  RESULT/series sin cambios. Offline (sin net).

## Validación
- render-rules ALL PASS (5 cargas incl. StarDestroyer + check M30).
- device `test_duel_m30` ALL PASS (crucero: esferas, voleas,
  kill-path, silencio post-muerte, auto-reset, raspado; MODO 3-cycle
  + brief "X VS ISD"/"TORRETAS: 10").
- Boot-check pendiente: instalación OK pero el visor estaba dormido
  y luego el usuario activo en otras apps (timeout de window
  placement). Lanzar desde el visor y confirmar.

## Tocados
`src/duel_sim.h/.c`, `src/duel_menu.h/.c`, `src/duel_main.c`,
`src/duel_render.c`, `src/flight_config.h`, `tests/test_duel.c`,
`tests/test_render_rules.py`, `app/build.gradle` (3.30-M30-cruiser),
`evidence/m30-cruiser/`.

## Validación pendiente en visor (usuario)
- MODO → CRUCERO, INICIAR: ¿aparece el ISD de frente a 2.2 km?
- Acercarse bajo fuego, pegar al casco (bursts), matarlo (explosión
  + VICTORIA). ¿Dificultad OK? ¿Torretas justas?
- Siguiente en cola: asteroides sólidos, más cielos, push/UAT.
