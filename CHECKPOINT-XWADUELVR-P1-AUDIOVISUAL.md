# XWADuelVR P1 Audiovisual Authenticity

## Milestone 1 frozen baseline
P0.5b user-validated in HMD: seated pilot, M8 controls, body-local axes,
72 fps. See `CHECKPOINT-XWADUELVR-P0.md` (frozen; do not edit for P1).
APK `XWADuelVR-P0.5-SEATFWD40-debug.apk` preserved. Frozen systems: seat,
HMD calibration, camera hierarchy, controls, quaternion fix, TIE AI,
collision, damage, combat loop, OpenXR, renderer backend.

## XWA/XWAUP resource audit
Source (READ-ONLY): `C:\games\XWAU-Mods\XWAU2025\` + `C:\OpenXWA\src\xwa`
(OpenXWA engine sources) + `C:\OpenXWA\XWAQuest` (read-only knowledge).
No files modified outside `C:\OpenXWA\XWADuelVR`.

## Starfield source
ORIGINAL XWA (engine-generated, fully recoverable): 2560 stars,
LCG `seed = 9421*seed + 1` (math2.c), 128x128 angular-bin reservation
for even coverage (512 direct + 2048 angular), Q15 direction vectors,
4 twinkle palette cycles (39/64/45/57 steps, brightness 0..23).
Ref: `src/xwa/flight/starfield.c` FlightStarfield_Init.
XWAUP: `Effects/DefaultStarfield.dds` (RGBA cubemap 1024^2, 25 MB).
Current DuelVR: 1600 uniform-random points, no twinkle -> REPLACE P1.2.

## Backdrop source
`Resdata/BACKDROP.DAT` (2.8 MB) + `BACKDROP_SPECIAL.DAT` + `WRAPBACK.DAT`
+ `NEBULA.DAT` (9.5 MB): paletted sprite groups (format in
`sprite_resource.h`: Sprite header + payload). Mission files place
backdrops by sprite id + scale (`BackdropScales.txt`). XWAUP: HD CubeMap
JPG sets in `Effects/CubeMaps/<Name>/` (6 faces each: Nebula1, Kesselv2,
Nasa1/2, ...). Extraction of DAT sprites needs an offline decoder (TBD).

## Planets
`Resdata/PLANET.DAT` (1.76 GB) + `Planet2.dat` + `PlanetCustom.dat` +
`FRONTPLANET.DAT`. Same sprite-group decoding path as backdrops.
Current DuelVR: procedural band-shader sphere + moon -> REPLACE P1.3
with real artwork (DAT extract or CubeMap JPG), native VR presentation.

## Space particles
FINDING: original XWA has NO dust/speed particles in normal flight;
only hyperspace streaks (`g_hyperspaceStarStreaks`). A "warp dust" P1.4
would be non-canonical. DECISION NEEDED: skip P1.4 (authentic) or add
subtle near-field dust (non-canonical, helps motion feel).

## Laser FX
Visuals are engine-drawn bolts (long, additive core+glow). XWAUP
`Hooks.ini [hook_weapon_color]`: rebel `FF3300`, imperial `00FF33`;
impact colors red/green/blue/purple/yellow by weapon id.
Current DuelVR: flat 14x0.7 m camera quads, solid colors, no core/glow
-> REPLACE P1.5 (presentation only; sim untouched).

## Impact FX
`SPARKS.DAT` (508 KB) + `PARTICLES.DAT` + `BLASTMARKS.DAT` + impact
colors above. Attach to collision points; collision math frozen.

## Explosion FX
`EXPLOSIONS.DAT` (378 MB) + `ExplosionsAlt[1-7].dat` + `DEBRIS.DAT` +
`TRAILS.DAT` + `CHAFF.DAT` + `DSFIRE.DAT` + `LIGHTINGEFFECTS.DAT`.
Current DuelVR: 120-point CPU burst + fullscreen flash -> REPLACE P1.7
with authentic sprite/debris/timing recreation. Death/reset sim frozen.

## Audio resources
All local WAV, directly reusable (private use, never redistributed):
- X-wing laser: `Wave/Sfx_Weapon/RebelLaser.WAV` (19 KB)
- TIE laser: `Wave/Sfx_Weapon/EmpireLaser.wav` (36 KB)
- Fighter explosion: `Wave/FE_HIGH_RES/EXPLOSIONFIGHTER1.WAV`
- Impacts: `EXPLOSIONSPARKS1.WAV`, `EXPLOSIONSML1-8.WAV`
- X-wing cockpit engine loop: `Wave/Sfx_Interior/EngineIntXW.WAV`
- TIE engine: `Wave/Sfx_Interior/EngineIntTI.WAV`
- Warnings/UI: `Sfx/AlarmDanger.WAV`, `PANELARM.WAV`, `R2*.WAV`, flyby,
  canopy, hangar ambience in `Sfx_*` dirs. Per-craft mapping + volume /
  distance params in `Wave/Sfx_Weapon.lst`, `SFXBLASTNEW.LST`.
Current DuelVR: NO audio layer -> NEW in P1.8.

## Native audio architecture
Planned: `duel_audio.c/h`, event-driven (`AUDIO_PLAYER_FIRE`,
`AUDIO_TIE_FIRE`, `AUDIO_PLAYER_HIT`, `AUDIO_TIE_HIT`,
`AUDIO_PLAYER_EXPLODE`, `AUDIO_TIE_EXPLODE`, `AUDIO_ENGINE_LOOP`).
Sim emits events; audio plays them; simple 3D for world sounds, local
for cockpit. WAVs cooked from the user's install to private app storage.

## Memory/performance
Budget target: hold 72 fps. 2560-star VBO is trivial; CubeMap JPG faces
(~3 MB each) decode to ~12-25 MB GPU -- fine. Explosion DAT frames must
be curated (378 MB raw is NOT shippable; extract a small atlas).
Measure fps/draws/texture-mem per stage.

## APKs
(none yet for P1; staged: P1.1 SPACE, P1.2 BACKDROPS, P1.3 FX, P1.4 AUDIO,
final `XWADuelVR-P1-AUDIOVISUAL-debug.apk`.)

## Quest validation
Pending. Per-stage: confirm black space, XWA-like stars, backdrops,
lasers, explosions, audio mix -- all in HMD.

## Remaining differences from XWAUP
To be filled per stage (twinkle animation, DAT decode fidelity, planet
terminator, cockpit/world lighting match, audio spatialization).

## Status / next step
P1.1 VALIDATED by user in HMD ("aceptado, estrellas y cielo negro",
72 fps, DUEL_CTRL live). P1.2-AUDIO below installed, awaiting user run.

## P1.2 Native Audio (music + efectos)

### Sources (all user's own install, pushed to duel_assets/audio)
SFX (direct WAV): RebelLaser, EmpireLaser, EXPLOSIONSPARKS1 (player hit),
EXPLOSIONSML1 (TIE hit), EXPLOSIONFIGHTER1 (both kills), EngineIntXW/TI
loops, AlarmDanger (low HP), R2A (TIE kill). Music: STCONFREB.IMC decoded
on-device with the REAL VIMA decoder (imcodec.c, read-only) via
`tools/imc2wav_main.c` (ARM): stereo 22050/16, 97.0 s rebel-conflict
loop, 8.5 MB PCM, validated peak/RMS/non-silent. Candidates SQREBLG3
(14.8 s) / STEMPIRE (64.6 s) / SQREBBDLG1 (8.9 s) also decode OK.
Nothing proprietary in repo or APK. Push script: `tools/Push-Audio.ps1`
(run-as into app-private storage).

### Architecture
`duel_audio.h` + `duel_audio_core.c` (SDL-free: event polling by
muzzle/hp/alive transitions, 16-voice mixer, linear resample, constant-
power pan, quadratic attenuation) + `duel_audio.c` (SDL3 device/stream,
F32 stereo @44100, WAV load, degraded-safe). Sim UNTOUCHED (rule-pinned);
listener = real world head pose (new HeadTelemetry). Engine pitch/gain
follow throttle; TIE engine + lasers positional; music loop local.

### Tests
C 34 -> 37 ALL PASS on-device (atten/pan/clamp, event transitions incl.
muzzle-wrap silence + warning cooldown, mix exact/interp/loop/resample).
Rules 38 -> 43 (wiring, sim-clean, core SDL-free, DUEL_AUDIO, listener).

### APK
`XWADuelVR-P1.2-AUDIO-debug.apk`, 2324839 bytes,
SHA-256 `9B4642B9DEA1B9B46B9FAD417DF7E6DAF4EFDB2A6D65961ACBCE418A052CEECB`,
versionName `1.2-P1.2-audio` (zero native warnings). INSTALLED
(user-authorized) + audio pushed + logger restarted. Prior APKs preserved.

### Quest validation
Run 1 (1.2-P1.2-audio): TOTAL SILENCE. Log (`evidence/p1-audio/duel.log`)
decisive: `DUEL audio: no device (Audio subsystem is not initialized)`
while sounds=11/11 loaded. Root cause: `SDL_Init(SDL_INIT_VIDEO)` never
initialized audio. Fix (1-line): `SDL_INIT_VIDEO | SDL_INIT_AUDIO` +
regression rule. `XWADuelVR-P1.2-AUDIOFIX-debug.apk`, 2183919 bytes,
SHA-256 `11E636C144079EFB18A2213BC3ED60E31F7231C99C959D702F4AABEB55D9061D`,
v1.2-P1.2-audiofix, INSTALLED. Run 2: device=ok, music OK, lasers OK,
kills=0/0 (no explosions occurred, correct), php=80 (hits happened but
went unnoticed). ENGINE TERRIBLE ("seco y ruidoso", unplayable). Root
cause: mixer summed S16-scale floats into an F32 +-1.0 stream = constant
hard clipping on the loud continuous loop (music/lasers only clipped
their loud instants, less noticeable). Fix2: `DuelAudio_NormalizeChunk`
(master 0.8, clamp) + `DuelAudio_LoopBlend` 2048-frame loop crossfade at
load (engine WAV seam was 7.9% of peak) + tests + rule.
`XWADuelVR-P1.2-AUDIOFIX2-debug.apk`,
SHA-256 `BE14A48BAB1DDB299BDE76138371434FCBC2EAACEE947917CFD50668AA7FEF1E`,
v1.2-P1.2-audiofix2, INSTALLED. Run 3 (user's last audio test) pending.
Remote verification attempted (no ears): BLOCKED twice, both documented.
(1) `am start` boots the process but vrshell keeps focus
(FocusPlaceholderActivity); native code never runs without VR focus, so
no log lines. (2) Headless SDL probe (`tools/sdl_audio_probe.c`, real
libSDL3.so + RebelLaser.WAV): `SDL_Init(AUDIO)` fails without the Java
activity bootstrap even with SDL_MAIN_HANDLED. Proven without HMD: mixer
core 37/37, 11/11 WAVs load (run-1 log), init-flag root cause fixed.
Decisive proof requires the user's ear test (last assisted test).

### Remaining differences
No iMUSE dynamic transitions (single loop); no doppler; volumes are
first-pass (.lst-based) pending ear validation.

## P1.1 Black Space
Clear color `(0.008,0.008,0.02)` -> pure `(0,0,0)`. No gradient, nebula,
haze. Cockpit lighting untouched. Pinned by render rule.

## P1.2 Original XWA Starfield

### Source algorithm
`C:\OpenXWA\src\xwa\flight\starfield.c` FlightStarfield_Init: LCG
`9421*s+1` (math2.c), 128x128 angular-bin reservation (azimuth wrapped,
elevation folded), 512 direct (polar cap, gnomonic, render-mirrored by
hemisphere) + 2048 angular (equatorial band), Q15 vectors, 4 twinkle
cycles (39/64/45/57 steps, values 0..23 x 0x841 gray + jitter 0..7),
bright-star gray cross flare, density 1..4 menu filter.

### Native implementation
New `src/duel_sky.h/.c` (clean-room behavior port, DuelVR style).
Divergences (all documented in header): (1) cap split 256+256 instead
of per-view mirroring (no VR pop, both poles populated, total stays
2560); (2) doubles for transcendentals; (3) fixed seed 0x1234;
(4) guarded direct-branch div0 (latent 1999 crash, p=2^-32);
(5) acos arg clamped (latent NaN bins). `#pragma STDC FP_CONTRACT OFF`
for bit-exact cross-platform fields. Static frame-0 brightness
(`DUEL_XWA_STAR_TWINKLE 0`); procedural planets behind
`DUEL_PROCEDURAL_PLANETS 0` (code kept for P1.3).

### Coordinate conversion
XWA (x,y,z) poles +-Z -> DuelVR (x,z,-y) poles +-Y (rotation, det +1).
Sky MVP = proj * rigid_inv(eye_q, 0): rotation-only, zero parallax,
like the original camera-matrix rotation of directions.

### Brightness mapping
Original 16-bit word = 0x841*value + jitter (RGB565 R1/G2/B1 per step):
bright = (4*value + jitter)/99. Neutral white, no rainbow. Static
frame-0 sample preserves the brightness-class distribution; twinkle
(P1.2b) will advance cycle frames.

### Performance
ONE VBO (2560x9 floats = 90 KB), ONE point draw per eye, size 2.5.
72 fps target preserved by construction (fewer verts than a ship).

### Tests
C suite 27 -> 34 on-device ALL PASS (§19: count, determinism, unit,
valid+poles+coverage, translation invariance, head independence, LCG
sequence 58277/32546/38459/39232/47169). Independent python oracle
`evidence/p1-space/sky_oracle.py`: full-field fnv1a MATCH `AB5F8131`
(bit-exact 2560 stars; debugging caught an oracle-side float-negation
-0.0 bug and a decimal print/parse artifact, both fixed). Render rules
32 -> 38 (black clear, planets off, sky wired, rotation-only, 2560).

### APK
`XWADuelVR-P1.1-XWASTARS-debug.apk`, 2180183 bytes,
SHA-256 `B1A7390F249AB7345D911CDF2CEF64E0C530F1B752F653AC256B5C15A0A6F3B5`,
versionName `1.1-P1.1-xwastars` (aapt + fresh-.so verified). P0.5b
preserved. NOT INSTALLED (awaiting order). Baseline recorded: no git
commits (all untracked, no destructive ops); P0.5b APK + P0 checkpoint
are the frozen baseline.

### Quest validation
Pending: cockpit/controls identical to P0.5b; BLACK space; no planets;
denser XWA-like stars; head/ship rotation behaves as distant sky;
2-3 min flight, ~72 fps. Pull duel.log (DUEL_SKY line) + photos.

### Remaining differences
No twinkle animation (P1.2b); no bright-star flare yet; poles 256 (not
512) cap stars each; XWAUP DDS cubemap audited only (1024^2 RGBA x6,
25 MB) for later A/B; planets/backdrops/audio/FX untouched (P1.3+).

### Quest validation (8 Oct 2026, physical)
User: "aceptado, estrellas y cielo negro". P1.1 VALIDATED. Asked for
more components + music. P1.1 APK was the installed build at that time.

## P1.2 Native Audio (VALIDATED with engine fix pending ear-check)

### Implementation
`src/duel_audio.h/_core.c/_audio.c`, wired in `duel_main.c`. SDL-free
core (unit-tested) + SDL device layer. Event polling from sim state
(muzzle counters, hp edges, alive edges). 11 proprietary WAVs pushed to
app-private `duel_assets/audio` via `tools/Push-Audio.ps1` (sources:
`Wave/Sfx_Weapon`, `Wave/FE_HIGH_RES`, `Wave/Sfx_Interior`,
`Wave/Sfx_Sounds`, `Sfx/R2A.WAV`); music = STCONFREB.IMC decoded
ON-DEVICE with a real VIMA decoder (`tools/imc2wav_main.c`). Nothing
proprietary in repo/APK.

### Bugs found by ear
Run1: total silence, `no device (Audio subsystem not initialized)` --
missing SDL_INIT_AUDIO (fixed + rule-pinned). Run2: music/lasers OK;
engine loop "seco y ruidoso" -- S16-scale sums into the F32 bus =
clipping + a 7.9% loop seam; fixed with `DuelAudio_NormalizeChunk`
(master 0.8) + `DuelAudio_LoopBlend` crossfade. User confirmed "audio
sin motores ruidosos" on the fix build. Explosions/impacts not yet
heard (no combat event during that run).

### APK
`XWADuelVR-P1.2-AUDIOFIX2-debug.apk`, 2184651 bytes,
SHA-256 `BE14A48BAB1DDB299BDE76138371434FCBC2EAACEE947917CFD50668AA7FEF1E`.
INSTALLED on Quest (last user-run build).

## P1.3 Feedback: ducking + shake + sparks + reticle -40% (NOT INSTALLED)

### Implementation
User-ordered from the audio session: (1) throttle ducking --
engine gain 0.35+0.65*thr, music x (1-0.45*thr); (2) impact shake
(0.035 rad decaying, camera-only); (3) dashboard sparks at panel ship
pos + flash floor 0.35 (XWA-like hit feedback); (4) reticle x0.6.

### APK
`XWADuelVR-P1.3-FEEDBACK-debug.apk`, 2185467 bytes,
SHA-256 `B801E963AB2BD6329590D4E8B889DD30B98F5939D169575DD9D59A5F77D32C71`.
Built, NOT installed (Quest USB lost that night).

## P1.4 FX: lasers + muzzle flash + staged booms (NOT INSTALLED)

### Implementation
`duel_render.c` only, sim untouched. Laser glow+core 2-pass with XWA
hook colors (rebel FF3300, imperial 00FF33); muzzle flash via the
single-source `Duel_MuzzleWorldAt` getter (player + TIE); staged
explosions (white flash + orange fireball + dark embers, 384-particle
budget). Tests 39/39 + rules 52/52 last green on-device before the
Quest disconnected (WiFi 192.168.68.100:5555 stale, USB gone).

### APK
`XWADuelVR-P1.4-FX-debug.apk`, 2186279 bytes,
SHA-256 `5D701AC69ECEB5D58657F1C964DC416BC390A59DC7C5830E65B4438F2AAB5BCE`.
Built, NOT installed.

## P1.5 Authentic DAT sprites (BUILT, Quest validation pending)

### DAT catalog (JeremyAnsel DatFileEditor+XwaTexture reflection)
Effects1.dat: g1000 lasers (23 imgs), g1010 laser glow, g1050/1051
muzzle flashes, g2000 explosions (47 frames), g3000 sparks (20),
g4000/4010 impact flashes, g5000 engine glow (9), g6000-6010 planet
(XWAU phase). Effects3.dat: g9001-9011 backdrops (11), g9100
particles, g9200 asteroids. BC7/DXGI_99 decoded; Y-flip required
(GetBitmap is OpenGL-order). 4 samples extracted to TEMP and
VISUALLY confirmed: g2000 fr0 = classic XWA fireball (white-yellow
core, orange-red smoke), g9001 = radial starburst flare (a SUN/flare,
not a nebula), g3000 fr0 = white-yellow spark.

### INCIDENT 2026-10-08 ~01:20: `Resdata/Effects/` VANISHED
Between the sample extract (01:14, PNGs timestamped) and the batch
extract, `C:\games\XWAU-Mods\XWAU2025\Resdata\Effects\` (DatFileEditor
+XwaTexture DLLs, Effects1.dat, Effects3.dat) disappeared; recursive
search finds no copy anywhere. NOTHING else lost: game root DATs
intact (EXPLOSIONS 378 MB, BACKDROP, SPARKS, PLANET 1.7 GB), all 18
APKs + code intact, audio re-pushable from install paths, 4 sample
PNGs survive in TEMP. Recovery: re-download JeremyAnsel DatFileEditor
and re-extract from the ROOT DATs (group catalog above re-verifies
content); `tools/Export-Sprites.ps1` documents the mapping.

### Render path: pipe_sprite (NO new shader)
New `src/duel_sprite.h/.c` (SDL-free billboard quad + frame/slot
timing, unit-tested). `pipe_sprite` reuses lit_vs/lit_fs as
TRIANGLELIST depth-test/write-off ADD blend: black base texture kills
the lit term (ambient=dir=0), sprite rides as emissive x brightness.
ADD makes black texels vanish -- no alpha shader needed. Sprites:
2-frame boom anim at ship deaths (30->120 m, 1.2 s), spark pops at
muzzle fire + hits + cockpit panel, fixed world-direction sun
(8000 m, inside the 30000 m frustum). PNGs load from
`<asset_dir>/fx/` (boom_00/boom_01/spark_00/sun.png); missing files
draw nothing and never fail Ready (cook lands separately).
HMD appears only in the camera basis (same rule as lasers).

### Push-ready cook
`C:\Users\enaci\AppData\Local\Temp\duel_fx\`: boom_00.png 238771 B,
boom_01.png 214214 B, spark_00.png 7557 B, sun.png 209536 B.
`tools/Export-Sprites.ps1` pushes to `duel_assets/fx` (run tomorrow).

### Tests (laptop; device run pending)
NDK arm64 compile of the full C suite incl. 3 new sprite tests:
test_sprite_quad (corners/normals/UV incl. flip + rotated basis),
test_sprite_frame (boundaries/clamp/mix), test_sprite_slots
(ring alloc/full/reuse/expire) -- exit 0, zero warnings.
`test_render_rules.py`: 56/56 PASS (4 new sprite-architecture rules).
Native lib + gradle BUILD SUCCESSFUL, zero warnings.

### APK
`XWADuelVR-P1.5-SPRITES-debug.apk`, 2188715 bytes,
SHA-256 `F0500F3420B29C11DE436F897A3689CE5C2BC48642BA58AF2FD5C020E7F92009`,
versionName `1.5-P1.5-sprites`. All prior APKs preserved. NOT
INSTALLED (Quest disconnected; user verifies tomorrow).

## Tomorrow plan (ONE order)
1. Reconnect Quest (USB). 2. `adb install XWADuelVR-P1.5-SPRITES`.
3. Run `tools/Push-Audio.ps1` (idempotent) + `tools/Export-Sprites.ps1`.
4. Push + run `TEMP/test_duel_p15`, pull `duel.log` (DUEL_SPR lines).
5. HMD: ducking (thr engines up/music down), shake+sparks on hits,
reticle -40%, hook-color lasers, authentic boom/spark/sun sprites,
72 fps. 6. Re-download DatFileEditor, re-extract full frame sets from
root DATs (47-frame booms, 20 sparks, planet phases, nebula backdrops)
for P1.6/P1.3-planet pass.

## Next step
Install P1.5 + push audio/fx cooks on the Quest and validate P1.2-P1.5
in the HMD in one session.

## P1.5 Quest validation 2026-10-08 ~11:52 (photo evidence)

### Setup
Quest 3S 3487C10H9T0BKZ (USB), then WiFi adb 192.168.68.100:5555.
P1.5 installed (Success). Audio 11/11 + music 97.0 s decoded.
Sprites 4/4 in duel_assets/fx. Device tests: ALL DUEL TESTS PASS
(incl. sprite_quad/frame/slots + audio_ducking eng 0.14->0.39,
mus 0.40->0.22). Photos: `evidence/p15-quest/p15_115108.jpg`,
`p15_115217.jpg`.

### User verdict
Effects better ("mejores efectos"). TWO issues:
1. Music almost inaudible ("musica casi no se escucha").
2. An explosion "stuck in space like a sheet with a drawing".

### Finding 1: WHITE SHEET = sun sprite transparent texels are WHITE
Photos show the g9001 starburst on an opaque WHITE square. Root cause:
the lit fragment shader outputs `texture().rgb` IGNORING alpha, and
sun.png transparent texels are RGB (255,255,255,0) -- additive adds
white. Corner samples: sun (255,255,255,0) BROKEN, spark_00
(255,255,255,9) BROKEN (faint white boxes on sparks/muzzles),
boom_00/boom_01 (0,0,0,0) CORRECT (explosions unaffected). The boom
anim itself was not faulted. Fix (no shader change): premultiply
cook -- rgb *= a for all fx PNGs, re-push. Sun quad placement/size
(2400 m @ 8000 m) and world-locking verified correct in both photos;
reticle smaller + green gunsight visible; cockpit/stars/black all good.

### Finding 2: MUSIC GAIN TOO LOW
G_MUSIC 0.40 (ducked 0.22..0.40) vs G_ENGINE 0.55 (x2 engines sum).
User ear: music nearly gone under engines. Fix: G_MUSIC 0.40 -> 0.65,
DUCK_DEPTH 0.55 -> 0.65 + update audio_ducking test expectations.

## Next step
P1.5a fix build: premultiplied fx cook + music gain up, install,
re-validate white sheet gone + music audible.

## P1.5a fixes (BUILT + cooks pushed, install pending order)

### White sheet fix (cook-only, no code)
All 4 fx PNGs premultiplied (rgb *= a, PIL): corners now (0,0,0,0),
opaque centers untouched. `Export-Sprites.ps1` now re-bakes
premultiply idempotently before every push (durable for future
re-extracts). Premultiplied cook pushed to duel_assets/fx 12:00
(boom 189531/189563 B, spark 7083 B, sun 142231 B -- smaller sizes
confirm the black bake).

### Music fix (code)
`duel_audio_core.c`: G_MUSIC 0.40 -> 0.65, DUCK_DEPTH 0.55 -> 0.65.
Test `audio_ducking` updated (ratio 0.65 + audibility floor mus_lo >
0.6). Device run: ALL DUEL TESTS PASS, ducking eng 0.14->0.39,
mus 0.65->0.42. Rules 56/56. Native + gradle BUILD SUCCESSFUL,
zero warnings.

### APK
`XWADuelVR-P1.5a-FIXES-debug.apk`, 2188715 bytes,
SHA-256 `51C0CF8EC752A9B3AED7F4E1A62D297BE9E649121394299D335483FDF37C3176`,
versionName `1.5a-P1.5a-fixes`. NOT INSTALLED (awaiting order).

## Next step
Install P1.5a and re-validate: white sheet gone, music audible.
