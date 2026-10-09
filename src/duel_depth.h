/* XWADuelVR M2 REALSPACE: deterministic world-cell depth field (SDL-free).
 * FAR (authentic XWA stars) is untouched; this module adds sparse NEAR +
 * MID world-anchored references so translation/parallax reads in VR.
 * A cell's content is a PURE function of (layer, cx, cy, cz): same cell
 * always yields the same particles, with no ship/head/random state.
 * Particles never chase the ship; the ship flies through the field. */
#ifndef XWADUELVR_DEPTH_H
#define XWADUELVR_DEPTH_H

#include "duel_math.h"

#define DUEL_DEPTH_NEAR 0
#define DUEL_DEPTH_MID 1

#define DUEL_DEPTH_NEAR_CELL 300.0f   /* m */
#define DUEL_DEPTH_NEAR_HALF 3        /* active cells: 7^3 = 343; face
                                       * coverage 900 m > NEAR_MAX 650 m */
#define DUEL_DEPTH_NEAR_MIN 25.0f     /* discard closer (never in cockpit) */
#define DUEL_DEPTH_NEAR_FADEIN 60.0f  /* 0 at MIN -> full at FADEIN */
#define DUEL_DEPTH_NEAR_FADEOUT 550.0f
#define DUEL_DEPTH_NEAR_MAX 650.0f
#define DUEL_DEPTH_NEAR_CAP 78 /* M2.2b +30%, M2.2c +20%: 50 -> 65 -> 78 */

#define DUEL_DEPTH_MID_CELL 2500.0f
#define DUEL_DEPTH_MID_HALF 2         /* active cells: 5^3 = 125; face
                                       * coverage 5000 m > MID_MAX 4000 m */
#define DUEL_DEPTH_MID_MIN 450.0f
#define DUEL_DEPTH_MID_FADEIN 700.0f
#define DUEL_DEPTH_MID_FADEOUT 3500.0f
#define DUEL_DEPTH_MID_MAX 4000.0f
#define DUEL_DEPTH_MID_CAP 29 /* M2.2c: +20% moving references */

#define DUEL_DEPTH_PER_CELL 2 /* max particles in one cell */

typedef struct DuelDepthP {
    DuelVec3 pos;   /* world position (inside its cell) */
    float size;     /* 0.5..1.5 relative */
    float bright;   /* 0..1 base brightness */
    int type;       /* 0 speck, 1 mote, 2 glint */
    int cx, cy, cz; /* source cell */
    int slot;       /* index within the cell (0..1) */
} DuelDepthP;

/* Cell content: 0..2 particles, deterministic. count may be NULL-checked. */
void duel_depth_cell(int layer, int cx, int cy, int cz,
                     DuelDepthP *out, int *count);

/* Gather one layer around world point `center` (SHIP pos, never HMD).
 * Returns particle count (<= cap); out must hold `cap` entries. Farther
 * cells do not displace nearer ones: candidates past `cap` are dropped
 * only after sorting by distance... (M2.1: shell order around center
 * keeps the nearest; see duel_depth_collect for the guarantee). */
int duel_depth_collect(const DuelVec3 *center, int layer,
                       DuelDepthP *out, int cap);

/* Distance fade 0..1 for one layer (0 outside [MIN, MAX]). */
float duel_depth_fade(int layer, float dist);

/* SplitMix32-grade integer hash for cell coords (public for tests). */
unsigned duel_depth_hash(int layer, int cx, int cy, int cz, int stream);

#endif /* XWADUELVR_DEPTH_H */
