# XWADuelVR M2 REALSPACE

## Frozen audiovisual baseline (Milestone 1 / P1 COMPLETE, user-validated)
APK `XWADuelVR-P1.5a-FIXES-debug.apk`, 2188715 bytes,
SHA-256 `51C0CF8EC752A9B3AED7F4E1A62D297BE9E649121394299D335483FDF37C3176`,
versionName `1.5a-P1.5a-fixes`. Installed on Quest 3S 3487C10H9T0BKZ at
freeze time (dumpsys confirmed). User verdict 2026-10-08: "me gusta"
(music audible, white sheet gone, effects better). Git: no commits, all
untracked, no destructive ops. Frozen: seat/HMD/camera, M8 controls,
body-local axes, flight model, TIE AI, weapons/collision/damage/reset,
OpenXR, audio arch + music, lasers/FX, XWA stars, black space.

## Goal
Make navigation feel convincing: DEPTH + PARALLAX + SCALE + MOTION
CUES. Felt more than seen ("el espacio se siente con más profundidad",
not "veo muchas partículas"). VR perceptual aid, NOT claimed as
original XWA (XWA has no near-field dust outside hyperspace).

## FAR layer
Untouched: 2560 XWA-algorithm stars, rotation-only sky VP, black
clear, ONE point draw/eye. Rule-pinned (`FAR XWA sky untouched`).

## MID layer
World cells 2500 m, active 5^3 = 125 cells, range 450-4000 m, cap 24,
expected ~17. Dim gray points (0.05-0.14), size 2.5. Fade in 450-700,
out 3500-4000 (smoothstep).

## NEAR layer
World cells 300 m, active 7^3 = 343 cells, range 25-650 m, cap 50,
expected ~33. Faint gray points (0.10-0.22), size 4. Fade in 25-60,
out 550-650. Points only in M2.1 (no streaks: warp-tunnel risk).

## World-cell architecture
`src/duel_depth.h/.c` (SDL-free). Content = pure function of
(layer, cx, cy, cz): 0-2 particles/cell (avg 0.875), position/size/
brightness/type from SplitMix-style unsigned hash streams. Collect
iterates the active cube around the SHIP pos, keeps the nearest `cap`
with fade > 0, sorted nearest-first. Face coverage exceeds MAX range
on both layers (900 > 650, 5000 > 4000): no boundary popping.
Particles never chase the ship; the ship flies through the field.

## Deterministic hashing
`duel_depth_hash(layer, cx, cy, cz, stream)`: unsigned-only
SplitMix32 mixing, cross-platform deterministic. Streams separate
count/position/size/brightness/type. Same world region always yields
the same field (fly away and back: identical).

## Parallax model
NEAR >>> MID >>> FAR(0). Measured: 50 m translation displaces a 100 m
reference 0.464 rad (~26.6 deg), a 2 km reference 0.0250 rad (~1.43
deg), far stars 0 (rotation-only sky). Near/mid use the normal world
VP per eye (true stereo disparity); far keeps the sky matrix.

## Stereo projection
Depth points baked once per frame (world coords, ship-collected) and
drawn per eye with each eye's world VP. Depth-tested (write off), so
cockpit/ships occlude them. No head/ship transform in world creation.

## Performance
468 cell visits + <=74 verts per frame CPU; 2 point draws/eye; zero
new GPU buffers (dyn buffer). 72 fps target preserved by construction.

## Tests
C suite on Quest: ALL DUEL TESTS PASS, incl. `depth_determinism` (A:
identical cells, in-cell bounds, hash layers differ; B+C+E: no
ship/head/orientation input by construction), `depth_parallax` (D:
0.464 vs 0.0250 vs 0), `depth_collect` (F: shared (cell,slot) match
across boundary; G: 245 cells finite + ranges; H: budget at 5 sampled
positions; fade shape both layers). Rules 60/60 (4 new M2 rules:
ship-keyed collect, world-VP draws, flags, FAR untouched).
`DUEL_REALSPACE` log every 6 s: near/mid/cells/draws/speed/mode.

## APK
`XWADuelVR-M2.1-SPACEDEPTH-debug.apk`, 2191379 bytes,
SHA-256 `15F40CA2E4AF1A70BACB773ACA518EC84E1DFC8AEB2496996326F7D4CFAAABA3`,
versionName `2.1-M2.1-spacedepth`. All prior APKs preserved.
INSTALLED on Quest (Success).

## Quest validation
Pending user HMD run: low speed ~empty; cruise occasional parallax;
head/roll/yaw world-stable; straight flight no tunnel; DEPTH FELT not
seen. Pull duel.log (DUEL_REALSPACE) + photos after.

## User verdict
VALIDATED 2026-10-08: "tus cambios fueron bien implementados". Depth
field accepted; proceeding to M2.2.

## M2.2 plan
After M2.1 validated: CELESTIALDEPTH -- authentic XWA/XWAUP landmarks
(BACKDROP/NEBULA/PLANET DATs, XWAUP CubeMaps, read-only): distant
planet impostor/sphere with terminator, far sun aligned with light
dir, one coherent nebula environment. No asteroid fields per duel.

## Next step
User HMD validation run of M2.1 (A-H protocol in the task brief).

## M2.2 Vehicle motion feel (INSTALLED, validation pending)

### Design
`src/duel_motion.h/.c` (SDL-free): critically-damped springs (w=7)
driven by measured ship accel + body rates, applied at SEAT level:
cameraWorld = ship * inertia * seat * hmd (`duel_eye_pose_motion` in
duel_math.h). Raw HMD inputs untouched. Limits: |pos| <= 13 cm,
|angles| <= 0.8 deg. Full accel ~8.4 cm back; full yaw ~2.7 cm out +
0.14 deg lag; full roll 0.44 deg lag. Sim untouched (history-derived
in render). Reset on combat entry (teleport safety).
`DUEL_VEHICLE_MOTION_FEEL 1` (0 = exact baseline). `DUEL_MOTION` log
every 3 s (pos/speed/fwd/accel/rates/offsets).

### Tests (device: ALL PASS)
`motion_springs` (accel direction, steady state, braking reverse, yaw
lean+lag, teleport clamps, decay), `motion_eye` (zero-offset == exact
baseline; shift == R(ship)*offset; HMD passthrough), `trajectory`
(A straight, B yaw curves, C pitch gains Y, D roll keeps line).
Rules 64/64 (4 new motion rules).

### APK
`XWADuelVR-M2.2-MOTIONFEEL-debug.apk`, 2193467 bytes,
SHA-256 `839BBB2CC06D547C2631A039A7A5F3F94BE7CF3948046FF6606FEF9354D90BD2`,
versionName `2.2-M2.2-motionfeel`. INSTALLED (Success). Prior APKs kept.

## Next step
User HMD run: accel/decel press, yaw/pitch/roll response; want "la
nave realmente avanza", never "la cámara se mueve sola".

## M2.2 Celestial Depth (INSTALLED, validation pending)

### Selected HDR / spheremap
`earthlike_planet.png` 8192x4096 (spacespheremaps.com, free
personal+commercial; kept in TEMP + device only). Crescent planet
(~31 deg) + ONE sun + faint nebula on black; sparse dim stars (no
fight with XWA stars).

### Why it was chosen
Safest/cleanest: black base (additive-friendly), single light source
(flare anchor), terminator in the art, no noise/artifacts. Viewed
before selection (preview.png).

### Integration path
Equirect on existing sphere geometry (UVs reused), pipe_sprite
additive (black = invisible), rotation-only sky VP, r=20000 (far
30000). No new shaders/geometry. Cook: 2048x1024 RGBA8 (8 MB GPU,
1.1 MB push) via `tools/Export-Env.ps1`. Source seam (68/255, dark
nebula, 50 rows) removed with a 32-col wrap crossfade (seam diff
48 -> 5). Old g9001 sun retired while env present (no double suns).

### Planet far-scene behavior
Zero translation parallax (sky VP), depth-tested vs ships, XWA stars
drawn additively on top (preserved).

### Lens flare design
Sun dir from measured brightest pixel (u=0.9175, v=0.2246 ->
(0.563, 0.761, -0.321)) via tested `duel_equirect_dir`. Billboard at
dir x 15000, 1500 m, gain = smooth view-alignment (1 on-axis, 0 past
~60 deg) x max 0.35. World-anchored, never face-glued.

### Alpha/blending safety
Env opaque (black adds 0); flare procedural black-bordered
(hard circular mask; unit test pins border == 0, center == 255).
No PNG alpha paths. `DUEL_CELESTIAL` A/B (0 = MODE A baseline).

### Performance impact
+2 draws/eye, +8 MB texture, 468-cell CPU walk unchanged. 72 fps safe.

### Tests (device: ALL PASS)
`celestial` (equirect poles/seam/unit/sun, gain 1/0/0.5, flare
border/center/falloff). Rules 69/69 (5 new celestial rules).

### APK
`XWADuelVR-M2.2-CELESTIALDEPTH-debug.apk`, 2195363 bytes,
SHA-256 `55E1EC0F2173FD96DCF99E64118B9A7DDA63B8D167762244D36393759F557172`,
versionName `2.3-M2.2-celestial`. INSTALLED (Success). Includes the
M2.2 motion-feel code (validate both this run).

### Quest validation
Pending: boot/cockpit unchanged, stars good, richer env, planet
distant, no seams, flare coherent + subtle, combat clear, 72 fps.

### Remaining issues
Motion-feel verdict still pending (same run). Planet softness at
2048 (6 px/deg) acceptable; 4096 upgrade possible if asked.

## Next step
User HMD run validating motion feel + celestial in one session.

## M2.2 Quest validation 2026-10-08 ~14:03 (photo evidence)
Photos `evidence/m22-quest/m22_140307/10/14.jpg`. User: "excelente
implementacion de todo". Motion feel accepted ("la nave se mueve muy
bien"). Env "increible": planet crescent huge + distant, sun + subtle
halo flare render correctly. Issues: (1) XWA star points visible ON
TOP of the planet disc (photo 140307); (2) a sharp BLACK RECTANGLE in
photos 140310/140314 at similar screen pos across rotated views.
Rect audit: no draw path produces an opaque black mid-screen quad
(HUD bars are bottom strips, flash is orange, sprites/flare/env are
additive); likely a Quest system overlay/compositor capture artifact,
not our render. OPEN: ask user whether it was visible in-HMD or only
in captures before any code action (root-cause discipline).

## M2.2a tune (INSTALLED, validation pending)
Motion: `DUEL_MO_GAIN 1.10` (+10% all inertia targets, clamps unchanged).
Stars: deterministic 1/2 decimation (even indices; generator untouched,
oracle green) + planet-disc cull (measured centroid u 0.3884 v 0.4954,
24 deg radius, cos 0.9135; env-gated; build_stars moved after env load).
Expected drawn: ~1225 of 2560. Tests ALL PASS (motion steady-state x
gain), rules 73/73 (4 new tune rules).
APK `XWADuelVR-M2.2a-TUNE-debug.apk`, 2195539 bytes,
SHA-256 `2680BBEAA7DCDF6FF56EA03114826F3BD32598338916269FADAD78071BAAC2EE`,
versionName `2.3a-M2.2a-tune`. INSTALLED (Success).

## Next step
User HMD run: motion +10% feel, planet clean of stars, star density,
black-rectangle discriminator (in-HMD vs captures-only).

## M2.2a validation 2026-10-08 ~14:21 (photo evidence)
Photos `evidence/m22-quest/m22a_142140/142150.jpg`. Stars STILL over
the planet disc (M2.2a 24-deg cull covered only the lit crescent; the
dark side shows stars). User orders 5 tweaks (all in M2.2b).

## M2.2b tune2 (INSTALLED, validation pending)
1. Stars -30% more: DIV 2 -> DIV 3 (854 of 2560 drawn, -33%).
2. Near pass-by +30%: NEAR_CAP 50 -> 65.
3. Reticle -10% diameter: rs 2.835, gap 0.972, circle 4.455, diamond
   0.486 (x0.54 total vs P0).
4. POV -15 cm: seat y 0.85 -> 0.70 (eye now glareshield-level;
   forward sightline clear: panel front is behind-above). Seat bounds
   test + render audit updated to the user-ordered value (> 0.63).
5. Full-disc cull: lit-crescent fits were contaminated (Kasa median
   resid 4.9 deg, bbox nebula). Wide-run fit (115 clean rows):
   disc center u 0.4049 v 0.4922, r 10.2 deg. Cull 16 deg
   (cos 0.9613, ~16 stars) incl. the unlit side.
Tests ALL PASS, rules 75/75 (decimation/POV/cap rules updated).
APK `XWADuelVR-M2.2b-TUNE2-debug.apk`, 2195607 bytes,
SHA-256 `693E964FC95A3493184D8919C94A02424071FBA824B3C907E3BFD395A03FBD20`,
versionName `2.3b-M2.2b-tune2`. INSTALLED (Success).

## Next step
User HMD run: planet fully clean (lit + dark side), density, pass-by,
reticle size, lower POV comfort. Black rectangle still OPEN.

## Black rectangle: RESOLVED as capture-only artifact
Zoom `evidence/m22-quest/rect_zoom.jpg`: sharp black parallelogram
occluding stars, similar screen pos in 2 captures across rotated views.
User does not recognize it ("no se a cual te refieres") = NOT visible
in-HMD. No draw path can produce it (audited: HUD bottom strips, orange
flash, all sky/FX additive). Verdict: Quest screenshot-compositor
artifact, benign, no code action. Monitor only.

## M2.2c tune3 (INSTALLED, validation pending)
1. POV +20 cm forward: seat z -0.78 -> -0.98 (eye model z 0.98, 2 cm
   inside the windshield bound -- tight but passing; flagged).
2. Static stars OFF: `DUEL_STATIC_STARS 0` gates the draw only
   (generator/VBO/decimation/cull untouched, trivially reversible).
   Experiment: planet region should show only env-baked + depth points.
3. Moving references +20%: NEAR_CAP 78, MID_CAP 29.
Tests ALL PASS (seat bounds incl. new eye), rules 76/76.
APK `XWADuelVR-M2.2c-TUNE3-debug.apk`, 2195547 bytes,
SHA-256 `1BE0BD1B05E0A943271E3997CC09BB00006000907E1368EB7C2A99D84F35425B`,
versionName `2.3c-M2.2c-tune3`. INSTALLED (Success).

## Next step
User HMD run: depth-only sky feel, planet clean?, pass-by density,
forward POV comfort.

## M2.2d combat (INSTALLED, validation pending)
1. X-Wing muzzles fixed from measured geometry (user: bolts didn't
   come from the cannon tips). Wingtip clusters in XwingExterior glTF
   (all 4 quads agree): OPT (+-5.35, +-1.30, +1.82 muzzle face) ->
   ship (+-5.35, +-1.30), 1.82 fwd. Old guess (+-5.0, +-0.8, 4.0)
   spawned bolts 2.2 m ahead of the muzzles. Cooker has no hardpoint
   support (verified), so glTF measurement is the authority; XWAUP
   proportions assumed same (HMD decides).
2. Enemy guide chevron: amber arrowhead around the reticle (7.6 m tip
   radius at the 120 m gunsight plane) pointing ship-relative toward
   the TIE (`Duel_GuideDir`: hidden inside 12-deg nose cone, (0,-1)
   when astern). Steering cue (turn toward it), HMD-independent.
Tests ALL PASS (`muzzle_world` new values, `guide_arrow` cone/
cardinals/astern/yawed-ship), rules 78/78 (2 new).
APK `XWADuelVR-M2.2d-COMBAT-debug.apk`, 2196015 bytes,
SHA-256 `583B3FB08F0AFE09869F8968177D57CFC80D8626438593EA536A49CC531582B9`,
versionName `2.4-M2.2d-combat`. INSTALLED (Success).

## Next step
User HMD run: bolts from wingtips, guide chevron behavior.
