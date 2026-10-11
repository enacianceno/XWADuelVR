# Release UAT 2 — MP-B1 (`3.30-MPB1-net`)

Segunda APK para prueba de usuario (UAT) de XWADuelVR. Novedad:
**modo duelo** — X-Wing vs X-Wing por red contra el piloto dron.

**APK:** `XWADuelVR-MPB1-NET-debug.apk` (2 246 555 bytes, SHA-256
`AA78E8A3E805B035066A2678B57C446210C05B1E840122C7D5D6D039E6232D20`)

**Dron (opcional):** `duel_drone` (binario Quest, adjunto abajo).
Llévalo al visor y arráncalo antes de entrar a DUELO:

```powershell
adb push duel_drone /data/local/tmp/duel_drone
adb shell "chmod +x /data/local/tmp/duel_drone"
adb shell /data/local/tmp/duel_drone
```

## Qué probar

- Todo lo del UAT 1 (menú, tablet, combate, rondas, pausa).
- MODO: DUELO en el brief + INICIAR (con el dron corriendo).
- Rival con diamante rojo que persigue y dispara verde;
  VICTORIA/DERROTA al terminar.
- Plan completo y plantilla de reporte:
  [docs/UAT.md](https://github.com/enacianceno/XWADuelVR/blob/master/docs/UAT.md).

## Instalación

```powershell
adb install -r XWADuelVR-MPB1-NET-debug.apk
```

Paquete `org.openxwa.xwaduellvr`. Quest 3/3S, modo desarrollador.

## Aviso

Proyecto experimental de fans, sin afiliación con los titulares de
X-Wing Alliance / Star Wars ni con Meta. Sin fines de lucro, sin
datos originales del juego. Úsalo bajo tu propio riesgo. Reporta en
Issues con visor, build, pasos y foto o video.
