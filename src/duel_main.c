/* XWADuelVR entry: BOOT -> COMBAT -> DESTROYED -> RESET, forever.
 * No menus. SDL provides the activity/threading; XR provides the display. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <string.h>

#include "duel_audio.h"
#include "duel_input.h"
#include "duel_math.h"
#include "duel_render.h"
#include "duel_sim.h"
#include "duel_xr.h"

int SDL_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    SDL_Log("DUEL BOOT XWADuelVR P0");
    /* VIDEO is required for GPU device creation even with no SDL window:
     * XR swapchains are our only render targets. */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("DUEL SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_GPUDevice *device = NULL;
    if (!DuelXr_Init(&device)) {
        SDL_Log("DUEL XR init failed");
        SDL_Quit();
        return 1;
    }
    if (!DuelInput_Init(DuelXr_Instance(), DuelXr_Session())) {
        SDL_Log("DUEL input init failed");
        DuelXr_Shutdown();
        SDL_Quit();
        return 1;
    }
    char asset_dir[512];
    {
        const char *internal = SDL_GetAndroidInternalStoragePath();
        if (!internal) internal = "/data/user/0/org.openxwa.xwaduellvr/files";
        snprintf(asset_dir, sizeof asset_dir, "%s/duel_assets", internal);
    }
    SDL_Log("DUEL assets: %s", asset_dir);
    DuelRender *render =
        DuelRender_Create(device, DuelXr_SwapchainFormat(), asset_dir);
    if (!DuelRender_Ready(render)) {
        SDL_Log("DUEL renderer not ready (assets missing?)");
        DuelRender_Destroy(render);
        DuelInput_Shutdown();
        DuelXr_Shutdown();
        SDL_Quit();
        return 1;
    }
    DuelState sim;
    Duel_Init(&sim);
    DuelAudio *audio = NULL;
    {
        char adir[600];
        snprintf(adir, sizeof(adir), "%s/audio", asset_dir);
        audio = DuelAudio_Create(adir);
    }

    Uint64 last_us = SDL_GetTicksNS() / 1000u;
    Uint64 stat_us = last_us;
    Uint64 ctrl_us = last_us; /* P0.5 §21: DUEL_CTRL 5 Hz gate */
    int frames = 0;
    int running = 1;
    while (running && !DuelXr_ShouldQuit()) {
        DuelXr_PollEvents();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) running = 0;
        }
        if (!running) break;
        int should_render = 0;
        XrTime display_time = 0;
        if (!DuelXr_WaitBeginFrame(&should_render, &display_time)) continue;
        XrView views[DUEL_EYE_COUNT];
        int have_views = should_render && DuelXr_LocateViews(display_time, views);
        int rendered = 0;
        {
            /* 5 s heartbeat even without views: distinguishes "waiting for
             * HMD/focus" from "stuck before first frame". */
            static Uint64 wait_us = 0;
            static int wait_logged = 0;
            Uint64 now_us = SDL_GetTicksNS() / 1000u;
            if (!have_views && (!wait_logged || now_us - wait_us > 5000000u)) {
                SDL_Log("DUEL waiting: should_render=%d (wear HMD / grant focus)",
                        should_render);
                wait_us = now_us;
                wait_logged = 1;
            }
            if (have_views) wait_logged = 0;
        }
        if (have_views) {
            Uint64 now_us = SDL_GetTicksNS() / 1000u;
            float dt = (float)(now_us - last_us) / 1000000.0f;
            last_us = now_us;
            if (dt < 0.0f) dt = 0.0f;
            if (dt > 0.1f) dt = 0.1f;
            DuelInput in;
            DuelInput_Poll(&in, dt, display_time);
            /* M3.2: a held hand owns the throttle (previous frame's grab);
             * otherwise the stick integrator runs. */
            {
                float hv = 0.0f;
                int ha = DuelRender_HandThrottle(render, &hv);
                Duel_HandThrottle(&sim, ha, hv);
            }
            /* M3.3: a held hand owns pitch/yaw (previous frame's
             * deflection); otherwise the controller axes run. M6: the
             * stick lateral is pure yaw; roll comes from the left
             * lever (independent channel, both hands at once). */
            {
                float pd = 0.0f, rd = 0.0f;
                int sa = DuelRender_HandStick(render, &pd, &rd);
                Duel_HandStick(&sim, sa, pd, rd);
            }
            {
                float ld = 0.0f;
                int la = DuelRender_HandLever(render, &ld);
                Duel_HandLever(&sim, la, ld);
            }
            /* M3.4: index trigger ORs with the controller trigger. The
             * sim is untouched: fire cooldown/ownership live there. */
            in.fire = in.fire || DuelRender_HandFire(render);
            DuelVec3 bF = duel_q_forward(sim.player.orient);
            DuelVec3 bU = duel_q_up(sim.player.orient);
            DuelVec3 bR = duel_q_right(sim.player.orient);
            Duel_Update(&sim, &in, dt);
            /* P0.5 §21: stick-active control telemetry (5 Hz max). Tells A
             * (bad raw) from B (bad mapping) from C (bad ship basis) from
             * E (calibration recaptured: generation jumps). D (camera math)
             * is pinned by the §10 unit test + DUEL_CAL cam_ship. */
            float stick_mag = fabsf(in.raw_lx);
            if (fabsf(in.raw_ly) > stick_mag) stick_mag = fabsf(in.raw_ly);
            if (fabsf(in.raw_rx) > stick_mag) stick_mag = fabsf(in.raw_rx);
            if (fabsf(in.raw_ry) > stick_mag) stick_mag = fabsf(in.raw_ry);
            if (stick_mag > 0.25f && now_us - ctrl_us >= 200000u) {
                ctrl_us = now_us;
                DuelVec3 aF = duel_q_forward(sim.player.orient);
                DuelVec3 aU = duel_q_up(sim.player.orient);
                DuelVec3 aR = duel_q_right(sim.player.orient);
                int gen = -1; DuelVec3 chp; float chyaw = 0.0f;
                DuelRender_CalTelemetry(render, &gen, NULL, &chp, &chyaw);
                DuelVec3 csl = duel_v3_add(DUEL_SEAT_POS, chp);
                SDL_Log("DUEL_CTRL gen=%d raw=(%.2f,%.2f,%.2f,%.2f) cmd ypr=(%.2f,%.2f,%.2f) thr=%.2f "
                        "F %.2f,%.2f,%.2f->%.2f,%.2f,%.2f U %.2f,%.2f,%.2f->%.2f,%.2f,%.2f "
                        "R %.2f,%.2f,%.2f->%.2f,%.2f,%.2f head=(%.2f,%.2f,%.2f,%.0fdeg) csl=(%.2f,%.2f,%.2f)",
                        gen, in.raw_lx, in.raw_ly, in.raw_rx, in.raw_ry,
                        in.yaw, in.pitch, in.roll, sim.player.throttle,
                        bF.x, bF.y, bF.z, aF.x, aF.y, aF.z,
                        bU.x, bU.y, bU.z, aU.x, aU.y, aU.z,
                        bR.x, bR.y, bR.z, aR.x, aR.y, aR.z,
                        chp.x, chp.y, chp.z, chyaw * 180.0f / (float)DUEL_PI,
                        csl.x, csl.y, csl.z);
            }
            DuelRender_Update(render, &sim, dt);
            DuelRender_Calibrate(render, &sim, &views[0].pose, &views[1].pose, have_views);
            DuelRender_HandsLocate(render, display_time); /* M3.0: once per frame */
            XrPosef poses[DUEL_EYE_COUNT];
            XrFovf fovs[DUEL_EYE_COUNT];
            int eyes_ok = 1;
            for (int e = 0; e < DUEL_EYE_COUNT; e++) {
                poses[e] = views[e].pose;
                fovs[e] = views[e].fov;
                SDL_GPUTexture *target = NULL;
                int32_t w = 0, h = 0;
                if (DuelXr_AcquireEye(e, &target) < 0 || !target ||
                    !DuelXr_EyeSize(e, &w, &h)) {
                    eyes_ok = 0;
                    break;
                }
                SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
                if (!cmd) {
                    static int cmd_fail = 0;
                    if (cmd_fail < 3) {
                        SDL_Log("DUEL AcquireGPUCommandBuffer failed eye=%d", e);
                        cmd_fail++;
                    }
                    DuelXr_ReleaseEye(e);
                    eyes_ok = 0;
                    break;
                }
                DuelRender_Eye(render, cmd, target, DuelXr_EyeDepth(e), w, h,
                               &views[e].pose, &views[e].fov, &sim);
                SDL_SubmitGPUCommandBuffer(cmd);
                DuelXr_ReleaseEye(e);
            }
            rendered = eyes_ok;
            frames++;
            if (now_us - stat_us >= 1000000u) {
                float fps = (float)frames * 1000000.0f / (float)(now_us - stat_us);
                int lasers = 0;
                for (int i = 0; i < DUEL_LASER_POOL; i++) lasers += sim.lasers[i].active;
                DuelVec3 pfwd = duel_q_forward(sim.player.orient);
                DuelVec3 pup = duel_q_up(sim.player.orient);
                /* M5: log the auto-target (nearest living tie). */
                int tgt = Duel_AutoTarget(&sim);
                DuelVec3 tpos = tgt >= 0 ? sim.ties[tgt].pos : duel_v3(0, 0, 0);
                float thp = tgt >= 0 ? sim.ties[tgt].hp : 0.0f;
                int tai = tgt >= 0 ? (int)sim.ai[tgt] : -1;
                SDL_Log("DUEL fps=%.1f phase=%d php=%.0f thp=%.0f ai=%d lasers=%d kills=%d/%d ppos=(%.0f,%.0f,%.0f) pspd=%.0f tpos=(%.0f,%.0f,%.0f) pfwd=(%.2f,%.2f,%.2f) pup=(%.2f,%.2f,%.2f) raw=(%.2f,%.2f,%.2f,%.2f) ypr=(%.2f,%.2f,%.2f) tgt=%d ties=%d allies=%d",
                        fps, (int)sim.phase, sim.player.hp, thp, tai, lasers,
                        sim.kills_player, sim.kills_tie,
                        sim.player.pos.x, sim.player.pos.y, sim.player.pos.z, sim.player.speed,
                        tpos.x, tpos.y, tpos.z,
                        pfwd.x, pfwd.y, pfwd.z, pup.x, pup.y, pup.z,
                        in.raw_lx, in.raw_ly, in.raw_rx, in.raw_ry,
                        in.yaw, in.pitch, in.roll,
                        tgt, Duel_TiesAlive(&sim), Duel_AlliesAlive(&sim));
                frames = 0;
                stat_us = now_us;
            }
        }
        /* P1.2 audio: runs every loop (music before first views is fine). */
        if (audio) {
            DuelQuat hq;
            DuelVec3 hp;
            DuelListener lis;
            DuelRender_HeadTelemetry(render, &hq, &hp);
            lis.pos = hp;
            lis.fwd = duel_q_forward(hq);
            lis.right = duel_q_right(hq);
            DuelAudio_Update(audio, &sim, lis, 1.0f / 60.0f);
        }
        {
            XrPosef poses[DUEL_EYE_COUNT];
            XrFovf fovs[DUEL_EYE_COUNT];
            for (int e = 0; e < DUEL_EYE_COUNT; e++) {
                if (have_views) {
                    poses[e] = views[e].pose;
                    fovs[e] = views[e].fov;
                } else {
                    memset(&poses[e], 0, sizeof poses[e]);
                    poses[e].orientation.w = 1.0f;
                    memset(&fovs[e], 0, sizeof fovs[e]);
                }
            }
            DuelXr_EndFrame(display_time, poses, fovs, rendered);
        }
    }
    SDL_Log("DUEL shutdown");
    DuelAudio_Destroy(audio);
    DuelRender_Destroy(render);
    DuelInput_Shutdown();
    DuelXr_Shutdown();
    SDL_Quit();
    return 0;
}
