# UAT — Plan de prueba de usuario (XWADuelVR)

Build bajo prueba: **3.30-M26-txt** (`XWADuelVR-M26-TXT-debug.apk`,
paquete `org.openxwa.xwaduellvr`).
Visor documentado: Quest con seguimiento de manos y mandos Touch.

## Instalación

```powershell
adb install -r XWADuelVR-M26-TXT-debug.apk
```

Sin desinstalar la anterior. Verifica la versión instalada:

```powershell
adb shell dumpsys package org.openxwa.xwaduellvr | Select-String versionName
# esperado: versionName=3.30-M26-txt
```

## Casos de prueba

1. **Menú principal** — Toca DIFICULTAD, BATALLA y RONDAS con el
   dedo; cambia valores; pulsa INICIAR. Esperado: responde al toque,
   entra en combate en la ronda 1.
2. **Tablet en mano** — Pellizca un LADO del menú (no esquinas ni
   arriba/abajo) y mueve la mano; gira la muñeca; suelta. Esperado:
   el menú sigue a la mano como tableta y se queda donde lo sueltas.
3. **Combate** — Agarra la palanca con el puño cerrado y vuela;
   dispara con el pulgar; juega una ronda completa. Esperado: control
   continuo, resultado al terminar, avance de ronda.
4. **Rondas** — Pon RONDAS = 1 y luego = 5; juega cada serie.
   Esperado: la línea muestra `RONDA X DE N GANADAS: W` y la serie
   termina donde debe.
5. **Botón de pausa** — En combate, toca con el dedo índice el botón
   rojo **A** (izquierda del par A/C junto a la ventana derecha).
   Esperado: pausa + vuelve el menú; la etiqueta PAUSA cambia de
   color al acercar/pulsar. Tocar el botón **C** no debe hacer nada.
6. **Recentrado** — Desde el menú en pausa usa RECENTRAR. Esperado:
   el menú vuelve a su posición inicial frente al piloto.

## Cómo reportar

Abre un issue con:

- Visor y build (p. ej. Quest 3S, 3.30-M26-txt).
- Caso y pasos exactos.
- Esperado vs observado.
- Foto o video si aplica (¡ayudan muchísimo!).

Gracias por probar. Cada reporte con foto vale oro.
