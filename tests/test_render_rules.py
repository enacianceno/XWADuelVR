#!/usr/bin/env python3
"""XWADuelVR render-rule regression tests (laptop-runnable).

Guards SDL_GPU API-ordering invariants that caused the P0 black screen:
a copy pass nested inside a render pass silently kills all draws.

Run:  python tests/test_render_rules.py   (exit 0 = pass)
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src" / "duel_render.c"
XR_SRC = Path(__file__).resolve().parent.parent / "src" / "duel_xr.c"

FAILURES = []


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f" ({detail})" if detail and not cond else ""))
    if not cond:
        FAILURES.append(name)


def main():
    text = SRC.read_text(encoding="utf-8")
    lines = text.split("\n")

    # 1. No copy pass may open between a render pass begin/end pair.
    depth = 0
    nested = []
    for i, ln in enumerate(lines):
        s = ln.strip()
        if "SDL_BeginGPURenderPass(" in s:
            depth += 1
        if "SDL_BeginGPUCopyPass(" in s and depth > 0:
            nested.append(i + 1)
        if "SDL_EndGPURenderPass(" in s:
            depth = max(0, depth - 1)
    check("no copy pass nested inside render pass", not nested, f"lines {nested}")

    # 2. Every render pass begin in DuelRender_Eye is closed in the same function.
    begins = sum(1 for ln in lines if "SDL_BeginGPURenderPass(" in ln)
    ends = sum(1 for ln in lines if "SDL_EndGPURenderPass(" in ln)
    check("render pass begin/end balanced", begins == ends, f"{begins} begins vs {ends} ends")

    # 3. Dynamic upload (UploadToGPUBuffer with cycle) happens before the eye's
    #    render pass begins: find order inside DuelRender_Eye.
    eye_start = next(i for i, ln in enumerate(lines) if "void DuelRender_Eye(" in ln)
    eye_body = lines[eye_start:]
    up_idx = next((i for i, ln in enumerate(eye_body) if "SDL_UploadToGPUBuffer(cp," in ln), None)
    begin_idx = next((i for i, ln in enumerate(eye_body) if "SDL_BeginGPURenderPass(" in ln), None)
    check("dyn upload precedes render pass begin",
          up_idx is not None and begin_idx is not None and up_idx < begin_idx,
          f"upload@{up_idx} begin@{begin_idx}")

    # 4. Uniform-slot contract: shaders declare <= slots the code pushes.
    #    Code pushes vertex slot 0 and fragment slot 0 only.
    push_slots = sorted(set(int(m.group(1)) for m in
                              re.finditer(r"SDL_PushGPU(?:Vertex|Fragment)UniformData\(\w+, (\d+)", text)))
    check("uniform push slots are {0}", push_slots == [0], f"slots {push_slots}")

    # 5. Every pipeline bind in the eye path is followed by draws before the
    #    pass ends (no dead binds): crude but catches stripped blocks.
    binds = sum(1 for ln in lines if "SDL_BindGPUGraphicsPipeline(rp," in ln)
    draws = sum(1 for ln in lines if re.search(r"SDL_DrawGPU(Indexed)?Primitives\(rp,", ln))
    check("draws exist for bound pipelines", binds > 0 and draws >= binds,
          f"{binds} binds vs {draws} draws")

    # 6. XrView structs must be type-initialized before xrLocateViews
    #    (P0.1 black-screen root cause: stack garbage fails validation).
    xr_text = XR_SRC.read_text(encoding="utf-8")
    has_init = "out_views[i].type = XR_TYPE_VIEW" in xr_text
    locate_after = (xr_text.find("out_views[i].type = XR_TYPE_VIEW") <
                    xr_text.find("s_xrLocateViews(s_session"))
    check("XrView.type set before xrLocateViews", has_init and locate_after)

    # 7. No player exterior in cockpit mode (P0.2 cockpit-space audit
    #    proof). M5: XwingExterior IS loaded, but ONLY as the ally mesh
    #    (drawn for allies[], never at the player pose).
    loads = sorted(set(m.group(1) for m in re.finditer(r'load_model\(r, &\S+, "(\w+)"\)', text)))
    check("model loads are tie+cockpit+xwing-ally (M28: +tiein, M30: +isd)",
          loads == ["StarDestroyer", "TieFighter", "TieInterceptor",
                    "XwingCockpit", "XwingExterior"],
          f"loads {loads}")
    check("xwing exterior drawn for allies only, never the player",
          "duel_craft_model(r, s->allies[k].craft)" in text
          and "amod, vp.m, am.m" in text
          and "&r->xwing, vp.m, cm.m" not in text)

    # 8. The P0.2 head-glue root cause must stay dead: no matrix->quat
    #    trace extraction with an identity fallback in the eye path.
    has_trace_hack = ("m[0] + m[5] + m[10]" in text) or ("tr > 0" in text and "q = duel_q_identity" in text)
    check("no matrix-quat identity fallback in eye path", not has_trace_hack)

    # 9. Eye pose must come from the exact closed form (ship x hmd),
    #    never from matrix decomposition.
    check("DuelRender_Eye uses duel_eye_pose", "duel_eye_pose(s->player.orient" in text)

    # 10. Frustum must output SDL Y-up NDC (P0.2b pitch-mirror root cause):
    #     m[5] positive (no Y-flip; SDL converts for Vulkan itself) and
    #     m[8] positive-sum glFrustum form (asymmetric FOV correctness).
    math_text = (SRC.parent / "duel_math.h").read_text(encoding="utf-8")
    check("frustum m[5] Y-up (no flip)",
          "m.m[5] = 2.0f / (tanU - tanD);" in math_text)
    check("frustum m[8] glFrustum offset sign",
          "m.m[8] = (tanR + tanL) / (tanR - tanL);" in math_text)

    # 11. Loader must record mesh_idx per prim and log a per-mesh prim
    #     histogram with range validation (P0.2c mesh2-absence audit:
    #     a mesh with 0 baked prims can never render).
    check("loader logs meshprims histogram", "meshprims=%s badidx=%d" in text)
    check("loader validates mesh_idx range",
          "mi < 0 || mi >= m->mesh_count" in text)

    # 12. Reticle must be ship-locked (P0.2d user report: eye-locked aim was
    #     unflyable). Center + frame come from the ship pose, never the eye.
    check("reticle center uses ship forward",
          "duel_q_forward(s->player.orient)" in text)
    check("reticle frame uses ship right/up",
          "duel_q_right(s->player.orient)" in text and "duel_q_up(s->player.orient)" in text)
    check("reticle not eye-locked (no cam_fwd x 8m)",
          "duel_v3_scale(cam_fwd, 8.0f)" not in text)
    check("reticle collimated at 120m (P0.2f 6deg parallax fix)",
          "duel_v3_scale(sf, 120.0f)" in text)
    check("lead pip uses duel_lead_point",
          "duel_lead_point(&s->player, tp, DUEL_PLAYER_LASER_SPEED)" in text)

    # 13. DUEL_SEAT_POS literal must match test_seat() in test_duel.c
    #     (P0.2d: stale seat caused the buried-in-fuselage views).
    test_text = (SRC.parent.parent / "tests" / "test_duel.c").read_text(encoding="utf-8")
    m_render = re.search(r"DUEL_SEAT_POS = \{ ([0-9.\-f]+), ([0-9.\-f]+), ([0-9.\-f]+) \}", text)
    m_test = re.search(r"test_seat\(void\) \{ return duel_v3\(([0-9.\-f]+), ([0-9.\-f]+), ([0-9.\-f]+)\); \}",
                        test_text)
    seat_sync = (m_render is not None and m_test is not None
                 and m_render.groups() == m_test.groups())
    check("seat literal matches test_seat()", seat_sync,
          f"render={m_render.groups() if m_render else None} "
          f"test={m_test.groups() if m_test else None}")

    # 14. M36 handed stick mapping (user order: the old map was
    #     reversed vs the hand-levers; input is XR-only, so the source is
    #     pinned instead of headless-tested). DIESTRA (default): the fly
    #     axes (f) come from the right stick, roll/throttle (g) from the
    #     left; ZURDA mirrors. yaw=fx, pitch=-fy, roll=gx, throttle=gy.
    input_text = (SRC.parent / "duel_input.c").read_text(encoding="utf-8")

    def _axis(field, var):
        m = re.search(r"out->" + field + r" = fabsf\((\w+)\) > DUEL_DEADZONE \? ([+-]?\w+) : 0\.0f;",
                       input_text)
        return m.groups() if m else (None, None)

    check("yaw on fly X (M36 handed)", _axis("yaw", "fx") == ("fx", "fx"),
          f"yaw={_axis('yaw', 'fx')}")
    check("roll on guide X (M36 handed)", _axis("roll", "gx") == ("gx", "gx"),
          f"roll={_axis('roll', 'gx')}")
    check("pitch kept flight-sim (-fy)", _axis("pitch", "fy") == ("fy", "-fy"),
          f"pitch={_axis('pitch', 'fy')}")
    check("throttle on guide Y (M36 handed)", _axis("thr_axis", "gy") == ("gy", "gy"),
          f"thr={_axis('thr_axis', 'gy')}")
    check("handed source select (DIESTRA right flies)",
          "float fx = s_hand_left ? lx : rx" in input_text
          and "float gx = s_hand_left ? rx : lx" in input_text)

    # 15. P0.4: sim/head separation + seat calibration wiring.
    math_text = (SRC.parent / "duel_math.h").read_text(encoding="utf-8")
    sim_text = (SRC.parent / "duel_sim.c").read_text(encoding="utf-8")
    xr_text = (SRC.parent / "duel_xr.c").read_text(encoding="utf-8")
    main_text = (SRC.parent / "duel_main.c").read_text(encoding="utf-8")
    head_refs = [w for w in ("hmd", "HMD", "camera", "Camera", "eye_pose",
                             "neutral", "calibrat")
                 if w in sim_text]
    check("sim never references HMD/camera/calibration", not head_refs,
          f"found={head_refs}")
    check("eye pose neutralized before use",
          "duel_cal_neutralize(&r->cal, hp, hq, &hp, &hq)" in text)
    check("calibration driven per frame with both views",
          "DuelRender_Calibrate" in text
          and "DuelXr_ConsumeSpaceChange" in text
          and "DuelRender_Calibrate(render, &sim, &views[0].pose, &views[1].pose, have_views)" in main_text)
    check("reference-space recenter handled in XR events",
          "XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING" in xr_text
          and "DuelXr_ConsumeSpaceChange" in xr_text)
    check("seat calibration audit logged",
          "DUEL_SEAT_CAL" in text and "DUEL_CAL gen=" in text)

    # 16. P0.5: body-local rotation, cal generation, control telemetry.
    check("player rotation post-multiplies (body-local)",
          "duel_q_mul(p->orient, dq)" in sim_text)
    check("calibration generation/reason tracked",
          "generation++" in math_text and "DUEL_CAL_RESET" in text
          and "DUEL_CAL_RECENTER" in text)
    check("DUEL_CTRL telemetry gated 5Hz + stick-active",
          "DUEL_CTRL gen=" in main_text and "stick_mag > 0.25f" in main_text
          and "DuelRender_CalTelemetry" in main_text)
    stick_refs = [w for w in ("DuelInput", "in->yaw", "in->pitch", "in->roll",
                              "in->thr_axis", "raw_lx", "thr_axis")
                  if w in text]
    check("render never reads sticks (camera follows ship only)", not stick_refs,
          f"found={stick_refs}")

    # 17. P1.1/P1.2: black space + original-XWA starfield.
    sky_text = (SRC.parent / "duel_sky.h").read_text(encoding="utf-8")
    check("space clear is pure black",
          "color.clear_color.r = 0.0f; color.clear_color.g = 0.0f; "
          "color.clear_color.b = 0.0f;" in text)
    check("procedural planets disabled",
          "#define DUEL_PROCEDURAL_PLANETS 0" in text)
    check("XWA sky generator wired",
          '#include "duel_sky.h"' in text and "duel_sky_generate(sky)" in text)
    check("star sky rotation-only (no parallax)",
          "duel_m4_rigid_inv(eq, duel_v3(0.0f, 0.0f, 0.0f))" in text)
    check("XWA star count 2560, twinkle off",
          "#define DUEL_SKY_STAR_COUNT 2560" in sky_text
          and "#define DUEL_XWA_STAR_TWINKLE 0" in sky_text)
    check("DUEL_SKY telemetry logged once",
          '"DUEL_SKY env=%d mode=XWA_ORIGINAL' in text)

    # 18. P1.2: native audio (sim untouched, events by polling).
    audio_text = (SRC.parent / "duel_audio.c").read_text(encoding="utf-8")
    core_text = (SRC.parent / "duel_audio_core.c").read_text(encoding="utf-8")
    check("audio engine wired in main",
          "DuelAudio_Create" in main_text and "DuelAudio_Update" in main_text
          and "DuelAudio_Destroy" in main_text)
    check("sim has no audio references",
          "audio" not in sim_text.lower() and "Audio" not in sim_text)
    check("audio core is SDL-free",
          "SDL_" not in core_text and "#include" not in core_text.replace(
              '#include "duel_audio.h"', "").replace(
              "#include <string.h>", ""))
    check("DUEL_AUDIO telemetry logged",
          "DUEL_AUDIO device=" in audio_text)
    check("listener uses real head pose",
          "DuelRender_HeadTelemetry" in main_text)
    check("SDL audio subsystem initialized",
          "SDL_INIT_AUDIO" in main_text)
    check("mixer normalized to F32 bus",
          "DuelAudio_NormalizeChunk" in audio_text
          and "DuelAudio_LoopBlend" in audio_text)

    # 19. Cockpit feedback + reticle size + throttle ducking.
    check("throttle ducking (engine up, music down)",
          "DUCK_DEPTH" in core_text and "s->player.throttle" in core_text)
    check("impact shake + dashboard sparks",
          "r->shake = 1.0f" in text and "0.22f * (float)k" in text
          and "r->shake * 0.035f" in text)
    check("reticle reduced 40% then 10%",
          "float rs = 2.835f, g = 0.972f;" in text
          and "cosf(a0) * 4.455f" in text
          and "duel_v3_scale(cu, 0.486f)" in text)

    # 20. P1.5/P1.6/P1.7: laser core+glow, muzzle flash, staged booms.
    check("laser glow quad + white core",
          "0.55f : 0.16f" in text and "gr * 0.55f" in text)
    check("XWA hook laser colors",
          "gg = 0.20f; gb = 0.0f;" in text and "gg = 1.0f; gb = 0.20f;" in text)
    check("muzzle flash uses sim getter",
          "Duel_MuzzleWorldAt(&s->player" in text
          and "Duel_MuzzleWorldAt(&s->ties[k]" in text
          and "Duel_MuzzleWorldAt(&s->allies[k]" in text)
    check("explosions staged (flash/fireball/embers)",
          "0.25f, 1.0f, 1.0f, 0.95f" in text
          and "2.6f, 0.7f, 0.2f, 0.05f" in text)

    # 21. P1.5: authentic sprite pipe (lit shaders, additive, no new SPV).
    check("sprite pipe additive lit reuse",
          "r->pipe_sprite = make_pipeline(r, r->lit_vs, r->lit_fs" in text
          and "1, 0, 1, 0, 0, swap_fmt, 1);" in text)
    check("sprite unlit via black base + emissive",
          "sfsu.ambient = 0.0f;" in text
          and "binds[0].texture = r->black_tex;" in text
          and "sfsu.emissive_strength = spr_draw[i].bright;" in text)
    check("sprite textures optional (cook lands separately)",
          "sprite textures are NOT required" in text
          and "if (!px) { SDL_Log(\"%s missing %s\", tag, path); return NULL; }" in text)
    check("sprite FX events from sim/render state",
          "duel_sprite_spawn(r->sprites" in text
          and "DUEL_SPR_SPARK, 1," in text
          and "DUEL_SPR_BOOM0, 2," in text)

    # 22. M2.1: world-anchored depth field (never HMD-keyed).
    check("depth collected from ship pos, not HMD",
          "duel_depth_collect(&s->player.pos, DUEL_DEPTH_NEAR" in text
          and "duel_depth_collect(&s->player.pos, DUEL_DEPTH_MID" in text)
    check("depth drawn with world VP, not sky",
          "UnlitVSU nsu = uvsu;" in text
          and "UnlitVSU msu = uvsu;" in text)
    check("SPACE_DEPTH flag on, debug off",
          "#define DUEL_SPACE_DEPTH 1" in text
          and "#define DUEL_REALSPACE_DEBUG 0" in text)
    check("FAR XWA sky untouched",
          "sky_view = duel_m4_rigid_inv(eq, duel_v3(0.0f, 0.0f, 0.0f));" in text)

    # 23. M2.2: vehicle inertia at seat level (HMD inputs untouched).
    check("motion feel flag on",
          "#define DUEL_VEHICLE_MOTION_FEEL 1" in text)
    check("motion fed from ship state only",
          "duel_motion_update(&r->motion, s->player.orient, s->player.speed, dt);" in text)
    check("motion eye keeps HMD passthrough",
          "duel_eye_pose_motion(s->player.orient, s->player.pos, DUEL_SEAT_POS," in text
          and "r->motion.off_p, mo_q, hq, hp, &eq, &ep);" in text)
    check("motion reset on combat entry",
          "duel_motion_reset(&r->motion);" in text)

    # 24. M2.2 celestial: env sphere additive + flare, old sun retired.
    check("celestial flag on",
          "#define DUEL_CELESTIAL 1" in text)
    check("env sphere rotation-only sky, additive pipe",
          "DuelMat4 esky = duel_m4_rigid_inv(eq, duel_v3(0.0f, 0.0f, 0.0f));" in text
          and "ebinds[1].texture = r->env_tex[r->env_active];" in text)
    check("old sun retired while env present",
          "r->spr_tex[DUEL_SPR_SUN] && !r->env_tex[r->env_active]" in text)
    check("flare gain-gated, world-anchored",
          "duel_flare_gain(cam_fwd, fsun)" in text
          and "spr_draw[spr_count].tex = DUEL_SPR_FLARE;" in text
          and "DUEL_FLARE_MAX * fg" in text)
    check("flare texture procedural, never PNG",
          "duel_flare_tex(flare);" in text)

    # 25. M2.2a tune: motion +10%, stars halved, planet-disc cull.
    motion_text = (SRC.parent / "duel_motion.h").read_text(encoding="utf-8")
    depth_text = (SRC.parent / "duel_depth.h").read_text(encoding="utf-8")
    check("motion gain 1.10",
          "#define DUEL_MO_GAIN 1.10f" in motion_text)
    check("star decimation 1/3",
          "#define DUEL_STAR_DRAW_DIV 3" in text
          and "if (i % DUEL_STAR_DRAW_DIV != 0) continue;" in text)
    check("POV back 10cm (M4g)",
          "DUEL_SEAT_POS = { 0.0f, 0.75f, -0.78f };" in text)
    check("near particles 78, mid 29",
          "#define DUEL_DEPTH_NEAR_CAP 78" in depth_text
          and "#define DUEL_DEPTH_MID_CAP 29" in depth_text)
    check("static stars experiment flag off",
          "#define DUEL_STATIC_STARS 0" in text)

    # 26. M2.2c: measured X-Wing muzzles + enemy guide chevron.
    check("measured wingtip muzzles",
          "static const float mx[4] = { 5.35f, -5.35f, 5.35f, -5.35f };" in sim_text
          and "static const float my[4] = { 1.30f, 1.30f, -1.30f, -1.30f };" in sim_text
          and "float fwd_dist = 1.82f;" in sim_text)
    # M3.8b: ship chevron replaced by the head look-cue (user order).
    check("head look-cue arrow, ship chevron gone",
          "Duel_HeadGuide(eq, cam_pos, tp->pos, &gdx, &gdy)" in text
          and "duel_v3_scale(gd, 0.14f)" in text
          and "1.0f, 0.6f, 0.1f, 0.9f" in text
          and "Duel_GuideDir(&s->player" not in text)
    check("planet-disc cull env-gated",
          "if (dot > e->cull_dot) { culled++; continue; }" in text
          and "if (r->env_tex[env]) {" in text)
    check("stars built after env load",
          text.index("build_stars_for(r, e);") > text.index("env_tex[e] = load_cook_tex"))

    # 27. M3.0 virtual hands: visual/tracking only through the seat path.
    xr_text = XR_SRC.read_text(encoding="utf-8")
    hands_text = (SRC.parent / "duel_hands.c").read_text(encoding="utf-8")
    check("hand extension requested with baseline retry",
          "XR_EXT_HAND_TRACKING_EXTENSION_NAME" in xr_text
          and "SDL_PROP_GPU_DEVICE_CREATE_XR_EXTENSION_NAMES_POINTER" in xr_text
          and "xr_device_props(props, &xr_instance, &xr_system, 0);" in xr_text)
    hands_hdr = (SRC.parent / "duel_hands.h").read_text(encoding="utf-8")
    check("joint world uses shared seat path",
          "duel_cal_neutralize(cal, j->pos, j->quat, &cp, &cq);" in hands_text
          and "duel_eye_pose_motion(ship_q, ship_t, seat_t, mo_p, mo_q, cq, cp, &eq, &ep);"
          in hands_text)
    check("joint world takes no HMD parameter",
          "DuelHands_JointWorld(const DuelSeatCal *cal," in hands_hdr
          and "hmd" not in hands_hdr.split("DuelHands_JointWorld")[1].split(";")[0])
    check("hands module never touches sim/input",
          "Duel_Update" not in hands_text
          and "DuelInput" not in hands_text
          and "duel_sim.h" not in hands_text)
    check("render emits only position-valid joints",
          "if (!j->pos_valid) continue;" in text
          and "if (!h->j[a].pos_valid || !h->j[z].pos_valid) continue;" in text)
    check("hand bones depth-tested (cockpit occludes)",
          "r->pipe_lines_test = make_pipeline" in text
          and "SDL_GPU_PRIMITIVETYPE_LINELIST,\n                                       1, 0, 0, 1, 1"
          in text)
    check("hands located once per frame after calibrate",
          "DuelRender_HandsLocate(render, display_time);" in
          (SRC.parent / "duel_main.c").read_text(encoding="utf-8"))
    manifest = (SRC.parent.parent / "app" / "src" / "main" /
                "AndroidManifest.xml").read_text(encoding="utf-8")
    check("manifest grants hand tracking",
          "com.oculus.permission.HAND_TRACKING" in manifest)
    check("manifest declares handtracking feature + V2.0 (M3.0c)",
          '<uses-feature android:name="oculus.software.handtracking"' in manifest
          and '<meta-data android:name="com.oculus.handtracking.version"' in manifest)

    # 28. M3.0a hands diagnosis: locate result + transitions observable.
    check("locate result recorded per side",
          "s_hands[i].last_result = (int)s_locate(s_tracker[i], &li, &locs);"
          in hands_text
          and "lr=%d lc=%d" in text)
    check("hand active transitions logged",
          "DUEL_HANDS transition L %d->%d" in text
          and "DUEL_HANDS transition R %d->%d" in text)

    # 29. M3.1 lever grab: pinch + proximity, throttle never touched.
    check("grab uses thumb/index tips + debounce",
          "h->j[5].pos_valid && h->j[10].pos_valid" in text
          and "DUEL_GRAB_HOLD_FRAMES" in hands_text
          and "DUEL_GRAB_REL_FRAMES" in hands_text)
    check("hands module never writes sim throttle state",
          "p->throttle" not in hands_text
          and "Duel_Update" not in hands_text
          and "duel_sim.h" not in hands_text
          and "hand_thr_active" not in hands_text)
    check("markers track moved targets + holder telemetry",
          "DuelLever_GripShip(s->player.throttle, r->defl_ld, &g0)" in text
          and "DuelStick_GripShip(r->defl_pd, r->defl_rd, &g1)" in text
          and "DUEL_GRAB holders Q=%d S=%d" in text)

    # 30. M3.2/M3.3 HOTAS: stick steers, left quadrant throttles.
    main_text = (SRC.parent / "duel_main.c").read_text(encoding="utf-8")
    cfg_text = (SRC.parent / "flight_config.h").read_text(encoding="utf-8")
    check("stick column drawn in deflection pass",
          "m->prims[m->prim_count].prim_idx = prim_idx;" in text
          and "DuelLever_IsPrim(mi, m->prims[i].prim_idx)" in text
          and "DuelStick_ModelOffset(r->defl_pd, r->defl_rd, lever_m)" in text)
    check("TIE keeps legacy single pass",
          "draw_model(r, NULL, cmd, rp, tmod, vp.m, tm.m, 0, 0, NULL)" in text)
    check("quadrant HOLDER owns throttle, stick integrates else",
          "s->hand_thr_active" in sim_text
          and "r->grab_holder[DUEL_TGT_QUAD]" in text
          and "DuelQuad_ThrottleForMid(r->grab_mid[hq].z)" in text
          and "if (hta) s->player.throttle" in sim_text
          and "DUEL_SHIP_PLAYER, 1, dt)" in sim_text)
    check("M6 axis split: stick pitch+yaw, lever throttle+roll",
          "Duel_HandStick(&sim, sa, pd, rd)" in main_text
          and "DuelRender_HandStick(render, &pd, &rd)" in main_text
          and "Duel_HandLever(&sim, la, ld)" in main_text
          and "DuelRender_HandLever(render, &ld)" in main_text
          and "s->hand_stk_active" in sim_text
          and "s->hand_lev_active" in sim_text
          and "yaw = Duel_StickCurve(s->hand_yaw" in sim_text
          and "roll = Duel_StickCurve(s->hand_lev_roll" in sim_text)
    check("M6 no bank coordination",
          "DUEL_COORD_YAW * roll" not in sim_text
          and "coordination deleted" in cfg_text)
    check("M6 lever grip follows slot + roll swing",
          "DuelLever_GripShip(s->player.throttle, r->defl_ld" in text
          and "out->x += duel_clampf(roll" in hands_text)
    check("quadrant handle solid box on slot",
          "DuelLever_GripShip(s->player.throttle, r->defl_ld, &qc)" in text
          and "r->pipe_tri_test" in text
          and "quad_range.count" in text)

    # 31. M3.4/M3.5 thumb trigger while HELD, ORed in main.
    # M3.7: per-hand loop (f is fire_l/fire_r). M3.8: touch metric
    # (thumb tip to index-intermediate), close latches.
    check("trigger gated on HELD + thumb/index joints",
          "h->j[DUEL_FIRE_REF_JOINT].pos_valid" in text
          and "#define DUEL_FIRE_JOINT 5" in hands_hdr
          and "#define DUEL_FIRE_REF_JOINT 8" in hands_hdr
          and "DuelFire_Update(f, held, fok, dst)" in text)
    # 32. M3.5 platform mesh: FB ext requested (2-deep ladder), scale
    # chained, mesh owns the joints path, skin runs the seat path.
    check("FB mesh extension requested with EXT fallback",
          "XR_FB_HAND_TRACKING_MESH_EXTENSION_NAME" in xr_text
          and "retrying EXT-only" in xr_text
          and "XrHandTrackingScaleFB" in hands_text)
    check("platform mesh owns joints + indexed skin draws",
          "#define DUEL_HAND_MESH 1" in text
          and "if (r->hand_mesh[side]) continue" in text
          and "DuelHands_Local2Game(&r->cal" in text
          and "DuelSkin_Vert(m->pos[k]" in text
          and "SDL_DrawGPUIndexedPrimitives(rp, (Uint32)r->hand_ni[hside]" in text
          and "SDL_BindGPUVertexBuffers(rp, 0, &dvb, 1)" in text)
    check("hand fire ORed in main, sim untouched",
          "int hf = DuelRender_HandFire(render);" in main_text
          and "in.fire = in.fire || hf;" in main_text
          and "HandFire" not in sim_text)
    # 33. M3.6: raw-index IBO (no double offset), index-curled thumb
    # gate, free grab assignment (either hand, either target).
    check("IBO stores raw indices, base via vertex_offset",
          "memcpy(ibuf" in text
          and "m1->idx[k] + vo" not in text
          and "r->hand_vbase[hside], 0)" in text)
    check("thumb fire requires curled index fist",
          "DuelFire_HoldOk" in text
          and "#define DUEL_FIRE_INDEX_JOINT 10" in hands_hdr
          and "#define DUEL_FIRE_INDEX_MAX 0.10f" in hands_hdr
          and "grab_holder[DUEL_TGT_STICK]" in text)
    check("free grab machines + winners drive controls",
          "DuelGrab grab[2][2]" in text
          and "DuelGrab_Assign" in text
          and "DuelGrab_Update(&r->grab[side][DUEL_TGT_QUAD]" in text
          and "DuelStick_DeflRel(r->grab_palm[hs]" in text)
    # 34. M3.7: fist-held stick (thumb can't break it), latch-relative
    # steering, either thumb fires.
    check("stick holds by fist, quadrant by pinch",
          "DuelHand_FistDist(h)" in text
          and "DuelGrab_UpdateT(&r->grab[side][DUEL_TGT_STICK]" in text
          and "#define DUEL_FIST_SHUT 0.11f" in hands_hdr
          and "#define DUEL_FIST_OPEN 0.14f" in hands_hdr
          and "#define DUEL_GRAB_PINCH_OPEN 0.085f" in hands_hdr)
    # 35. M3.8: markers/tethers off (clean cockpit), code retained.
    check("grip markers gated off by flag",
          "#define DUEL_GRAB_MARKERS 0" in text
          and "#if DUEL_GRAB_MARKERS" in text)
    # 36. M3.8b: head-following look cue replaces the ship chevron;
    # TIE rebalanced (constants, user order).
    check("head guide arrow replaces ship chevron",
          "Duel_HeadGuide(eq, cam_pos, tp->pos" in text
          and "Duel_GuideDir(&s->player" not in text
          and "duel_q_right(eq), hu = duel_q_up(eq)" in text)
    check("M5 skirmish TIE constants (easier to catch)",
          "DUEL_TIE_MAX_SPEED      120.0f" in cfg_text
          and "DUEL_TIE_TURN_RATE      0.75f" in cfg_text
          and "DUEL_TIE_EVADE_DIST     80.0f" in cfg_text
          and "DUEL_TIE_EVADE_TIME" in sim_text
          # M9: attack throttle is rookie-gated; the 0.70 baseline is pinned.
          and "t->throttle = (s->diff <= 0) ? 0.85f : 0.70f;" in sim_text)
    # 37. M3.8c: positioned stick latch + fair-kill bundle.
    check("stick latch 9cm, positioned only",
          "#define DUEL_GRAB_STICK_LATCH_R 0.09f" in hands_hdr
          and "DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R" in text
          and "wd=(%.2f,%.2f)" in text)
    check("fair-kill bundle (XWA low-skill mirror)",
          "DUEL_TIE_HP             70.0f" in cfg_text
          and "DUEL_TIE_RADIUS         7.0f" in cfg_text
          and "DUEL_TIE_LASER_COOLDOWN 0.75f" in cfg_text
          and "DUEL_TIE_WEAVE" in sim_text)
    check("grab anchor captured at latch, steering relative",
          "r->stick_anchor[side] = pp" in text
          and "DuelStick_DeflRel(r->grab_palm[hs], r->stick_anchor[hs]" in text)
    check("either thumb fires its gripping hand",
          "(r->fire_l.firing || r->fire_r.firing)" in text
          and '"DUEL_FIRE %c %s' in text
          and "r->grab_holder[DUEL_TGT_STICK] == fside" in text)
    # 38. M5 skirmish: 3 ties vs player + 2 allies, team-only damage.
    sim_hdr = (SRC.parent / "duel_sim.h").read_text(encoding="utf-8")
    check("M5 counts (M31: asymmetric forces)",
          "#define DUEL_TIE_COUNT 5" in sim_hdr
          and "#define DUEL_ALLY_COUNT 4" in sim_hdr
          and "#define DUEL_FOES_MIN 0" in sim_hdr
          and "#define DUEL_FOES_MAX 5" in sim_hdr
          and "#define DUEL_FOES_DEFAULT 3" in sim_hdr
          and "#define DUEL_ALLIES_MIN 0" in sim_hdr
          and "#define DUEL_ALLIES_MAX 4" in sim_hdr
          and "#define DUEL_ALLIES_DEFAULT 2" in sim_hdr)
    check("M5 team damage (rebel bolts hit ties only)",
          "DUEL_SHIP_ALLY" in sim_text
          and "the player's shots can never hurt friendlies" in sim_text)
    check("M5 auto-target drives diamond/lead/cue/bar",
          "Duel_AutoTarget(s)" in text
          and "const DuelShip *tp = tgt >= 0" in text)
    check("M5 allies drawn from X-wing exterior",
          'load_model(r, &r->xwing, "XwingExterior")' in text
          and "draw_model(r, NULL, cmd, rp, amod, vp.m, am.m" in text)
    check("M5 wave resets on last tie or player death",
          "Duel_TiesAlive(s) == 0" in sim_text)
    check("M5 hand-pop gate (palm+wrist valid, 3-frame debut)",
          "r->hand_show[hside]" in text
          and "h->j[0].pos_valid && h->j[1].pos_valid" in text
          and "r->hand_ok[side] >= 3" in text)
    # 39. M7 menu: pure tri slice then pure line slice (pipes need them).
    check("M7 menu pure slices (tris then lines)",
          "menu_tri_range.count = dw.count - menu_tri_range.first;" in text
          and "menu_line_range.count = dw.count - menu_line_range.first;" in text
          and text.find("menu_tri_range.count = dw.count") <
          text.find("menu_line_range.count = dw.count"))
    check("M7 menu ranges gated on panel show",
          "menu_tri_range.first = dw.count;" in text
          and "if (r->menu_show && r->menu)" in text)
    check("M7 menu anchor LOCAL=>game through eye path",
          "DuelVec3 menu_c = r->menu_center;" in text
          and "duel_cal_neutralize(&r->cal, r->menu_center, r->menu_quat" in text
          and "duel_eye_pose_motion(s->player.orient, s->player.pos, DUEL_SEAT_POS," in text
          and "duel_q_right(menu_q)" in text
          # M10: touch measures the fingertip against the converted anchor.
          and "rel = duel_v3_sub(tip, menu_c);" in text
          and "duel_q_right(r->menu_quat)" not in text)
    check("M7 menu ranges are drawn (emit => draw)",
          "SDL_DrawGPUPrimitives(rp, (Uint32)menu_tri_range.count, 1, (Uint32)menu_tri_range.first, 0);" in text
          and "SDL_DrawGPUPrimitives(rp, (Uint32)menu_line_range.count, 1, (Uint32)menu_line_range.first, 0);" in text
          and "r->pipe_tri_alpha" in text
          and text.find("menu_line_range.count = dw.count") <
          text.find("(Uint32)menu_tri_range.count, 1"))
    # 40. M7 main: sim frozen outside combat, result recorded once.
    main_text = (SRC.parent / "duel_main.c").read_text(encoding="utf-8")
    check("M7 sim only steps in combat",
          "if (in_combat_now)" in main_text
          and main_text.count("Duel_Update(&sim") == 2)
    check("M7 result recorded once on reset edge",
          "prev_phase == DUEL_PHASE_DESTROYED" in main_text
          and "DuelPilot_Record(&menu.pilot" in main_text
          and "DuelMenu_SetState(&menu, DUEL_APP_RESULT)" in main_text)
    check("M7 pause on menu button + focus loss",
          "DUEL_APP_PAUSED" in main_text
          and "DuelXr_HasFocus()" in main_text
          and "need_fire_release = 1" in main_text)
    check("M7 fresh grip after screen transitions",
          main_text.count("DuelRender_ClearGrabs(render)") >= 4)
    check("M7 pilot baseline difficulty survives reset",
          "s->diff = 1; /* M7: pilot = frozen baseline" in sim_text
          and "int df = s->diff;" in sim_text)
    # 41. M9 stability + easy: highlight activation, throttled prefs saves,
    #     boot panel re-place, rookie-only AI envelope.
    menu_text = (SRC.parent / "duel_menu.c").read_text(encoding="utf-8")
    check("M9 activation follows the visible highlight",
          "int DuelMenu_Highlighted(const DuelMenu *m)" in menu_text
          and "m->hover >= 0 ? m->hover : m->sel" in menu_text
          and "click_row = DuelMenu_Highlighted(&menu)" in main_text)
    check("M9 sticks take over from stale hand hover",
          "menu.sel = menu.hover;" in main_text
          and "menu.hover = -1;" in main_text)
    check("M9 prefs saves throttled + flushed on transition",
          "prefs_dirty = 1;" in main_text
          and "save_now - last_save_us >= 500000u" in main_text
          and "menu_act != DUEL_ACT_NONE" in main_text)
    check("M9 boot panel re-placed when seat-cal lands",
          "panel_cal_gen == 0 && gen_now > 0" in main_text
          and "DUEL menu re-place" in main_text)
    check("M9 easy default (menu + new pilot)",
          "m->difficulty = DUEL_DIFF_ROOKIE" in menu_text
          and "p->last_diff = 0;" in
          (SRC.parent / "duel_profile.c").read_text(encoding="utf-8"))
    check("M9 easy AI envelope is rookie-only",
          "evade_dist = easy ? 130.0f : DUEL_TIE_EVADE_DIST" in sim_text
          and "fire_dot = easy ? 0.980f : DUEL_TIE_FIRE_CONE_DOT" in sim_text
          and "fire_range = easy ? 500.0f : DUEL_TIE_FIRE_RANGE" in sim_text
          and "int easy = (s->diff <= 0);" in sim_text)
    check("M9 easy crossing reacquire + exits",
          "M9 easy: fly a crossing pass" in sim_text
          and "s->ai_t[idx] > 6.0f" in sim_text)
    # 42. M10 home view + touch + battle sizes.
    audio_text = (SRC.parent / "duel_audio_core.c").read_text(encoding="utf-8")
    hands_text = (SRC.parent / "duel_hands.c").read_text(encoding="utf-8")
    check("M10 death reset restores the home view",
          "duel_cal_home_restore(&r->cal)" in text
          and "reason=HOME-RESTORE" in text
          and "duel_cal_home_store(&r->cal)" in text
          and "static inline void duel_cal_home_restore(DuelSeatCal *cal)"
          in math_text
          and "cal->reason = DUEL_CAL_RESET;" in math_text)
    check("M10 menus use panel touch, not pinch",
          "DuelTouch_Level(&r->menu_press[sd]" in text
          and "return r->menu_touch;" in text
          and '"TOCA LA PANTALLA / GATILLO"' in text
          and "r->menu_pinch" not in text
          and "void DuelTouch_Level(int *pressed" in hands_text)
    check("M10 touch clicks the row touched now",
          "menu.hover = hov;" in main_text
          and "prev_press = press;" in main_text)
    check("M10 battle size applied before combat reset (M31: forces)",
          "Duel_SetForces(&sim, menu.allies_n, menu.foes_n);" in main_text
          and main_text.find("Duel_SetForces(&sim, menu.allies_n, menu.foes_n);") <
          main_text.find("Duel_ResetCombat(&sim);")
          and '"ALIADOS"' in menu_text
          and '"ENEMIGOS"' in menu_text
          and "Duel_TiesWanted(s)" in sim_text
          and "Duel_TiesAlive(s) == Duel_TiesWanted(s)" in text)
    check("M10 paused screen offers recenter",
          menu_text.count('"RECENTRAR", DUEL_ACT_RECENTER') >= 2)
    check("M10 audio first poll snapshots silently",
          "if (!st->synced) {" in audio_text
          and "st->last_alive_t[k] = s->ties[k].alive;" in audio_text)
    # 43. M11 near home panel + winner-hand touch arbitration.
    menu_hdr = (SRC.parent / "duel_menu.h").read_text(encoding="utf-8")
    check("M11 canonical home placement at 0.5 m",
          "DuelRender_PlaceMenuHome(render, &menu, hpos, hq)" in main_text
          and "duel_home_anchor(&r->cal" in text
          and "duel_v3(0, 0, -DUEL_MENU_DIST)" in text
          and "#define DUEL_MENU_DIST 0.5f" in menu_hdr
          and "static inline void duel_home_anchor(" in math_text)
    check("M11 winner hand owns hover+press",
          "r->menu_touch = r->menu_press[win] ? 1 : 0;" in text
          and "cand_row[sd] >= 0" in text)
    # 44. M12 ghost-explosion gate (no stuck fullscreen flash).
    check("M12 explosion triggers gated on seen-alive",
          "duel_fx_boom(&r->seen_player, s->player.alive" in text
          and "duel_fx_boom(&r->seen_tie[k], s->ties[k].alive" in text
          and "duel_fx_boom(&r->seen_ally[k], s->allies[k].alive" in text
          and "r->seen_tie[k] = s->ties[k].alive ? 1 : 0;" in text
          and "static inline int duel_fx_boom(" in math_text)
    check("M12 placement branch logged",
          '"DUEL menu placed home dist=%.2f"' in text
          and '"DUEL menu placed head dist=%.2f"' in text)
    # 45. M13 no visible pilot body (M4 removed per user request: menu
    # pilot profiles stay, the skinned body module is gone).
    check("M13/M35 rays: fingers none, controllers gated",
          "M4e: finger-ray draw removed (touch UI)." in text
          and "duel_menu_ray_world" in text
          and "r->menu->ray_on" in text)
    check("M13 no pilot body in render",
          "pilot_load" not in text
          and "draw_pilot" not in text
          and "skin_arms" not in text
          and "pilot_mesh_wanted" not in text
          and "pilot_bake_prim" not in text
          and "DuelPilotJoint" not in text
          and "PilotSkin" not in text
          and "PilotArm" not in text
          and "duel_pilot.h" not in text
          and "DuelHands_Local2Ship" not in text
          and not (SRC.parent / "duel_pilot.c").exists()
          and not (SRC.parent / "duel_pilot.h").exists())
    check("M13 no pilot-body tests in suite",
          "test_pilot_pose" not in test_text
          and "test_pilot_boot" not in test_text
          and "test_pilot_arm" not in test_text
          and "test_local2ship" not in test_text
          and "test_pilot_roundtrip" in test_text
          and "test_pilot_scan" in test_text)
    # 46. M14 menu/hands depth (user report: hands render behind the menu).
    # The panel used the depth-off HUD pipes drawn after the hands, so it
    # always overdrew the fingers even when touching. Now the menu has its
    # own depth-tested pipes and draws before the hands: fingers in front
    # win, hands behind the panel stay hidden.
    check("M14 menu pipes depth-tested (tris write, lines test)",
          "r->pipe_menu_tri = make_pipeline(r, r->unlit_vs, r->unlit_fs, "
          "SDL_GPU_PRIMITIVETYPE_TRIANGLELIST," in text
          and "r->pipe_menu_line = make_pipeline(r, r->unlit_vs, r->unlit_fs, "
          "SDL_GPU_PRIMITIVETYPE_LINELIST," in text
          and "SDL_BindGPUGraphicsPipeline(rp, r->pipe_menu_tri);" in text
          and "SDL_BindGPUGraphicsPipeline(rp, r->pipe_menu_line);" in text)
    check("M14 menu drawn before hands (front fingers win)",
          text.find("(Uint32)menu_tri_range.count, 1") <
          text.find("(Uint32)hbone_range.count, 1")
          and text.find("(Uint32)menu_line_range.count, 1") <
          text.find("(Uint32)r->hand_ni[hside], 1"))
    check("M14 HUD keeps the depth-off pipe (unchanged)",
          "r->pipe_tri_alpha = make_pipeline(r, r->unlit_vs, r->unlit_fs, "
          "SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,\n"
          "                                      0, 0, 0, 1, 0" in text)
    # 47. M15 grabbable menu (user report: M14 depth lets the cockpit cover
    # part of the panel). Pinch near the panel grabs it, the drag moves the
    # LOCAL anchor through the inverse eye path (cockpit-fixed), drop
    # re-faces the user, the spot survives screen changes until RECENTRAR.
    check("M15 panel grab is pinch near panel",
          "DuelPanelGrab_Update(&r->menu_pg[sd]" in text
          and "h->j[5].pos_valid && h->j[10].pos_valid" in text)
    # M17 supersedes the delta follow with a rigid tablet follow
    # (absolute place); the anchor still moves through the inverse path.
    check("M15 drag moves LOCAL anchor via inverse path",
          "duel_panel_place" in text
          and "r->menu_center = duel_panel_clamp" in text)
    check("M15 dragged panel kept across screens",
          "if (r->menu_moved)" in text
          and "DUEL menu kept dragged" in text)
    check("M15 grab suppresses click, recenter re-homes (M17: drop keeps pose)",
          "!r->menu_pg[sd].held" in text
          and "DUEL menu drop" in text
          and "r->menu_moved = 0" in text)
    # 48. M16 small panel + grab handle + opaque paint (user report: hard
    # to know where to grab; panel too big; noise in the colors). One
    # scale knob over the M11 design values, an amber handle strip with
    # explicit text above the rows, and alpha 1.0 paint (the 0.90-0.93
    # translucency bled the cockpit through = the color noise).
    # M19: user ordered 40% smaller still (x0.6 of M16).
    check("M16/M19 menu scale x0.42 over M11 design values",
          "#define DUEL_MENU_SCALE 0.42f" in menu_hdr
          and "(0.773f * DUEL_MENU_SCALE)" in menu_hdr
          and "#define DUEL_MENU_DIST 0.5f" in menu_hdr)
    # M18 supersedes the top strip (user order: sides only, no top /
    # bottom / corners): grip ticks on both side borders instead.
    check("M16/M18 grip affordance on the sides",
          "duel_menu_seg(" in text
          and "AGARRA PARA MOVER" not in text
          and "DUEL_MENU_HANDLE_H" not in text)
    check("M16 panel paint opaque (no bleed-through)",
          "0.015f, 0.020f, 0.045f, 1.0f" in text
          and "0.05f, 0.07f, 0.12f, 1.0f" in text
          and "0.10f, 0.32f, 0.42f, 1.0f" in text)
    # 49. M17 tablet follow (user report: the M15 grab box latched from
    # empty space below; want to grab the frame and have the panel move
    # WITH the hand, wrist rotation included). Latch only on the frame
    # (all edges + handle strip); while held the panel is rigidly pinned
    # to the hand (absolute place, no drift); drop keeps the hand pose.
    check("M17/M18 grab latches on the sides only",
          "near = duel_panel_frame(DUEL_MENU_PANEL_W, ph,\n"
          "                            mlx, mly, mdist);" in text)
    check("M17 held panel rigidly follows the hand",
          "duel_panel_place(" in text
          and "r->menu_pghand[sd]" in text
          and "h->j[1].rot_valid" in text)
    check("M17 drop keeps the hand orientation",
          "DUEL menu drop" in text
          and "duel_q_from_to" not in text)
    # 50. M18 depth-layered paint (user report: moving diagonal lines in
    # the colors). Since M14 the bg/rows/slider ride exactly coplanar
    # with depth write on: 1-ulp interpolation differences make
    # overlapping triangles flicker with the viewpoint (diagonals = quad
    # triangulation). Fix: stack layers in mm (bg 2, rows/borders 3,
    # slider track 3.5 / fill 4, glyphs 5); invisible parallax, no fight.
    check("M18 menu paint depth-layered (rows/slider above bg)",
          "duel_v3_scale(pn, 0.001f)" in text
          and "duel_v3_scale(pn, 0.0015f)" in text
          and "duel_v3_scale(pn, 0.002f)" in text
          and "&dw, ccr, pax, pay" in text)
    check("M18 glyphs float on the top layer",
          "-0.005f" in text)
    # 51. M19 rounds + cockpit pause button (user order: 40% smaller
    # menu, round count in the menu, pause button on the ship's right
    # side that pauses and brings the menu back). BRIEF gains a RONDAS
    # choice (1/3/5, default 3); RESULT shows the series line; a touch
    # button on the right console pauses from combat.
    check("M19 rounds choice + series line",
          '"RONDAS"' in menu_text
          and "series_best" in menu_hdr
          and "series_line" in menu_hdr
          and '"RONDA %d DE %d GANADAS: %d"' in menu_text)
    # MP-B1: MODO choice (ESCARAMUZA/DUELO) drives the duel sim; the
    # peer renders with target cue + hull bar in duel mode.
    check("MP-B1 MODO duel choice + sim wiring",
          '"MODO"' in menu_text
          and '"ESCARAMUZA"' in menu_text
          and '"DUELO"' in menu_text
          and "Duel_ResetDuel" in main_text
          and "Duel_UpdateDuel" in main_text)
    check("MP-B1 peer rendered in duel mode",
          "duel_model_matrix(s->peer.orient, s->peer.pos)" in text
          and "tp = &s->peer" in text
          and "s->peer.hp / Duel_CraftHP(s->peer.craft)" in text)
    # M21: the floating box is gone (user never saw it; the touch
    # snapped to a REAL red button on the right panel). Only the state-
    # colored PAUSA label floats; the cockpit button is the visual.
    # M24: the user photo settles it (M23 mesh40p1 was the CENTER pair,
    # a miss): the A-like button of the tilted RIGHT pair by the window
    # (Tex16 V-cell quad on mesh02p4, barycentric). Mate C 4.6 cm away.
    # M26 label glyphs are 3.2 mm (60% smaller than M24's 8 mm;
    # M25's 2.4 mm overshot, M23's 1.6 mm was subpixel at 0.5 m).
    check("M19/M26 pause on the photo A button",
          "DUEL_PAUSEBTN_SHIP" in text
          and '"PAUSA"' in text
          and "btn_line_range" in text
          and "btn_range" not in text
          and "int DuelRender_PauseButton(DuelRender *r)" in text
          and "0.3590f, 0.4713f, -1.1595f" in hands_text
          and '"PAUSA", 0.0f, -0.013f, 0.0032f' in text)
    check("M19 main pauses on cockpit button",
          "DuelRender_PauseButton(render)" in main_text
          and '"pause button"' in main_text)
    # M27: one background scene per match (arena/Death Star/nebula).
    # All envs preloaded at boot; the per-match swap is an index-only
    # SetEnv (no mid-frame GPU work); env 0 keeps the M2.2 numbers.
    env_text = (SRC.parent / "duel_env.c").read_text(encoding="utf-8")
    env_hdr = (SRC.parent / "duel_env.h").read_text(encoding="utf-8")
    check("M27 env table + preload + per-match pick",
          "#define DUEL_ENV_COUNT 3" in env_hdr
          and '"space_env.png"' in env_text
          and '"site_blue1_env.png"' in env_text
          and '"site_blue3_env.png"' in env_text
          and "deathstar_env.png" not in env_text
          and "env_tex[e] = load_cook_tex" in text
          and "build_stars_for(r, e);" in text
          and "void DuelRender_SetEnv(DuelRender *r, int env)" in text
          and "DuelRender_SetEnv(render, DuelEnv_Pick(" in main_text
          and "Duel_ResetDuel" in main_text)
    check("M27 env0 keeps the M2.2 measured numbers",
          "0.4049f" in env_text
          and "0.4922f" in env_text
          and "0.9613f" in env_text
          and "0.9175f" in env_text
          and "0.2246f" in env_text)
    # M28: XWA setup (MI NAVE + ENEMIGA over X-Wing/TIE/Interceptor).
    # Kind stays role/team; craft is the airframe (stats/geometry).
    check("M28 craft setup wiring",
          '"MI NAVE"' in menu_text
          and '"ENEMIGA"' in menu_text
          and '"TIE INT"' in menu_text
          and "Duel_SetCrafts(&sim, menu.craft_player," in main_text
          and "Duel_CraftHP(s->craft_enemy)" in sim_text
          and "DUEL_IN_MUZZ_FWD" in sim_text)
    check("M28 interceptor exterior + fallback",
          'load_model(r, &r->tiein, "TieInterceptor")' in text
          and "duel_craft_model(r, s->peer.craft)" in text
          and "r->tiein.vbo ? &r->tiein : &r->tie" in text
          and "s->peer.hp / Duel_CraftHP(s->peer.craft)" in text)
    # M29: skirmish mirror (the ENEMIGA-no-cambio fix: picks used to
    # apply to the duel only). Ties fly ENEMIGA, allies MI NAVE;
    # muzzles strictly follow the airframe (no owner override).
    check("M29 skirmish mirror",
          "DUEL_TIE_HP * dhp, s->craft_enemy" in sim_text
          and "DUEL_ALLY_HP, s->craft_player" in sim_text
          and "int craft = sh->craft;\n    (void)owner;" in sim_text
          and "duel_craft_model(r, s->ties[k].craft)" in text
          and "duel_craft_model(r, s->allies[k].craft)" in text
          and '"%s+%d VS %d %s"' in menu_text)
    # M30: cruiser battle (ISD gun platform, MODO 3-way). Hull spheres
    # + turret volleys in sim; pseudo-ship drives cue/lead/bar; kill
    # stages across the hull; player bar is craft-sized.
    check("M30 cruiser battle wiring",
          '"CRUCERO"' in menu_text
          and '"TORRETAS: %d"' in menu_text
          and "(m->duel_mode + step + 3) % 3" in menu_text
          and "Duel_ResetCruiser(&sim)" in main_text
          and "Duel_UpdateCruiser(&sim, &in, dt)" in main_text
          and "duel_cruiser_volley(s, dt);" in sim_text
          and "duel_cruiser_ram(s, dt);" in sim_text
          and 'load_model(r, &r->isd, "StarDestroyer")' in text
          and "draw_model(r, NULL, cmd, rp, &r->isd, vp.m, cm.m" in text
          and "cship.pos = s->cruiser.pos;" in text
          and "s->cruiser.hp / DUEL_CRUISER_HP" in text
          and "s->player.hp / Duel_CraftHP(s->player.craft)" in text
          and '"DUEL cruiser destroyed"' in text)
    # M31: asymmetric forces (ALIADOS 0..4 + ENEMIGOS 0..5, skirmish
    # clamps foes >= 1, cruiser escorts from the pick, 1v0 = guns-only).
    check("M31 asymmetric forces",
          "Duel_SetForces(DuelState *s, int allies, int foes)" in sim_text
          and "Duel_TiesWanted(s); i < DUEL_TIE_COUNT; i++" in sim_text
          and "Duel_AlliesWanted(s); i < DUEL_ALLY_COUNT; i++" in sim_text
          and '"TORR:10+TIE:%d"' in menu_text
          and "duel_cruiser_ram_one(&s->ties[k], s, dt);" in sim_text)
    # M32: cruiser subsystems (10 turrets + 2 domes + bridge, 15/15/30
    # bolts; dead turret skipped, dead bridge silences, dead domes
    # double hull damage) with diamonds + micro bars; ENEMIGA locks
    # in a guns-only cruiser.
    check("M32 cruiser subsystems",
          "duel_cruiser_sub_redirect(s, l)" in sim_text
          and "sub_hp[bi] -= dmg;" in sim_text
          and "dmg * duel_shield_mult(s)" in sim_text
          and "Duel_CruiserSubPos(s, bi)" in sim_text
          and "Duel_CruiserSubPos(s, bi)" in text
          and "DUEL_CRUISER_SUBS; bi++" in text
          and '"---"' in menu_text
          and "m->duel_mode == 2 && m->foes_n == 0" in menu_text)
    # M33: stick expo (deadzone rescale + cubic blend) on every turn
    # axis, stick and hand levers alike: fine aim near center, full
    # rate at the stops.
    check("M33 stick expo",
          "float Duel_StickCurve(float x, float dead)" in sim_text
          and "Duel_StickCurve(in->pitch, DUEL_DEADZONE)" in sim_text
          and "Duel_StickCurve(s->hand_pitch, s->hand_dead)" in sim_text
          and "Duel_StickCurve(s->hand_lev_roll, s->hand_dead)" in sim_text)
    # M34: CONTINUE? on AI defeat (the lost battle holds behind RESULT;
    # CONTINUAR respawns the player into it, REPETIR/MENU unchanged).
    check("M34 continue after death",
          "DUEL_ACT_CONTINUE" in menu_text
          and '"CONTINUAR?"' in menu_text
          and '"CONTINUAR", DUEL_ACT_CONTINUE' in menu_text
          and "void Duel_RespawnPlayer(DuelState *s)" in sim_text
          and "!s->result_hold" in sim_text
          and "sim.result_hold = 1;" in main_text
          and "Duel_RespawnPlayer(&sim);" in main_text
          and '"DUEL defeat: CONTINUAR?' in main_text)
    # M35: (a) menu flicker: the HUD gate is actually read + the HUD
    # is re-shown on combat entry; (b) dash HP% readout; (c) controller
    # ray + A/B (XWAQuestVR parity) with a RAYO MANDO option.
    input_text = (SRC.parent / "duel_input.c").read_text(encoding="utf-8")
    check("M35 menu flicker gate",
          "if (!r->menu_hide_hud) {" in text
          and "DuelRender_SetCombatHud(render, 1);" in main_text)
    check("M35 dash HP readout",
          "DUEL_DASHHP_SHIP" in text
          and '"%d%%"' in text
          and "DUEL_DASHHP_SHIP" in hands_text)
    check("M35 controller ray + AB",
          '"RAYO MANDO"' in menu_text
          and "DuelMenu_RayHit(" in menu_text
          and "r->menu->ray_on" in text
          and "DuelRender_ControllerAim" in text
          and "DuelRender_ControllerAim(render, 0" in main_text
          and "abtn_edge" in main_text
          and "bbtn_edge" in main_text
          and "/user/hand/right/input/a/click" in input_text
          and "/user/hand/right/input/b/click" in input_text
          and "/user/hand/right/input/aim/pose" in input_text)
    # M36: sticks un-reversed (right hand flies by default, ZURDA mirrors
    # via OPCIONES->MANO), either trigger fires, CONTINUE? respawns
    # shielded (same spot, 3 s damage-free).
    check("M36 handed sticks + mercy shield",
          '"MANO"' in menu_text
          and "DuelInput_SetLeftHanded(menu.hand_left);" in main_text
          and "s_hand_left ? lx : rx" in input_text
          and "s_fire_r || s_fire_l" in input_text
          and "shield_t = DUEL_RESPAWN_SHIELD_S;" in sim_text
          and "shield_t <= 0.0f" in sim_text)
    # M37: ray dot lands on the panel (cursor feel) + the cruiser guns
    # obey difficulty (rookie sprays wide and slow, ace tight + fast).
    check("M37 ray dot + kind cruiser",
          "M37 ray dot" in text
          and "DUEL_CRUISER_VOLLEY_CD * dcd" in sim_text
          and "rookie x2.0 (sprays), ace x0.6 (snipes)" in sim_text)
    # M38: the beam dies where the dot lands (one plane test drives
    # both; full beam only on a clean panel miss).
    check("M38 beam dies at dot",
          "DuelMenu_RayPlane(" in menu_text
          and "DuelMenu_RayPlane(r->menu" in text
          and "beam dies at the" in text)

    if FAILURES:
        print(f"{len(FAILURES)} FAILURES")
        return 1
    print("ALL RENDER RULE TESTS PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
