/* XWADuelVR drone: headless chase pilot (pilot 2 over UDP).
 * B1: runs on the Quest next to the game (localhost) or on a PC on
 * the LAN. Receives snapshots, steers with DuelDrone_Step, returns
 * stick input. Usage: duel_drone [game_ip [game_port [drone_port]]] */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* nanosleep under -std=c99 */
#endif
#include "duel_net.h"

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
    const char *game_ip = argc > 1 ? argv[1] : "127.0.0.1";
    int game_port = argc > 2 ? atoi(argv[2]) : DUEL_NET_GAME_PORT;
    int drone_port = argc > 3 ? atoi(argv[3]) : DUEL_NET_DRONE_PORT;
    DuelNet net;
    DuelNetSnap snap;
    DuelNetInput in;
    unsigned char buf[DUEL_NET_MAXDAT];
    int have_snap = 0, n_in = 0, n_snap = 0, tick = 0, was_alive = 0;
    if (!DuelNet_Open(&net, drone_port)) {
        printf("drone: cannot bind UDP %d\n", drone_port);
        return 1;
    }
    memset(&snap, 0, sizeof(snap));
    printf("drone: up 127.0.0.1:%d -> game %s:%d\n", drone_port,
           game_ip, game_port);
    fflush(stdout);
    for (;;) {
        int got = DuelNet_RecvSnap(&net, &snap);
        if (got) {
            have_snap = 1;
            n_snap++;
            if (snap.r_alive && !was_alive)
                printf("drone: round live (seq=%u)\n", snap.seq);
            if (!snap.r_alive && was_alive)
                printf("drone: I died (winner=%d)\n", snap.winner);
            was_alive = snap.r_alive;
        }
        memset(&in, 0, sizeof(in));
        if (have_snap) DuelDrone_Step(&snap, &in);
        {
            int len = DuelNet_InputEncode(&in, buf, sizeof(buf));
            if (len > 0 &&
                DuelNet_SendTo(&net, buf, len, game_ip, game_port))
                n_in++;
        }
        if (++tick % 60 == 0) {
            printf("drone: t=%.1f rx snap=%d tx in=%d self=(%.0f,%.0f,%.0f)%s\n",
                   snap.time, n_snap, n_in, snap.r_pos.x, snap.r_pos.y,
                   snap.r_pos.z, have_snap ? "" : " WAITING-GAME");
            fflush(stdout);
        }
        duel_msleep(1000 / DUEL_NET_INPUT_HZ);
    }
}
