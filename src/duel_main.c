/* XWADuelVR entry: BOOT -> COMBAT -> DESTROYED -> RESET, forever.
 * No menus. SDL provides the activity/threading; XR provides the display. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "duel_audio.h"
#include "duel_input.h"
#include "duel_math.h"
#include "duel_menu.h"
#include "duel_profile.h"
#include "duel_render.h"
#include "duel_sim.h"
#include "duel_xr.h"

/* M7 options: deadzone then sensitivity gain on a -1..1 axis. */
static float duel_shape_axis(float v, float dead, float sens) {
    float a = fabsf(v);
    float s, g;
    if (a < dead) return 0.0f;
    s = (a - dead) / (1.0f - dead);
    g = s * sens;
    if (g > 1.0f) g = 1.0f;
    return (v < 0.0f) ? -g : g;
}

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
    char prof_dir[512]; /* M7: prefs + pilots live in the files root */
    {
        const char *internal = SDL_GetAndroidInternalStoragePath();
        if (!internal) internal = "/data/user/0/org.openxwa.xwaduellvr/files";
        snprintf(asset_dir, sizeof asset_dir, "%s/duel_assets", internal);
        snprintf(prof_dir, sizeof prof_dir, "%s", internal);
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
    /* M7 menu boot: prefs + pilot restore (close/reopen persistence). */
    DuelMenu menu;
    DuelPrefs prefs;
    DuelMenu_Init(&menu);
    DuelPrefs_Load(&prefs, prof_dir);
    menu.vol_master = prefs.vol_master;
    menu.vol_music = prefs.vol_music;
    menu.vol_sfx = prefs.vol_sfx;
    menu.sens = prefs.sens;
    menu.dead = prefs.dead;
    menu.npilots = DuelPilot_Scan(prof_dir, menu.pilot_names, DUEL_PILOT_MAX);
    menu.pilot_idx = -1;
    {
        int i;
        for (i = 0; i < menu.npilots; i++) {
            if (!strcmp(menu.pilot_names[i], prefs.last_pilot)) {
                menu.pilot_idx = i;
                break;
            }
        }
        if (menu.pilot_idx < 0 && menu.npilots > 0) menu.pilot_idx = 0;
        if (menu.pilot_idx >= 0) {
            DuelPilot_Load(&menu.pilot, prof_dir,
                           menu.pilot_names[menu.pilot_idx]);
            menu.difficulty = menu.pilot.last_diff;
        }
    }
    DuelMenu_RefreshPilots(&menu);
    if (audio)
        DuelAudio_SetVolumes(audio, menu.vol_master, menu.vol_music,
                             menu.vol_sfx);
    SDL_Log("DUEL menu boot: pilots=%d sel=%d prefs=%s", menu.npilots,
            menu.pilot_idx, prefs.last_pilot);

    Uint64 last_us = SDL_GetTicksNS() / 1000u;
    Uint64 stat_us = last_us;
    Uint64 ctrl_us = last_us; /* P0.5 §21: DUEL_CTRL 5 Hz gate */
    int frames = 0;
    int running = 1;
    /* M7 menu/combat state machine edges + placement. */
    int prev_menu_btn = 0, prev_fire = 0, prev_press = 0;
    int nav_y = 0, nav_x = 0;
    int panel_state = -1;
    int panel_cal_gen = 0; /* M9: cal gen at panel placement (boot fix) */
    int prefs_dirty = 0; /* M9: prefs changed, save pending (throttled) */
    Uint64 last_save_us = 0;
    int need_fire_release = 0;
    int was_focus = 1;
    int prev_phase = DUEL_PHASE_COMBAT;
    int combat_kills0 = 0;
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
            /* M7 options shape the flight axes (menu nav uses raw_*). */
            in.yaw = duel_shape_axis(in.yaw, menu.dead, menu.sens);
            in.pitch = duel_shape_axis(in.pitch, menu.dead, menu.sens);
            in.roll = duel_shape_axis(in.roll, menu.dead, menu.sens);
            in.thr_axis = duel_shape_axis(in.thr_axis, menu.dead, menu.sens);
            Duel_SetHandDead(&sim, menu.dead);
            DuelRender_SetHandSens(render, menu.sens);
            int menu_edge = in.menu && !prev_menu_btn; prev_menu_btn = in.menu;
            int fire_edge = in.fire && !prev_fire; prev_fire = in.fire;
            /* M7 menu screens: panel placement (once per screen,
             * world-locked) + panel touch + controller fallback. */
            if (menu.state != DUEL_APP_COMBAT) {
                float ov_m = menu.vol_master, ov_u = menu.vol_music;
                float ov_s = menu.vol_sfx, ose = menu.sens, ode = menu.dead;
                int menu_act = DUEL_ACT_NONE; /* M9: flush prefs on transit */
                if (panel_state != (int)menu.state) {
                    /* M11: canonical home placement (0.5 m, cockpit-fixed;
                     * reselects land in the same spot, never run away). */
                    DuelVec3 hpos = duel_v3(
                        (views[0].pose.position.x + views[1].pose.position.x) * 0.5f,
                        (views[0].pose.position.y + views[1].pose.position.y) * 0.5f,
                        (views[0].pose.position.z + views[1].pose.position.z) * 0.5f);
                    DuelQuat hq = duel_q(views[0].pose.orientation.x,
                                         views[0].pose.orientation.y,
                                         views[0].pose.orientation.z,
                                         views[0].pose.orientation.w);
                    DuelRender_PlaceMenuHome(render, &menu, hpos, hq);
                    DuelRender_SetCombatHud(render,
                        menu.state == DUEL_APP_PAUSED ? 1 : 0);
                    DuelRender_ClearGrabs(render);
                    panel_state = (int)menu.state;
                    DuelRender_CalTelemetry(render, &panel_cal_gen, NULL,
                                            NULL, NULL);
                    SDL_Log("DUEL menu screen=%d", (int)menu.state);
                } else {
                    /* M9 boot fix: the first panel is placed before seat-cal
                     * capture (~2 s settle); re-place once when cal lands so
                     * the panel sits 1.1 m in front instead of wherever the
                     * pre-cal anchor converts. One-shot (gen 0 -> N). */
                    int gen_now = 0;
                    DuelRender_CalTelemetry(render, &gen_now, NULL, NULL,
                                            NULL);
                    if (panel_cal_gen == 0 && gen_now > 0) {
                        panel_state = -1;
                        SDL_Log("DUEL menu re-place: cal gen 0->%d",
                                gen_now);
                    }
                }
                {
                    int hov = -1, act = DUEL_ACT_NONE, arg = -1;
                    float hx = 0.0f, hy = 0.0f;
                    menu_act = DUEL_ACT_NONE;
                    int press = DuelRender_MenuPointer(render, &hov, &hx, &hy);
                    int click_row = -1;
                    if (hov >= 0) menu.hover = hov;
                    else if (!press) menu.hover = -1;
                    /* M10 touch: slider drag while pressed (level); click
                     * on the press-make edge, at the row touched NOW. */
                    if (press && menu.hover >= 0) {
                        DuelMenuRow rows[DUEL_MENU_MAXROWS];
                        int n = DuelMenu_Rows(&menu, rows, DUEL_MENU_MAXROWS);
                        if (menu.hover < n &&
                                rows[menu.hover].kind == DUEL_ROW_SLIDER) {
                            float x0, y0, x1, y1;
                            if (DuelMenu_RowRect(&menu, menu.hover,
                                                 &x0, &y0, &x1, &y1) &&
                                    x1 > x0)
                                DuelMenu_SetSlider(&menu, menu.hover,
                                    (hx - x0) / (x1 - x0));
                            menu.sel = menu.hover;
                        } else if (!prev_press) {
                            menu.hover = hov;
                            click_row = menu.hover;
                        }
                    }
                    prev_press = press;
                    /* Controller fallback: stick nav + trigger/menu.
                     * M9: sticks take over where the highlight is (a stale
                     * hand hover never hijacks controller input), and the
                     * trigger/menu activates the VISIBLE highlight. */
                    {
                        int ny = 0, nx = 0;
                        if (in.raw_ly < -0.6f) ny = -1;
                        else if (in.raw_ly > 0.6f) ny = 1;
                        if (in.raw_lx > 0.6f || in.raw_rx > 0.6f) nx = 1;
                        else if (in.raw_lx < -0.6f || in.raw_rx < -0.6f) nx = -1;
                        if ((ny && ny != nav_y) || (nx && nx != nav_x)) {
                            if (menu.hover >= 0) {
                                menu.sel = menu.hover;
                                menu.hover = -1;
                            }
                        }
                        if (ny && ny != nav_y) DuelMenu_Nav(&menu, ny);
                        if (nx && nx != nav_x) DuelMenu_Adjust(&menu, nx);
                        nav_y = ny;
                        nav_x = nx;
                        if ((fire_edge || menu_edge) && click_row < 0)
                            click_row = DuelMenu_Highlighted(&menu);
                    }
                    if (click_row >= 0) {
                        arg = DuelMenu_RowArg(&menu, click_row);
                        act = DuelMenu_ActivateRow(&menu, click_row);
                        menu.sel = click_row;
                        /* M9 repro log: every menu activation on one line. */
                        SDL_Log("DUEL menu click screen=%d row=%d act=%d",
                                (int)menu.state, click_row, (int)act);
                    }
                    switch (act) {
                    case DUEL_ACT_ENTER_BRIEF:
                        DuelMenu_SetState(&menu, DUEL_APP_BRIEF);
                        break;
                    case DUEL_ACT_ENTER_OPTIONS:
                        menu.options_from = menu.state;
                        DuelMenu_SetState(&menu, DUEL_APP_OPTIONS);
                        break;
                    case DUEL_ACT_ENTER_PILOTS:
                        DuelMenu_SetState(&menu, DUEL_APP_PILOTS);
                        break;
                    case DUEL_ACT_BACK:
                        DuelMenu_SetState(&menu, menu.state == DUEL_APP_OPTIONS ?
                            menu.options_from : DUEL_APP_MENU);
                        break;
                    case DUEL_ACT_START:
                    case DUEL_ACT_RESTART:
                    case DUEL_ACT_REPEAT:
                        /* M10: size + difficulty BEFORE the reset (spawn
                         * counts/HP use the menu values, not stale ones). */
                        Duel_SetDifficulty(&sim, menu.difficulty);
                        Duel_SetSkirmish(&sim, menu.skirmish);
                        Duel_ResetCombat(&sim);
                        /* M19 series: START opens round 1, REPEAT advances
                         * (a fresh series after the last round), RESTART
                         * replays the current round untouched. */
                        if (act == DUEL_ACT_START) {
                            menu.series_round = 1;
                            menu.series_won = 0;
                        } else if (act == DUEL_ACT_REPEAT) {
                            if (menu.series_round >= 1 &&
                                    menu.series_round < menu.series_best)
                                menu.series_round++;
                            else {
                                menu.series_round = 1;
                                menu.series_won = 0;
                            }
                        }
                        DuelRender_ClearGrabs(render);
                        DuelMenu_SetState(&menu, DUEL_APP_COMBAT);
                        DuelRender_SetMenuPanel(render, 0, NULL,
                            duel_v3(0, 0, 0), duel_q_identity());
                        panel_state = -1;
                        combat_kills0 = sim.kills_player;
                        need_fire_release = 1;
                        prev_phase = DUEL_PHASE_COMBAT;
                        SDL_Log("DUEL combat start diff=%d size=%dv%d pilot=%s round=%d/%d won=%d",
                                menu.difficulty, Duel_TiesWanted(&sim),
                                Duel_TiesWanted(&sim), menu.pilot.name,
                                menu.series_round, menu.series_best,
                                menu.series_won);
                        break;
                    case DUEL_ACT_RESUME:
                        DuelMenu_SetState(&menu, DUEL_APP_COMBAT);
                        DuelRender_SetMenuPanel(render, 0, NULL,
                            duel_v3(0, 0, 0), duel_q_identity());
                        panel_state = -1;
                        DuelRender_ClearGrabs(render);
                        need_fire_release = 1;
                        SDL_Log("DUEL resume");
                        break;
                    case DUEL_ACT_QUIT_TO_MENU:
                        DuelMenu_SetState(&menu, DUEL_APP_MENU);
                        DuelRender_ClearGrabs(render);
                        SDL_Log("DUEL quit to menu");
                        break;
                    case DUEL_ACT_RECENTER:
                        DuelRender_RequestRecenter(render);
                        break;
                    case DUEL_ACT_EXIT_APP:
                        SDL_Log("DUEL exit requested from menu");
                        running = 0;
                        break;
                    case DUEL_ACT_SELECT_PILOT:
                        if (arg >= 0 && arg < menu.npilots) {
                            menu.pilot_idx = arg;
                            DuelPilot_Load(&menu.pilot, prof_dir,
                                           menu.pilot_names[arg]);
                            DuelMenu_RefreshPilots(&menu);
                            snprintf(prefs.last_pilot,
                                     sizeof(prefs.last_pilot), "%s",
                                     menu.pilot_names[arg]);
                            DuelPrefs_Save(&prefs, prof_dir);
                        }
                        break;
                    case DUEL_ACT_NEW_PILOT:
                        if (menu.npilots < DUEL_PILOT_MAX) {
                            char nm[DUEL_PILOT_NAME_MAX + 1];
                            int t, u, free;
                            for (t = 1; t <= DUEL_PILOT_MAX; t++) {
                                snprintf(nm, sizeof(nm), "PILOTO%d", t);
                                free = 1;
                                for (u = 0; u < menu.npilots; u++) {
                                    if (!strcmp(menu.pilot_names[u], nm)) {
                                        free = 0;
                                        break;
                                    }
                                }
                                if (free) break;
                            }
                            if (free) {
                                DuelPilot np;
                                DuelPilot_Defaults(&np, nm);
                                if (DuelPilot_Save(&np, prof_dir)) {
                                    menu.npilots = DuelPilot_Scan(prof_dir,
                                        menu.pilot_names, DUEL_PILOT_MAX);
                                    menu.pilot_idx = -1;
                                    for (u = 0; u < menu.npilots; u++) {
                                        if (!strcmp(menu.pilot_names[u], nm)) {
                                            menu.pilot_idx = u;
                                            break;
                                        }
                                    }
                                    menu.pilot = np;
                                    DuelMenu_RefreshPilots(&menu);
                                    DuelMenu_SetState(&menu, DUEL_APP_PILOTS);
                                    snprintf(prefs.last_pilot,
                                             sizeof(prefs.last_pilot), "%s", nm);
                                    DuelPrefs_Save(&prefs, prof_dir);
                                }
                            }
                        }
                        break;
                    case DUEL_ACT_DELETE_PILOT:
                        if (menu.pilot_idx >= 0 &&
                                menu.pilot_idx < menu.npilots) {
                            DuelPilot_Delete(prof_dir, menu.pilot_names[menu.pilot_idx]);
                            menu.npilots = DuelPilot_Scan(prof_dir,
                                menu.pilot_names, DUEL_PILOT_MAX);
                            menu.pilot_idx =
                                menu.npilots > 0 ? 0 : -1;
                            if (menu.pilot_idx >= 0) {
                                DuelPilot_Load(&menu.pilot, prof_dir,
                                    menu.pilot_names[menu.pilot_idx]);
                                snprintf(prefs.last_pilot,
                                         sizeof(prefs.last_pilot), "%s",
                                         menu.pilot_names[menu.pilot_idx]);
                            } else {
                                DuelPilot_Defaults(&menu.pilot, "");
                                prefs.last_pilot[0] = '\0';
                            }
                            DuelMenu_RefreshPilots(&menu);
                            DuelMenu_SetState(&menu, DUEL_APP_PILOTS);
                            DuelPrefs_Save(&prefs, prof_dir);
                        }
                        break;
                    default:
                        break;
                    }
                    menu_act = act;
                }
                /* Prefs apply live; persist throttled (M9: a slider drag
                 * changes values every frame -- disk writes stay at 2 Hz,
                 * flushed immediately on any screen transition). */
                if (ov_m != menu.vol_master || ov_u != menu.vol_music ||
                        ov_s != menu.vol_sfx || ose != menu.sens ||
                        ode != menu.dead) {
                    prefs.vol_master = menu.vol_master;
                    prefs.vol_music = menu.vol_music;
                    prefs.vol_sfx = menu.vol_sfx;
                    prefs.sens = menu.sens;
                    prefs.dead = menu.dead;
                    prefs_dirty = 1;
                    if (audio)
                        DuelAudio_SetVolumes(audio, menu.vol_master,
                                             menu.vol_music, menu.vol_sfx);
                }
                if (prefs_dirty) {
                    Uint64 save_now = SDL_GetTicksNS() / 1000u;
                    if (menu_act != DUEL_ACT_NONE ||
                            save_now - last_save_us >= 500000u) {
                        last_save_us = save_now;
                        prefs_dirty = 0;
                        DuelPrefs_Save(&prefs, prof_dir);
                    }
                }
            }
            int in_combat_now = (menu.state == DUEL_APP_COMBAT);
            /* M19: cockpit pause button (consumed edge, combat only). */
            int pausebtn = in_combat_now ?
                DuelRender_PauseButton(render) : 0;
            if (in_combat_now &&
                    (menu_edge || pausebtn ||
                        (!DuelXr_HasFocus() && was_focus))) {
                /* M9 repro log: pause reason on one line. */
                SDL_Log("DUEL pause: %s",
                        !DuelXr_HasFocus() ? "focus lost" :
                        (pausebtn ? "pause button" : "menu button"));
                DuelMenu_SetState(&menu, DUEL_APP_PAUSED);
                DuelRender_ClearGrabs(render);
                in_combat_now = 0;
            }
            was_focus = DuelXr_HasFocus();
            DuelVec3 bF = duel_q_forward(sim.player.orient);
            DuelVec3 bU = duel_q_up(sim.player.orient);
            DuelVec3 bR = duel_q_right(sim.player.orient);
            if (in_combat_now) {
                /* M3.2: a held hand owns the throttle (previous frame's
                 * grab); otherwise the stick integrator runs. */
                {
                    float hv = 0.0f;
                    int ha = DuelRender_HandThrottle(render, &hv);
                    Duel_HandThrottle(&sim, ha, hv);
                }
                /* M3.3/M6: hand stick + left lever publish. */
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
                /* M3.4/M7: fire OR, gated until release after any
                 * screen transition (no accidental first-frame shot). */
                {
                    int hf = DuelRender_HandFire(render);
                    if (need_fire_release) {
                        if (!in.fire && !hf) need_fire_release = 0;
                        in.fire = 0;
                    } else {
                        in.fire = in.fire || hf;
                    }
                }
                if (sim.phase == DUEL_PHASE_DESTROYED) {
                    /* Destroyed beat: world animates, input neutral. */
                    DuelInput neutral;
                    memset(&neutral, 0, sizeof(neutral));
                    Duel_Update(&sim, &neutral, dt);
                } else {
                    Duel_Update(&sim, &in, dt);
                }
                /* Sim auto-reset rebuilt the fight: freeze it, record
                 * the result ONCE, show the RESULT screen. */
                if (prev_phase == DUEL_PHASE_DESTROYED &&
                        sim.phase == DUEL_PHASE_COMBAT) {
                    int win = sim.player.alive ? 1 : 0;
                    int kills = sim.kills_player - combat_kills0;
                    menu.result_win = win;
                    menu.result_kills = kills < 0 ? 0 : kills;
                    /* M19: series score before RESULT (the series line
                     * refreshes on SetState). */
                    if (win) menu.series_won++;
                    if (menu.pilot_idx >= 0) {
                        DuelPilot_Record(&menu.pilot, win,
                                         menu.result_kills, menu.difficulty);
                        DuelPilot_Save(&menu.pilot, prof_dir);
                        DuelMenu_RefreshPilots(&menu);
                    }
                    DuelMenu_SetState(&menu, DUEL_APP_RESULT);
                    DuelRender_ClearGrabs(render);
                    SDL_Log("DUEL result: %s kills=%d pilot=%s",
                            win ? "VICTORIA" : "DERROTA",
                            menu.result_kills, menu.pilot.name);
                }
                prev_phase = sim.phase;
            }
            /* P0.5 §21: stick-active control telemetry (5 Hz max). Tells A
             * (bad raw) from B (bad mapping) from C (bad ship basis) from
             * E (calibration recaptured: generation jumps). D (camera math)
             * is pinned by the §10 unit test + DUEL_CAL cam_ship. */
            float stick_mag = fabsf(in.raw_lx);
            if (fabsf(in.raw_ly) > stick_mag) stick_mag = fabsf(in.raw_ly);
            if (fabsf(in.raw_rx) > stick_mag) stick_mag = fabsf(in.raw_rx);
            if (fabsf(in.raw_ry) > stick_mag) stick_mag = fabsf(in.raw_ry);
            if (in_combat_now && stick_mag > 0.25f && now_us - ctrl_us >= 200000u) {
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
