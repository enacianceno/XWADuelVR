/* XWADuelVR net: 1v1 duel wire protocol + UDP transport.
 * B1: localhost game<->drone; the same datagrams ride LAN/internet
 * later (discovery/relay only change addressing, never the payload).
 * Wire: little-endian, floats raw IEEE-754; magic+proto gate junk. */
#ifndef XWADUELVR_NET_H
#define XWADUELVR_NET_H

#include "duel_math.h"

#include <stdint.h>

#define DUEL_NET_PROTO 1
#define DUEL_NET_MAGIC 0x314C5544u /* "DUL1" */
#define DUEL_NET_GAME_PORT 17531
#define DUEL_NET_DRONE_PORT 17532
#define DUEL_NET_SNAP_HZ 20
#define DUEL_NET_INPUT_HZ 30
#define DUEL_NET_MAXDAT 256

/* Authoritative-ish snapshot, game -> peer @20 Hz. p_* = pilot 1
 * (the sender), r_* = echo of the pilot-2 ship (so the drone can
 * steer in its own body frame and see deaths). */
typedef struct DuelNetSnap {
    unsigned seq;
    float time;
    DuelVec3 p_pos;
    DuelQuat p_orient;
    float p_speed;
    int p_alive;
    DuelVec3 r_pos;
    DuelQuat r_orient;
    float r_speed;
    int r_alive;
    int phase;
    int winner;
} DuelNetSnap;

/* Stick input, peer -> game @30 Hz. Same axes as DuelInput. */
typedef struct DuelNetInput {
    unsigned seq;
    float yaw;
    float pitch;
    float roll;
    float thr;
    int fire;
} DuelNetInput;

/* Codec: encode returns bytes (0 = cap too small); decode 1 ok,
 * 0 reject (short/truncated/wrong magic+proto). */
int DuelNet_SnapEncode(const DuelNetSnap *s, unsigned char *out, int cap);
int DuelNet_SnapDecode(const unsigned char *buf, int len, DuelNetSnap *out);
int DuelNet_InputEncode(const DuelNetInput *in, unsigned char *out, int cap);
int DuelNet_InputDecode(const unsigned char *buf, int len, DuelNetInput *out);

/* Non-blocking UDP endpoint (POSIX; _WIN32 via Winsock glue). */
typedef struct DuelNet {
    intptr_t fd; /* int, or SOCKET on _WIN32 (64-bit: no int store) */
    unsigned next_seq;
} DuelNet;

int DuelNet_Open(DuelNet *n, int local_port); /* 1 ok, 0 fail */
void DuelNet_Close(DuelNet *n);
/* Drain to the LATEST datagram: 1 + *out when anything arrived. */
int DuelNet_RecvInput(DuelNet *n, DuelNetInput *out);
int DuelNet_RecvSnap(DuelNet *n, DuelNetSnap *out);
int DuelNet_SendTo(DuelNet *n, const unsigned char *buf, int len,
                   const char *ip, int port); /* 1 sent, 0 fail */

/* Headless chase pilot (pure: shared by the drone binary + tests).
 * Steers the r_* ship toward p_*, matches speed, fires in the cone.
 * Neutral + no fire when either ship is dead. */
void DuelDrone_Step(const DuelNetSnap *view, DuelNetInput *out);

#endif /* XWADUELVR_NET_H */
