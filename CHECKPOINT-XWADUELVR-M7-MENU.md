# M7 COMBAT-SIMULATION BASE (menú + pilotos + opciones) — ETAPA 1

Date: 2026-10-09. Status: STAGE 1 COMPLETE (preservación + auditoría +
propuesta). No se migró ni renombró nada. Milestone NO validado
físicamente: stages 2-4 pendientes.

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
UNO: pedir autorización para (a) commit inicial del árbol
DuelVR y (b) arrancar stage 2 (menú + ciclo de duelo).
