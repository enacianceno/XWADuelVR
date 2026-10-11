/* XWADuelVR nettest: B1 loopback integration on-device.
 * Game-side DuelState + UDP vs a LIVE drone process (start it first).
 * Scripted P1 (throttle + weave + fire); the drone flies for real.
 * PASS: two-way traffic + peer displaced + peer fired its gun.
 * Usage: duel_nettest [drone_ip [drone_port [game_port]]] */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* nanosleep under -std=c99 */
#endif
#include "duel_net.h"
#include "duel_sim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void duel_msleep(int ms) { Sleep(ms); }
#else
#include <time.h>
static void duel_msleep(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

int main(int argc, char **argv) {
    const char *drone_ip = argc > 1 ? argv[1] : "127.0.0.1";
    int drone_port = argc > 2 ? atoi(argv[2]) : DUEL_NET_DRONE_PORT;
    int game_port = argc > 3 ? atoi(argv[3]) : DUEL_NET_GAME_PORT;
    DuelNet net;
    DuelState s;
    DuelNetInput in2;
    DuelInput in1;
    DuelVec3 spawn;
    unsigned char buf[DUEL_NET_MAXDAT];
    int tick, rx = 0, tx = 0, deaths = 0;
    float disp = 0.0f;
    if (!DuelNet_Open(&net, game_port)) {
        printf("nettest: cannot bind UDP %d\n", game_port);
        return 1;
    }
    memset(&in1, 0, sizeof(in1));
    memset(&in2, 0, sizeof(in2));
    Duel_Init(&s);
    Duel_ResetDuel(&s);
    spawn = s.peer.pos;
    printf("nettest: duel live, drone at %s:%d\n", drone_ip, drone_port);
    fflush(stdout);
    for (tick = 0; tick < 60 * 90; tick++) {
        DuelNetInput got;
        DuelNetSnap snap;
        int len;
        float t = (float)tick / 60.0f;
        if (DuelNet_RecvInput(&net, &got)) {
            in2 = got;
            rx++;
        }
        /* Scripted P1: full throttle, gentle weave, always firing. */
        in1.thr_axis = 1.0f;
        in1.yaw = 0.4f * sinf(t * 0.7f);
        in1.fire = 1;
        {
            DuelInput p2;
            p2.yaw = in2.yaw;
            p2.pitch = in2.pitch;
            p2.roll = in2.roll;
            p2.thr_axis = in2.thr;
            p2.fire = in2.fire;
            p2.menu = 0;
            p2.raw_lx = p2.raw_ly = p2.raw_rx = p2.raw_ry = 0.0f;
            Duel_UpdateDuel(&s, &in1, &p2, 1.0f / 60.0f);
        }
        if (!s.player.alive || !s.peer.alive) deaths++;
        if (tick % 3 == 0) {
            memset(&snap, 0, sizeof(snap));
            snap.seq = net.next_seq++;
            snap.time = s.time;
            snap.p_pos = s.player.pos;
            snap.p_orient = s.player.orient;
            snap.p_speed = s.player.speed;
            snap.p_alive = s.player.alive;
            snap.r_pos = s.peer.pos;
            snap.r_orient = s.peer.orient;
            snap.r_speed = s.peer.speed;
            snap.r_alive = s.peer.alive;
            snap.phase = (int)s.phase;
            snap.winner = s.duel_winner;
            len = DuelNet_SnapEncode(&snap, buf, sizeof(buf));
            if (len > 0 &&
                DuelNet_SendTo(&net, buf, len, drone_ip, drone_port))
                tx++;
        }
        {
            DuelVec3 d = duel_v3_sub(s.peer.pos, spawn);
            float l = duel_v3_len(d);
            if (l > disp) disp = l;
        }
        if (tick == 600 && rx == 0) {
            printf("nettest: FAIL no drone input in 10 s (start duel_drone first)\n");
            DuelNet_Close(&net);
            return 1;
        }
        duel_msleep(16);
    }
    printf("nettest: rx_in=%d tx_snap=%d peer_disp=%.0f peer_shots=%d deaths=%d winner=%d\n",
           rx, tx, disp, s.peer.muzzle, deaths > 0 ? 1 : 0,
           s.duel_winner);
    DuelNet_Close(&net);
    if (rx > 100 && tx > 100 && disp > 50.0f && s.peer.muzzle > 0) {
        printf("NETTEST PASS\n");
        return 0;
    }
    printf("nettest: FAIL traffic/motion/gun gate\n");
    return 1;
}
