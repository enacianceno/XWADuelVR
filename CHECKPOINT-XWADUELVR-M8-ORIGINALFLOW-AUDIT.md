# M7→M8 AUDITORÍA FLUJO ORIGINAL XWA (sin cambios de código)

Date: 2026-10-09. Status: AUDIT COMPLETE. No se modificó ningún
código, repo ni APK. Milestone M7-menú-simplificado DETENIDO por
orden del usuario: no expandirlo como solución final.

Orden: XWAQuest debe ser adaptación de X-Wing Alliance a Quest/VR
con inicio/menús/opciones/flujo ORIGINALES, aprovechando OpenXWA
y XWAQuest. Primer alcance: Combat Simulator vía flujo original.

## 1. Preservación (verificada 2026-10-09)

- DuelVR repo LIMPIO en `23117a7` (M7 hotfix MENUb). Sin reset/clean.
- Instalado en Quest: M7-MENUB v3.7-M7-menu-b (panel放置 fix pendiente
  de prueba física; irrelevante si se adopta flujo original).
- Preservados en disco: M6-AXISSPLIT (baseline validado M6),
  M7-MENU, M7-MENUa, plus serie P0/M2/M3/M5. No sobrescribir.
- XWAQuest: READ-ONLY en esta sesión (solo `git log/status`,
  lectura de checkpoints/código). Cambios pendientes ajenos
  intactos (XWAQUEST_MASTER.md, m8integrated1/*, etc.).
- Motor `C:\OpenXWA\src`: solo lectura.

## 2. Qué pantallas/funciones originales YA funcionan en XWAQuest

Evidencia por checkpoint (físicamente validado = usuario en visor):

- Arranque + frontend original 2D en VR (M5 2040+ ticks, intros
  SMUSH, concourse visible). Base preservada.
- Creación de piloto ORIGINAL: `neto` → `neto0.plt` 152076 bytes
  vía `Pilot_CreateNew` + `Pilot_Save` reales, sala concourse
  después (m6/ESTADO.md + foto pilot-created.png). Nombres
  ASCII 1-12 vía IME Android (M6, panel 2D pre-XR).
- Listas de misiones originales: `missions/mission.lst` +
  `melee/mission.lst` leídas correctamente (M6).
- Navegación a Combat Simulator + vuelos: M8 Run-1..Run-5 entran
  a vuelos Combat Simulator (misma misión Run-1), ship-select
  visible, vuelo inmersivo estéreo con telemetría.
- Vuelo con motor original en VR: 1080+ ticks, misiles
  confirmados funcionando, controles Touch (pitch/roll/yaw +
  throttle) probados en inmersivo.
- Resultados/debrief NATIVOS preservados: SP1/SP2
  (`simulator_return.c`) conserva logging de resultados y
  guardado; retorno escaramuza→setup implementado (CPU PASS,
  validación física pendiente).
- Guardado piloto: `.plt` real preservado entre `install -r`.
- GameData: 7744 rutas con manifest SHA-256 (GAMEDATA.md).

## 3. Arreglos 2D/parpadeo ya hechos (CHECKPOINT-MENU-FLICKER-FIX)

- Causa raíz probada con probes: doble-present (FLICKPROBE-1)
  + capa HD reconstruye texto sin replay (FLICKPROBE-3).
- Main menu: ARREGLADO (classic-only, HD drop; veredicto usuario
  "éxito rotundo con las letras").
- Fix genérico `rt_last_complete` en ddraw (válido, conservado).
- In-flight B menu: probe-4 (modal-aware drop) construido,
  veredicto pendiente.
- `vr_modal_tick` (1 tick por present): insuficiente solo, pero
  base correcta para cadencia 72 Hz.
- Cursor rayo: INTENTADO Y REVERTIDO (seguía posición, no
  dirección). Quedó stick-cursor + `vr_ui_ray` dormido testeado.
- Daño disco: mecanismo probado (timers 59 ticks + overlay);
  DMG-D parcial (colores fuera, shimmer residual aparte).

## 4. Qué falta: inicio original → Combat Simulator (en XWAQuest)

Funcional (código existente, pendiente validar/conectar):
- F6/SP1-SP2: retorno vuelo→setup validado en CPU, NO en visor.
- Selección de piloto EXISTENTE (crear probado; elegir entre
  varios + persistencia cierre/reapertura sin protocolo corrido).
- B-menu flicker: veredicto probe-4 pendiente.
- Bindings (KNOWN-ISSUES F1-F5): órdenes wingman, targeting
  extra, MFD/mapa, sistemas nave (link/foil/CM/escudos) sin
  mapear a Quest. Crítico para misiones, parcial para duelo.
- F8: MISBOAT sin manifest; cockpits fallback en varios TIE.
- Manos VR: CERO en XWAQuest (V7 deferred, sin hand-tracking).

Ausente (no existe):
- Manos/palancas/disparo por gestos (solo existe en DuelVR M6).
- Calibración asiento/HMD estilo DuelVR (XWAQuest usa otro
  esquema de cámara/cockpit).
- Test documentado cierre/reapertura de piloto.

## 5. Cómo conectar con el vuelo VR actual: EL FORK

Hecho central: DuelVR y XWAQuest vuelan SIMULADORES DISTINTOS.

- DuelVR M6: sim PROPIA (física/AI/daño/ waxwing-vs-ties),
  visual XWAUP, manos+palancas+disparo+asiento validados.
  NO corre misiones originales, NO usa motor XWA.
- XWAQuest M8: motor XWA ORIGINAL (vuelo, AI, misiones,
  resultados, guardado) presentado en VR inmersivo.
  Controles Touch, sin manos.

PATH A — motor dentro de DuelVR (lectura literal del pedido):
injertar Aeron + OpenXWA + VFS + GameData 757MB + frontend
640x480 + puente de input + secuestro del "launch" (misión
original → spawn de la sim DuelVR) dentro de la app DuelVR.
= Rehacer M6+M7+M8 dentro de DuelVR. Meses. Los "resultados"
no serían originales (la sim DuelVR no corre la misión).
Duplica XWAQuest.

PATH B — UX DuelVR dentro de XWAQuest (recomendado):
XWAQuest M8 YA ES el flujo original end-to-end. Portar a M8
la capa UX validada de DuelVR: hand-tracking + palancas
físicas + disparo touch + asiento/calibración + motion-feel,
como reemplazo/aumento del vuelo Touch. El V7 de XWAQuest
YA PIDE manos virtuales. Flujo/resultados/pilotos/guardado
siguen 100% originales. DuelVR queda como laboratorio UX
y fallback intacto.

Dependencias/funciones que faltan (ambos paths):
- A: TODO el motor (Aeron, VFS, config/pilot, frontend_task,
  pumps, dd-shim, GameData provisioning, audio iMUSE/SDL).
- B: port de `duel_hands` + grab/lever/fire + seat-cal +
  panel-pausa? (M8 ya tiene B-menu) a la arquitectura M8
  (frame_bridge/input_xr), manteniendo pumps del motor.

## 6. Propuesta por etapas (sujeta a decisión A/B)

- S0 (esta): preservación + auditoría. DONE.
- S1: cerrar validaciones M8 pendientes (SP1/SP2 en visor,
  probe-4 B-menu, cierre/reapertura piloto). Sin código nuevo.
- S2: bindings mínimos Quest para Combat Simulator
  (disparo/ targeting básico ya existen; completar lo que
  pida el duelo).
- S3 (path B): port manos+palancas+asiento DuelVR→M8 por
  capas, con A/B Touch siempre disponible.
- S4: recorrido completo original en visor + checkpoint.

## 7. Protocolo de validación (usuario en visor)

Inicio original → crear/seleccionar piloto → menús originales
→ Combat Simulator → configurar → volar → resultados →
volver al menú → cerrar/abrir → piloto y datos intactos.

## Next step
UNO: decisión de arquitectura PATH A vs PATH B (pregunta al
usuario abajo). Sin esa respuesta no se escribe código: los
implementaciones divergen totalmente.
