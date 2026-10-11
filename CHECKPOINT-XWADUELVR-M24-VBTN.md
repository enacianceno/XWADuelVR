# CHECKPOINT XWADuelVR M24-VBTN (3.30-M24-vbtn)

## Fecha
2026-10-10/11. Quest 3487C10H9T0BKZ (USB). APK
`XWADuelVR-M24-VBTN-debug.apk` (2242003 B, SHA256
`68A2D126E7F36D04A0C1EC0237507A9F4BA422FE199CAF59F7B176E41F09938A`),
dumpsys `versionName=3.30-M24-vbtn`. Instalado OK.

## Problema (de M23)
El usuario nunca vio la etiqueta PAUSA ni el boton de pausa cableado
(mesh40p1, supuesto par A/C central). Foto del usuario
(`org.openxwa.xwaduellvr-20261010-181508.jpg`) muestra el par real:
dos botones rojos CUADRADOS separados (~1.1 anchos), glifos oscuros
tipo A/Flecha-arriba y C/Flecha-arriba, panel negro inclinado junto a
la ventana derecha, 2 verdes en horizontal cerca.

## Diagnostico (dos bugs, no uno)
1. PAR EQUIVOCADO. M23 cableo el par CENTRAL de mesh40p1 (Tex17 A/C,
   ship x~-0.02). El par de la foto es el de la CONSOLA DERECHA:
   quads Tex16 celdas V (izq, se ve como A/flecha-arriba) y C (der),
   en mesh02p4, ship (+0.359,+0.471,-1.160) y (+0.400,+0.478,-1.140),
   separados 4.6 cm. Pruebas: barrido de las 111 texturas (unicos
   rojos con glifo: Tex16/Tex17); barrido mat-16 fila V/I/C (solo
   centro); barrido mat-16 v-bajo 0.05-0.35 -> par V/C derecho;
   raycast limpio (1 solo self-hit); aspecto foto 0.90 vs Tex17 1.89.
   Nota app: muestrea texturas con v=0 en imagen-arriba (Model N).
2. ETIQUETA SUBPIXEL. Glifos de 1.6 mm a 0.5-0.7 m ~ 2.7 px, trazos
   subpixel -> invisible aunque el draw path (emit+pipe+draw) estaba
   verificado correcto. Sin oclusores (raycast M23 y M24 limpios).

## Fix
- `DUEL_PAUSEBTN_SHIP` -> (+0.3590, +0.4713, -1.1595) (centro
  baricentrico del quad V en mesh02p4, uv 0.437,0.186).
- Etiqueta PAUSA: glifos 1.6 mm -> 8 mm, 16 mm bajo el centro del
  boton (libre del boton de 18 mm). ~86x17 px a 0.53 m, legible.
- Radios intactos: PRESS 0.03 / NEAR 0.06 (dedo en C a 4.6 cm no
  pulsa A).

## Tests
- `tests/test_render_rules.py`: ALL PASS (check M19/M24 fija coords +
  `"PAUSA", 0.0f, -0.016f, 0.008f`).
- Device `/data/local/tmp/test_duel_m24`: ALL DUEL TESTS PASS
  (nuevo `test_pausebtn`: lado derecho, alcance, mate 4-6 cm fuera
  de PRESS; primer intento fallo por check demasiado estricto
  PRESS < dm/2, corregido a PRESS < dm).

## Archivos
- `src/duel_hands.c`, `src/duel_hands.h`, `src/duel_render.c`
- `tests/test_render_rules.py`, `tests/test_duel.c`
- `app/build.gradle` (3.30-M24-vbtn)
- `evidence/m24-vbtn/`: vbtn_hunt.py, vbtn_map.py, vbtn_raycast.py,
  vic_scan.py, grn_scan.py

## Validacion pendiente en visor
Ver etiqueta PAUSA ambar bajo el boton A derecho; tocar A pausa y
trae el menu; tocar C no hace nada.
