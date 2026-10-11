# Checkpoint M32 — Crucero: subsistemas destruibles (torretas/domos/puente)

## Versión
- `versionName 3.30-M32-subs`, APK `XWADuelVR-M32-SUBS-debug.apk` (superado por M33).
- Quest `3487C10H9T0BKZ` @ `192.168.68.101:5555`.

## Pedido del usuario
En CRUCERO con 0 enemigos: ENEMIGA bloqueada; destruir armas (torretas), las
2 esferas (domos) y el puente entre ellas, señalados con diamante + barra de
vida, ~15 disparos certeros cada uno; la zona media estaba en negro sin textura.

## Implementación
- `src/duel_sim.h/.c`: `DUEL_CRUISER_SUBS 13` (10 torretas + 2 domos + puente),
  HP 180/180/360, radios 25/30/70; `Duel_CruiserSubPos/MaxHP`; volley salta
  torretas muertas y calla si el puente cae; domos muertos +50% daño al casco c/u.
- **Fix real**: los mounts están DENTRO de las esferas del casco (torreta de proa
  a 119 m en esfera r170) — `duel_cruiser_sub_redirect` redirige el bolt al sub
  vivo cuando la línea de vuelo lo atraviesa e impacto <200 m. (Primer intento
  con proyección clampada no disparaba; va sin clampar.)
- `src/duel_render.c`: diamantes ámbar (R 20 / 50 puente) + micro-barras por sub.
- `src/duel_menu.c`: ENEMIGA muestra `---` y no cicla si `duel_mode==2 && foes_n==0`.
- Textura: `ISD2_Tex08.png` (puente) era la única negra (media 0.0); parchada
  desde Tex07 a `evidence/m30-cruiser/tex_patch/` y copiada al Quest.

## Tests
- `tests/test_render_rules.py`: ALL PASS (pin M32 + redirect).
- `evidence/test_duel_m32` en Quest: ALL DUEL TESTS PASS (1 aimed bolt a torreta,
  grind 15 bolts, silencio por torretas/puente, domos doblan a 24).
- M30 point-blank movido a x=100 (disparar por el eje alimenta a la torreta de proa).

## Validación en visor
PENDIENTE (el usuario pasó a M33): diamantes/barras, 15 hits por torreta,
efectos domos/puente, ENEMIGA `---`, puente texturado.
