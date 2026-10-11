# CHECKPOINT XWADuelVR M4c — PILOT TEXTURE + FLAT FEET

Date: 2026-10-09 ~23:50. Device: Quest 3S `3487C10H9T0BKZ`.
APK: `XWADuelVR-M4c-PILOTFIX-debug.apk` (2242419 B,
SHA256 `8BDF41AAC981226DF5A580F39126C28CA9AEAE9888F86E9A0DE8EB042D867CAA`),
`versionName=3.14-M4c-pilotfix`, installed `install -r`.
Device files refreshed: `Pilot.gltf` (29031 B) + `Pilot.jpg`
(1235594 B, TRUE diffuse now); `Pilot.bin` unchanged (8424848 B).
Prior builds/APKs preserved; no reset/clean.

## Visor report on M4b (user-tested)
- Torso + legs VISIBLE (M4b frame fix works). No arms, platform
  glove-hands intact (as designed for stage 1).
- Body all WHITE (no texture) + feet inverted (heels up, toes down).

## Root causes + fixes
1. WHITE: `convert_pilot.py` assumed texture 0 = diffuse, but material
   0's baseColor is texture 1 -> image 1. It extracted the NORMAL map
   (1996185 B) to Pilot.jpg while the real diffuse stayed embedded
   (loader reads base-color by URI only) -> white_tex fallback.
   FIX: converter now follows material 0 baseColorTexture ->
   texture -> image, asserts the name contains "Diffuse", extracts
   1235594 B. Verified: baseColor resolves to Pilot.jpg (diffuse).
2. FEET: M4b set an invented toe pitch (0.02,-0.35,0.94). The boot is
   rigid: heel sits ~97 deg from the toe joint axis, so that pitch
   swung the heel straight UP (+9 cm over the ankle, FK-verified).
   FIX: feet keep EXACT bind world orientation (boot modeled flat):
   new L/R Foot quats in kSeated. Heel now -7 cm below ankle, toe
   box +17 cm forward. Ankle flex absorbs the steep shin (boots
   hide the joint; no candy-wrap: D_foot = identity, D_shin ~60 deg
   about parallel axes, standard joint blend).

## Verification (host + device, all green)
- `tests/test_render_rules.py`: 153/153 PASS (+2 M4c rules: converter
  extracts true base-color; boot regression covered).
- Device CPU suite `/data/local/tmp/test_duel_m4c` (with fresh
  /data/local/tmp/Pilot.*): `ALL DUEL TESTS PASS` incl. new
  `pilot_boot` (skins real boots mesh: heel below-behind ankle,
  toe box forward, no NaN) + pinned foot quats.
- NEGATIVE CONTROL: same suite built with M4b foot quats FAILS
  `heel_y < ay - 0.03` (+ 4 quat pins) -> the test guards this bug.
- Bake probe: joints on targets, boots bounds sane, no NaN.

## Visor test (PENDING user validation -- not claimed)
1. Launch XWADuelVR. Look down: orange flight suit WITH texture
   (not white), torso + thighs + knees + shins + boots.
2. Boots: soles DOWN toward the footwell floor, toes pointing
   forward-down into the nose well (relaxed dangle), heels BEHIND
   (toward the seat), not up.
3. Start combat: body rides with the ship; hands/levers unchanged.
4. Report: texture visible? boots flat? any clip/twist? If clean,
   stage 1 is DONE -> stage 2 arms (follow hands, no dup hands).
