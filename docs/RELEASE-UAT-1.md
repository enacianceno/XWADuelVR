# Release UAT 1 — M26-TXT (`3.30-M26-txt`)

Primera APK para prueba de usuario (UAT) de XWADuelVR.

**APK:** `XWADuelVR-M26-TXT-debug.apk` (2 242 007 bytes, SHA-256
`DE068FF124BA622EE8BA49A3223C26EAB68B0A6E66FEDA67B9E24171C02CAADE`)

## Qué probar

- Menú touch + tablet agarrada de los lados.
- Combate X-Wing vs TIE, rondas 1/3/5.
- Botón físico de pausa **A** (par A/C, consola derecha).
- Plan completo y plantilla de reporte: [docs/UAT.md](docs/UAT.md).

## Instalación

```powershell
adb install -r XWADuelVR-M26-TXT-debug.apk
```

Paquete `org.openxwa.xwaduellvr`. Quest 3/3S, modo desarrollador.

## Aviso

Proyecto experimental de fans, sin afiliación con los titulares de
X-Wing Alliance / Star Wars ni con Meta. Sin fines de lucro, sin
datos originales del juego. Úsalo bajo tu propio riesgo. Reporta en
Issues con visor, build, pasos y foto o video.
