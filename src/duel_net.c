/* XWADuelVR net implementation: LE codec, UDP sockets, drone AI. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* sockets + O_NONBLOCK under -std=c99 */
#endif
#include "duel_net.h"
#include "flight_config.h"

#include <math.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET duel_sock_t;
#define DUEL_SOCK_BAD INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int duel_sock_t;
#define DUEL_SOCK_BAD (-1)
#endif

static void duel_put32(unsigned char *b, unsigned v) {
    b[0] = (unsigned char)(v & 0xFFu);
    b[1] = (unsigned char)((v >> 8) & 0xFFu);
    b[2] = (unsigned char)((v >> 16) & 0xFFu);
    b[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static unsigned duel_get32(const unsigned char *b) {
    return (unsigned)b[0] | ((unsigned)b[1] << 8) |
           ((unsigned)b[2] << 16) | ((unsigned)b[3] << 24);
}

static void duel_putf(unsigned char *b, float v) {
    unsigned u;
    memcpy(&u, &v, 4);
    duel_put32(b, u);
}

static float duel_getf(const unsigned char *b) {
    unsigned u = duel_get32(b);
    float v;
    memcpy(&v, &u, 4);
    return v;
}

static void duel_putv3(unsigned char *b, DuelVec3 v) {
    duel_putf(b, v.x);
    duel_putf(b + 4, v.y);
    duel_putf(b + 8, v.z);
}

static DuelVec3 duel_getv3(const unsigned char *b) {
    DuelVec3 v;
    v.x = duel_getf(b);
    v.y = duel_getf(b + 4);
    v.z = duel_getf(b + 8);
    return v;
}

static void duel_putq(unsigned char *b, DuelQuat q) {
    duel_putf(b, q.x);
    duel_putf(b + 4, q.y);
    duel_putf(b + 8, q.z);
    duel_putf(b + 12, q.w);
}

static DuelQuat duel_getq(const unsigned char *b) {
    DuelQuat q;
    q.x = duel_getf(b);
    q.y = duel_getf(b + 4);
    q.z = duel_getf(b + 8);
    q.w = duel_getf(b + 12);
    return q;
}

/* Snapshot layout: magic u32, proto u32, seq u32, time f32,
 * p pos v3, p orient q, p speed f32, p alive u32,
 * r pos v3, r orient q, r speed f32, r alive u32,
 * phase u32, winner u32. Total 100 bytes. */
#define DUEL_SNAP_LEN 100
#define DUEL_INPUT_LEN 32 /* magic, proto, seq, yaw, pitch, roll, thr, fire */

int DuelNet_SnapEncode(const DuelNetSnap *s, unsigned char *out, int cap) {
    if (!s || !out || cap < DUEL_SNAP_LEN) return 0;
    duel_put32(out, DUEL_NET_MAGIC);
    duel_put32(out + 4, DUEL_NET_PROTO);
    duel_put32(out + 8, s->seq);
    duel_putf(out + 12, s->time);
    duel_putv3(out + 16, s->p_pos);
    duel_putq(out + 28, s->p_orient);
    duel_putf(out + 44, s->p_speed);
    duel_put32(out + 48, s->p_alive ? 1u : 0u);
    duel_putv3(out + 52, s->r_pos);
    duel_putq(out + 64, s->r_orient);
    duel_putf(out + 80, s->r_speed);
    duel_put32(out + 84, s->r_alive ? 1u : 0u);
    duel_put32(out + 88, (unsigned)s->phase);
    duel_put32(out + 92, (unsigned)s->winner);
    return DUEL_SNAP_LEN;
}

int DuelNet_SnapDecode(const unsigned char *buf, int len, DuelNetSnap *out) {
    if (!buf || !out || len < DUEL_SNAP_LEN) return 0;
    if (duel_get32(buf) != DUEL_NET_MAGIC) return 0;
    if (duel_get32(buf + 4) != DUEL_NET_PROTO) return 0;
    out->seq = duel_get32(buf + 8);
    out->time = duel_getf(buf + 12);
    out->p_pos = duel_getv3(buf + 16);
    out->p_orient = duel_getq(buf + 28);
    out->p_speed = duel_getf(buf + 44);
    out->p_alive = duel_get32(buf + 48) ? 1 : 0;
    out->r_pos = duel_getv3(buf + 52);
    out->r_orient = duel_getq(buf + 64);
    out->r_speed = duel_getf(buf + 80);
    out->r_alive = duel_get32(buf + 84) ? 1 : 0;
    out->phase = (int)duel_get32(buf + 88);
    out->winner = (int)duel_get32(buf + 92);
    return 1;
}

int DuelNet_InputEncode(const DuelNetInput *in, unsigned char *out, int cap) {
    if (!in || !out || cap < DUEL_INPUT_LEN) return 0;
    duel_put32(out, DUEL_NET_MAGIC);
    duel_put32(out + 4, DUEL_NET_PROTO);
    duel_put32(out + 8, in->seq);
    duel_putf(out + 12, in->yaw);
    duel_putf(out + 16, in->pitch);
    duel_putf(out + 20, in->roll);
    duel_putf(out + 24, in->thr);
    duel_put32(out + 28, in->fire ? 1u : 0u);
    return DUEL_INPUT_LEN;
}

int DuelNet_InputDecode(const unsigned char *buf, int len, DuelNetInput *out) {
    if (!buf || !out || len < DUEL_INPUT_LEN) return 0;
    if (duel_get32(buf) != DUEL_NET_MAGIC) return 0;
    if (duel_get32(buf + 4) != DUEL_NET_PROTO) return 0;
    out->seq = duel_get32(buf + 8);
    out->yaw = duel_getf(buf + 12);
    out->pitch = duel_getf(buf + 16);
    out->roll = duel_getf(buf + 20);
    out->thr = duel_getf(buf + 24);
    out->fire = duel_get32(buf + 28) ? 1 : 0;
    return 1;
}

int DuelNet_Open(DuelNet *n, int local_port) {
    duel_sock_t fd;
    struct sockaddr_in addr;
    if (!n) return 0;
    n->fd = -1;
    n->next_seq = 1;
#ifdef _WIN32
    {
        WSADATA wd;
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return 0;
    }
#endif
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == DUEL_SOCK_BAD) return 0;
#ifdef _WIN32
    {
        u_long nb = 1;
        if (ioctlsocket(fd, FIONBIO, &nb) != 0) {
            closesocket(fd);
            return 0;
        }
    }
#else
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) {
            close(fd);
            return 0;
        }
    }
#endif
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)local_port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return 0;
    }
    n->fd = (intptr_t)fd;
    return 1;
}

void DuelNet_Close(DuelNet *n) {
    if (!n || n->fd < 0) return;
#ifdef _WIN32
    closesocket((duel_sock_t)n->fd);
    WSACleanup();
#else
    close(n->fd);
#endif
    n->fd = -1;
}

static int duel_recv_dgram(DuelNet *n, unsigned char *buf, int cap) {
    int got = -1;
    if (!n || n->fd < 0) return -1;
    for (;;) { /* drain to the latest (UDP: only fresh state matters) */
        int r = (int)recv((duel_sock_t)n->fd, (char *)buf, cap, 0);
        if (r <= 0) break;
        got = r;
    }
    return got;
}

int DuelNet_RecvInput(DuelNet *n, DuelNetInput *out) {
    unsigned char buf[DUEL_NET_MAXDAT];
    int r = duel_recv_dgram(n, buf, sizeof(buf));
    if (r < DUEL_INPUT_LEN || r >= DUEL_SNAP_LEN) return 0;
    return DuelNet_InputDecode(buf, r, out);
}

int DuelNet_RecvSnap(DuelNet *n, DuelNetSnap *out) {
    unsigned char buf[DUEL_NET_MAXDAT];
    int r = duel_recv_dgram(n, buf, sizeof(buf));
    if (r <= 0) return 0;
    /* A snap (100 B) and an input (32 B) share the port pair: route
     * by length so a crossed datagram never parses as the other. */
    if (r < DUEL_SNAP_LEN) return 0;
    return DuelNet_SnapDecode(buf, r, out);
}

int DuelNet_SendTo(DuelNet *n, const unsigned char *buf, int len,
                   const char *ip, int port) {
    struct sockaddr_in addr;
    int r;
    if (!n || n->fd < 0 || !buf || len <= 0 || !ip) return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return 0;
    r = (int)sendto((duel_sock_t)n->fd, (const char *)buf, len, 0,
                    (struct sockaddr *)&addr, sizeof(addr));
    return r == len ? 1 : 0;
}

/* Chase pilot: steer r_* at p_*, close to ~150 m, fire in the cone.
 * Body-frame errors (forward=-Z): yaw>0 noses +X, pitch>0 noses +Y
 * (same signs as the sim integrator). Throttle is bang-bang with a
 * deadband (the sim integrates thr_axis, it is not absolute). */
void DuelDrone_Step(const DuelNetSnap *view, DuelNetInput *out) {
    DuelVec3 to, fwd, bd;
    DuelQuat inv;
    float dist, dot, want, yaw, pitch;
    static unsigned seq = 1;
    memset(out, 0, sizeof(*out));
    out->seq = seq++;
    if (!view || !view->p_alive || !view->r_alive) return;
    to = duel_v3_sub(view->p_pos, view->r_pos);
    dist = duel_v3_len(to);
    if (dist < 1.0f) return;
    to = duel_v3_scale(to, 1.0f / dist);
    fwd = duel_q_forward(view->r_orient);
    dot = duel_v3_dot(fwd, to);
    /* World dir into body frame (unit quat: inverse = conjugate). */
    inv.x = -view->r_orient.x;
    inv.y = -view->r_orient.y;
    inv.z = -view->r_orient.z;
    inv.w = view->r_orient.w;
    bd = duel_q_rot(inv, to);
    yaw = duel_clampf(2.0f * atan2f(bd.x, -bd.z), -1.0f, 1.0f);
    pitch = duel_clampf(2.0f * atan2f(bd.y, sqrtf(bd.x * bd.x + bd.z * bd.z)),
                        -1.0f, 1.0f);
    out->yaw = yaw;
    out->pitch = pitch;
    out->roll = 0.0f;
    want = view->p_speed + (dist - 150.0f) * 0.5f;
    want = duel_clampf(want, DUEL_PLAYER_MIN_SPEED, DUEL_PLAYER_MAX_SPEED);
    if (view->r_speed < want - 5.0f) out->thr = 1.0f;
    else if (view->r_speed > want + 5.0f) out->thr = -1.0f;
    else out->thr = 0.0f;
    out->fire = (dot > 0.995f && dist < 400.0f) ? 1 : 0;
}
