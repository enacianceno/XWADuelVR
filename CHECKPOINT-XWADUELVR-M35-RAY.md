# Checkpoint M35 — Flicker, respawn en sitio, HP% en tablero, rayo de mando

## Versión
- `versionName 3.30-M35-ray`.
- APK `XWADuelVR-M35-RAY-debug.apk`, SHA256
  `17AC11F1857D9224EB9D5FCC162D1221B1F7393418302405BE4F30FFA09F94B5`.
- Instalado OK (`Success`), Quest `3487C10H9T0BKZ` @ `192.168.68.101:5555`.

## Pedidos (4 en uno)
1. El menú parpadea otra vez con rayas inclinadas.
2. CONTINUAR debe reaparecer donde murió, con más vida, no reiniciar.
3. Barra de vida en el tablero frontal: número con % de 100 a 0 en la
   pantalla negra.
4. Opción de usar controles (mismo uso que XWAQuestVR): rayo para apuntar
   al menú + gatillo para seleccionar.

## 1. Flicker: causa + fix
- Causa: `menu_hide_hud` se ESCRIBÍA pero nunca se LEÍA — el HUD de combate
  (cue/diamantes/lead/barras + 13 diamantes M32, todo no-depth) se dibujaba
  ENCIMA del menú en cada pantalla. Con M32 el síntoma explotó.
- Fix `src/duel_render.c`: gate `if (!r->menu_hide_hud)` en cue/diamond/lead/
  subs y en las barras NDC (retícula intacta, fuera del contrato M7).
- Fix `src/duel_main.c`: `DuelRender_SetCombatHud(render, 1)` en las 3
  entradas a combate (START/RESTART/REPEAT, RESUME, CONTINUE) — antes solo
  se apagaba y el HUD habría quedado oculto tras el primer menú.

## 2. Respawn en sitio
- `Duel_RespawnPlayer`: conserva pose/velocidad/throttle del punto de muerte,
  restaura HP lleno + 0.5 s de gracia de cañón. Resto intacto (M34).
- Test `continue` actualizado: pin de posición de muerte (no origen).

## 3. HP% en tablero
- Medición: dash frontal = strip mesh0p11 (única geometría al frente);
  ancla `DUEL_DASHHP_SHIP (0, 0.26, -0.95)` flotando 3 cm al piloto.
- Readout `"100%".."0%"` (craft-sized, verde>50/ámbar>25/rojo), glifos 3.2 mm
  probados + pipe depth-tested del rango PAUSA (solo combate).
- Nota: la "pantalla negra" es región de textura, no prim propio (Tex03 negro
  es piso). Si el ancla no cae sobre negro, mover con foto del visor.

## 4. Controles estilo XWAQuestVR
- Referencia (solo lectura): `C:/OpenXWA/XWAQuest/m8input1/input_xr.c` —
  A=confirm, B=back, sticks navegan, gatillo=clic.
- `src/duel_input.c`: acciones A/B (boolean) + aim L/R (pose→spaces,
  locate por poll, degradan a rayo apagado). Log:
  `sticks+triggers+menubtn+A/B+aim` (verificado en logcat).
- `DuelInput`: `btn_a/btn_b` + `aim_valid/pos/quat[2]` (codec net intacto:
  usa struct separado).
- `DuelMenu_RayHit` (puro/testeado): rayo→plano→fila. Fila OPCIONES
  `RAYO MANDO: SI/NO` (índice 5, sliders intactos 0-4, default SI).
- Render: `DuelRender_ControllerAim` publica poses; fallback de rayo tras
  el ganador táctil (tacto manda, derecha gana empates, funciona sin manos);
  rayos cian 1.2 m depth-tested en el pase de líneas del menú.
- Main: publica aim por frame; A confirma highlight (como gatillo);
  B = atrás (pausa→reanudar, resto→salir; en RESULT = no continuar).

## Tests
- Reglas: ALL PASS (3 pins M35; regla M13 re-apuntada al nuevo contrato: dedos sin rayo,
  mandos con rayo gated — el usuario ordenó el cambio).
- `evidence/test_duel_m35` en Quest: ALL DUEL TESTS PASS, incl. nuevo
  `menu_ray ok` (RayHit centro de cada fila + 6 misses, opción SI/NO/click,
  sliders sin shift, ancla dash) y `continue ok` (respawn en sitio).

## Boot
`am start` OK: input ready A/B+aim, menú, 72 fps, sin FATAL.

## Validación en visor (todo pendiente del usuario)
1. Menú sin diagonales en ninguna pantalla (mover cabeza).
2. Morir→CONTINUAR: reaparecer en el mismo punto con 100%.
3. `100%` visible sobre el tablero al mirar abajo (color por nivel).
4. Rayos cian desde los mandos; apuntar+gatillo (o A) selecciona;
   B regresa; OPCIONES→RAYO MANDO apaga el rayo.
