# XWADuelVR FUTURE (scope-creep parking lot — P0 stays a duel)

## Deferred by design
- Mission editor / campaign / briefing / hangar.
- Full XWA HUD port (target computer text, shield/hull pages, messages).
- Ship database beyond X-Wing + TIE (20 ships, capitals).
- Advanced physics (inertia tensor, collisions mesh-level, capital ship docking).
- Virtual hands / hand tracking / physical stick grab / gaze steering.
- Multiplayer / netcode.
- Save files / profiles / pilot persistence.
- Options menus (video/graphics/key bindings) — constants live in
  `src/flight_config.h` for now.

## Visual upgrades (post-P0)
- XWA backdrop asset for planet/sky instead of procedural bands (audit
  `Resdata`/Frontres planet bitmaps for a loadable candidate).
- Player X-Wing exterior render (death cam / external debug view).
- S-foil animation (bake per-mesh nodes, animate at runtime).
- Cockpit interactive instruments (reuse cockpit emissive textures as state lamps).
- Engine glow billboards (currently emissive textures only).
- Debris meshes (currently particle points).
- Text rendering for debug overlay (currently logcat 1 Hz).
- MSAA + foveated rendering perf pass; draw-call/instance batching for prims.

## Audio (P0 ships silent)
- Laser / impact / explosion / engine ambience from local install (license-safe
  local prototype use only, never redistribute).
