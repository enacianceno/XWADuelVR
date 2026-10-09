# M7 COMBAT-SIMULATION BASE (menú + pilotos + opciones) — ETAPAS 1-3

Date: 2026-10-09. Status: STAGES 1-3 COMPLETE (código + tests + APK).
Stage 4 (validación física) PENDIENTE: milestone NO validado en visor.
Repo: commit inicial `2a33a12` + commit M7 (ver §12).

Contexto: el usuario aprobó M6 ("milleston aprobado, ahora si es un
juego") y ordenó convertir XWADuelVR en la base jugable de XWAQuest
empezando por COMBAT SIMULATION, reutilizando lo rescatable de
XWAQuest y añadiendo pilotos con persistencia.

## 1. Preservación (frozen build)

Último build físicamente validado y aprobado:
- `XWADuelVR-M6-AXISSPLIT-debug.apk`, v3.6-M6-axissplit,
  2218055 bytes,
  SHA-256 `E28B730EB04BA0E6D198AC80DD536A1CA1A27A3382E2FD90245F5CC979EFBD9E`.
- Instalado en Quest 3487C10H9T0BKZ (dumpsys 3.6-M6-axissplit).
- Contenido validado: vuelo M1/P1/M2, manos M3, skirmish M5
  (3 TIEs + 2 aliados, TIE 120/0.75, sin fuego amigo),
  reparto M6 (derecha pitch+yaw puro, izquierda throttle+roll,
  sin virada coordinada), pop-gate de manos.
- APKs anteriores preservados en raíz (M3.8c, M5, toda la
  serie P0/M2/M3). No sobrescribir.

Git / relación entre proyectos (verificado 2026-10-09):
- `C:\OpenXWA\XWADuelVR`: repo git EXISTE pero SIN COMMITS
  (63 entradas untracked: src/, app/, assets/, tests/,
  tools/, checkpoints, APKs). Rama sin nacer. Recuperable =
  árbol en disco + APKs + este checkpoint. No se hizo
  commit (sin autorización); propuesta abajo.
- `C:\OpenXWA\XWAQuest`: repo SEPARADO con historial
  (HEAD `d327692 docs: add XWAQuest progress video...`) y
  cambios sin commitear (M XWAQUEST_MASTER.md,
  m8integrated1/*). READ-ONLY para este milestone: no se
  tocó ni se tocará.
- Relación: directorios hermanos, sin historial compartido,
  sin dependencias de build cruzadas (DuelVR solo usa
  third_party/SDL3 + wrapper gradle ajeno).

Lo que se conserva intacto (baseline M6): vuelo, manos,
agarres, disparo touch-fire, mapeo M6, sim/AI M5, audio,
asiento/cámara/HMD. El menú vive ENCIMA, no dentro.

## 2. Auditoría XWAQuest (read-only)

### 2.1 Menús 2D + flicker (CHECKPOINT-MENU-FLICKER-FIX.md)
- Los menús XWAQuest son el FRONTEND ORIGINAL del motor XWA
  (raster CPU 640x480, pumps `flight_display.c`
  PumpFrontendModalFrame + `frontend_task.c`, gate de timer
  legacy ~21 Hz) presentado en VR por el frame bridge.
- Fix validado: `m8integrated1/vr_modal_tick.h/.c` — fuerza
  exactamente un tick modal por present (verificado cuerpo
  del .h: `VrModal_ShouldForceTick`, estático, host-testeado).
- Veredictos físicos: FLICKPROBE-3 (2026-10-06) "MAIN MENU
  FIXED, HD GUILTY" (drop de capa HD en menús); resto del
  track = disco de daño + light probes del motor (no menú).
- Reutilización: el CÓDIGO no es portable (envuelve pumps
  del motor que DuelVR no tiene). Portable: la LECCIÓN
  (presentar solo después de dibujar; ticks modales
  conscientes del present) y el CONCEPTO (menú 2D plano
  presentado en VR, classic-only). Un menú nativo DuelVR
  dibuja cada present: la clase de flicker no puede ocurrir.
- `vr_ui_ray.h/.c` (ray-quad intersect CPU-testeado) es
  portable como IDEA, pero DuelVR necesita pinch de manos,
  no rayo de controller: adaptar, no copiar.

### 2.2 Pilotos / guardado motor original
- XWAQuest SÍ usa pilotos originales: binario `PilotData`
  (~152 KB, `neto0.plt` validado en m6/ESTADO.md con foto).
- Cuerpos verificados: `C:/OpenXWA/src/xwa/config/pilot.c`
  `Pilot_CreateNew` (:41, `<name>N.plt`),
  `Pilot_LoadFile` (:161), `Pilot_Save` (:305); struct en
  `src/xwa/frontend/mission_setup.h:215-256` (255 misiones
  tour + 255 combat chamber + 192 flight groups + emails +
  facciones + red; packed, acoplado a VFS Aeron,
  FrontendString, MissionSetup).
- Veredicto: NO reutilizable. Extraerlo arrastraría medio
  motor (VFS, strings, sesiones de misión) por un formato
  de campaña que DuelVR no necesita y no debe falsear.
- Decisión (opción que el usuario permitió): PERFIL PROPIO
  DuelVR, documentado, NUNCA presentado como piloto XWA.

## 3. Seams de integración DuelVR (cuerpos verificados)

- Main loop `src/duel_main.c:69-131`: poll XR + SDL →
  WaitBeginFrame → LocateViews → DuelInput_Poll → publish
  manos → `Duel_Update` → render ojos. Hook de menú: un
  app-state (MENU/COMBAT/PAUSED/RESULT) que compuerta
  publish + `Duel_Update`; el render corre siempre.
- Reset: `Duel_ResetCombat` restaura todo (fase, naves,
  oleada, láseres, contadores preservados); re-agarre
  exigible limpiando holders/anchors al cambiar de pantalla.
- Audio separado: `DUEL_AUDIO_MASTER` 0.8
  (`duel_audio.h:23`), `G_MUSIC` 0.65 / `G_ENGINE` 0.55 /
  G_FIRE/G_HIT/G_BOOM (`duel_audio_core.c`). Volumen
  general + música + efectos mapean limpio (master debe
  pasar de #define a estado).
- Recenter: `DuelXr_ConsumeSpaceChange` (`duel_xr.c:306`)
  + calibración de asiento existente. "Recentrar" = forzar
  recaptura por el mismo camino (nave/cockpit inmóviles).
- Foco: `duel_input.c:111` ya ignora input sin foco;
  `duel_xr.c:282-300` maneja STOPPING/EXITING/LOSS +
  space-change. Falta exponer FOCUSED/VISIBLE para
  pausar-al-perder-foco (adición pequeña, stage 2).
- Separación menú/vuelo: los 4 publish
  (HandThrottle/HandStick/HandLever/HandFire,
  `duel_main.c:106-127`) se saltan en MENU/PAUSED/RESULT:
  imposible disparar o agarrar desde un menú.
- Congelado: asiento, HMD cal, cámara, controles M6,
  sim/AI, audio, estrellas/RealSpace, manos/mallas.

## 4. Propuesta de integración (sin migrar ni renombrar)

NO fusionar repos, NO renombrar el proyecto, NO portar el
motor. Crecer DuelVR in place:

1. `src/duel_menu.*` (nativo): app-state + panel 2D
   world-locked + navegación manos (apuntar+pinch) y
   controllers (stick + trigger). Presentación 2D plana en
   VR = lo que el usuario pidió conservar.
2. `src/duel_profile.*` (nativo): pilotos + prefs en
   `files/` (misma raíz que `duel_assets`, sin VFS).
3. Pausa: estado PAUSED (sim detenida, render+tracking
   vivos) + auto-pausa al perder foco. Gesto de pausa:
   botón menu del controller (no colisiona con volar);
   manos: sin gesto (panel tiene botón Salir; decidir en
   stage 2 si hace falta pausa solo-manos).
4. Texto: DuelVR NO tiene renderer de texto. Stage 2 debe
   implementar bitmap-font mínimo (atlas cocinado offline
   + quads). Es el mayor trabajo nuevo del milestone.

TODO stage 2 detalla pantallas; stage 3 persistencia;
stage 4 validación física.

## 5. Formato de perfil propio (propuesta fija)

Texto key=value (sin JSON: C puro, parseo trivial).
Separado en dos archivos (prefs ≠ stats, orden del usuario):

`files/duel_prefs.cfg`
```
last_pilot=<nombre>
vol_master=0.8 vol_music=0.65 vol_sfx=1.0
stick_sens=1.0 stick_dead=0.15
```

`files/duel_pilots/<nombre>.profile`
```
name=<nombre>
combats=<n> wins=<n> losses=<n> kills=<n>
last_diff=<0/1/2> last_result=<win|loss|—>
```

Reglas: un combate registra EXACTAMENTE UNA VEZ al
decidirse (Victoria/Derrota en pantalla de fin);
reiniciar/abandonar a mitad NO registra; repetir tras fin
no duplica (flag por combate, se arma al empezar).
Nombres: solo [A-Za-z0-9_-], máx 16 (teclado VR mínimo o
lista + crear; stage 2 elige).
Último piloto: se restaura desde prefs al arrancar.
Prueba de cierre/reapertura obligatoria (stage 3).

## 6. Dificultad (solo si es real)

Niveles propuestos (constantes YA existentes, diferencias
comprobables en combate): falcil/normal/duro = sets de
TIE_HP / TIE_MAX_SPEED / TIE_TURN_RATE /
TIE_LASER_COOLDOWN (+ nº de TIEs fijo en 3). Si stage 2
no los cablea a la sim, NO se muestra el selector.

## 7. Riesgos

- Repo DuelVR sin commits: TODO depende del disco. Pedir
  autorización para commit inicial antes de stage 2.
- Texto VR desde cero (sin renderer previo).
- Teclado VR para nombres (minimizar: crear = nombre corto
  por defecto editable / lista).
- Pausa solo-manos sin gesto confusable (pendiente stage 2).
- XWAQuest tiene trabajo sin commitear: no tocar, no mezclar.

## 8. Plan por etapas

1. (ESTA) Preservación + propuesta. DONE, sin cambios de código.
2. Menú + ciclo duelo + pausa + fin (panel, pinch, controllers,
   texto bitmap, estados, re-agarre).
3. Opciones + pilotos + persistencia + prueba cierre/reapertura.
4. Validación física en Quest (protocolo abajo).

## 9. Protocolo de validación física (stage 4)

- Menú legible/estable; selección manos + controllers.
- Ciclo: menú → duelo → victoria/derrota → repetir + volver.
- Pausa: reanudar/reiniciar/opciones/salir; sin gestos fantasma.
- Foco perdido → pausa segura.
- Piloto: crear/elegir/restaurar; stats correctas, sin duplicados.
- Cierre/reapertura: nombre, piloto, prefs, resultados intactos.
- Sin regresión M6 (vuelo, manos, agarres, disparo, mapeo).

## Next step
UNO (2026-10-09): validación física en Quest del APK M7 (§10-11):
menú → duelo → pausa → resultado → repetir/volver, pilotos,
cierre/reapertura. Sin regresión M6.

---

## 10. Stage 2/3 implementado (2026-10-09, código)

Decisión de texto: STROKE FONT (no bitmap atlas): `src/duel_font.*`,
glifos CAPS-ASCII 5x7 como segmentos por el pipe dyn-line
existente. Cero assets, cero licencias, nítido en VR.

Módulos nuevos:
- `duel_font.*`: `DuelFont_Glyph`/`TextW`, set A-Z 0-9 `%:*.-+/() `
  `!?'-_<>=*`, minúsculas pliegan a mayúsculas.
- `duel_menu.*`: estados MENU/BRIEF/OPTIONS/PILOTS/COMBAT/PAUSED/
  RESULT; filas ACTION/CHOICE/SLIDER/LABEL; layout panel-local;
  `Nav`/`Adjust`/`ActivateRow`/`SetSlider`/`RowValue`.
- `duel_profile.*`: `duel_prefs.cfg` + `pilot_<NOMBRE>.cfg`
  (perfil NATIVO DuelVR, no piloto XWA); `Scan`/`Record`/`Delete`.

Pantallas: MENU (Combat Simulation, Piloto, Opciones, Salir),
BRIEF (X-Wing vs 3 TIE + 2 aliados, dificultad, Iniciar),
OPTIONS (volumen/música/efectos, sensibilidad 0.5-1.5,
zona muerta 0.05-0.30, recentrar), PILOTS (elegir/crear
PILOTO<n>/eliminar, stats V/D/bajas), PAUSED
(reanudar/reiniciar/opciones/menú), RESULT (victoria/derrota,
bajas, repetir/menú). Strings CAPS-ASCII español.

Render (`duel_render.c`): panel world-locked (colocado 1 vez por
pantalla, 1.1 m al frente, yaw de cabeza; NUNCA persigue).
Dos slices puros: tris (fondo/filas/sliders/borde) + líneas
(glifos + rayos de manos). Hover = pinch target; pausa conserva
HUD de combate, resto de pantallas lo oculta.

Input menú: manos = rayo + pinch (drag en sliders);
controllers = stick (nav/ajuste) + trigger/menu (activar).
Vuelo y menú SEPARADOS: publish de manos + `Duel_Update` solo
en COMBAT; al entrar a combate se exige soltar el fuego y
`ClearGrabs` exige re-agarre fresco.

Main (`duel_main.c`): pausa con botón menu + auto-pausa al
perder foco XR (`HasFocus`); sim congelada fuera de COMBAT
(render/tracking vivos); fin de combate detectado en el flanco
DESTROYED→COMBAT del auto-reset (conserva el beat de explosión
~2.5 s con input neutral); resultado registrado UNA vez por
combate (reiniciar/abandonar no registra).

Sim/audio (`duel_sim.*`, `duel_audio*`): dificultad 0/1/2 =
multiplicadores TIE HP 0.7/1.0/1.3, vel, giro, cadencia
(piloto = baseline M6 intacto, sobrevive resets); volúmenes
master/música/sfx en runtime; sens/deadzone aplicados a
sticks + manos (`SetHandDead`, `SetHandSens`).

Pendiente explícito: nombre de piloto a elegir (crear usa
PILOTO<n> auto; teclado VR diferido); pausa solo-manos sin
gesto (requiere controller); sin campaña/hangar/cuerpo piloto.

## 11. Pruebas + APK M7 (2026-10-09)

- CPU en Quest (`/data/local/tmp/test_duel_m7`): ALL DUEL TESTS
  PASS (99 tests: 91 previos + 8 M7: font/menu/values/strings/
  prefs/pilot/scan/difficulty).
- Reglas render (`test_render_rules.py`): 128/128 PASS
  (+7 M7: slices puros, sim solo en combate, resultado único,
  pausa+foco, re-agarre, baseline dificultad).
- Build: `Build-Target.ps1` + `gradlew assembleDebug --offline
  -x lint` → BUILD SUCCESSFUL. OJO: el primer APK salió con
  .so viejo (M6); se reconstruyó tras el nativo.
- APK: `XWADuelVR-M7-MENU-debug.apk`, v3.7-M7-menu,
  2408785 bytes,
  SHA-256 `68E346D8D2EC47097C9860297E3B18F8A63F506566F48630CC86785500C98E7B`.
  Instalado con `install -r` en 3487C10H9T0BKZ.
- Smoke adb: SIN crash (sin FATAL/SIGSEGV); arranque por
  `am start` falla limpio con `SESSION_NOT_READY` (-28, sin
  HMD/foco) — ambiental, igual que M6; el usuario lanza
  desde el visor.
- M6 preservado: `XWADuelVR-M6-AXISSPLIT-debug.apk` intacto
  (reinstalable si M7 regresa).

## 11b. Hotfix MENUa (2026-10-09, "juego congelado")

Síntoma físico: app a 72 fps, sim quieta en spawn, sin panel =
parecía congelada. Causa: los rangos del menú se EMITÍAN pero
nunca se DIBUJABAN (faltaban los `DrawGPUPrimitives`).
Fix: draws `pipe_tri_alpha` + `pipe_lines` tras el bloque HUD
+ regla de regresión "emit => draw". Segundo fix: el rayo de
manos leía `hw[][]` sin inicializar con hand-mesh activo;
ahora usa `JointWorld` directo.
- APK: `XWADuelVR-M7-MENUa-DRAWFIX-debug.apk`, v3.7-M7-menu-a,
  2234131 bytes,
  SHA-256 `48862BEBDA84BCBA3F9456B5A70E22287B1666B4B7411BD933A912C147FBD62C`.
  Instalado con `install -r` en 3487C10H9T0BKZ.
- Reglas: 129/129 PASS. CPU suite sin cambios (lógica nueva
  solo render).

## 11c. Hotfix MENUb (2026-10-09, "menú debajo de mí")

Causa: el anchor del panel se colocaba en espacio LOCAL del
HMD pero se dibujaba como si fuera espacio de juego (nave).
Fix: el eye pass convierte LOCAL→juego por la ruta exacta
del ojo/manos (`neutralize` + `eye_pose_motion`); docs +
regla "LOCAL=>game". Manos/rayos ya usaban esa ruta, ahora
panel y rayos coinciden en el mismo frame.
- APK: `XWADuelVR-M7-MENUB-PANELFIX-debug.apk`,
  v3.7-M7-menu-b,
  SHA-256 `C6001155405DEFD976A0F587DDD81268801B4A479BB8B9D0D4279ABEF9A75639`.
  Instalado con `install -r` en 3487C10H9T0BKZ.
- Reglas: 130/130 PASS.

## 12. Git

- `2a33a12` M6 approved baseline (commit inicial).
- Commit M7 (stage 2/3): 15 modificados + 6 nuevos
  (`duel_font/menu/profile.*`), ~1100 inserciones.
  XWAQuest y demás proyectos: intactos, read-only.
