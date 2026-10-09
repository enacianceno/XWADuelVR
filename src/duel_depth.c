/* XWADuelVR M2 REALSPACE depth field (see duel_depth.h). */
#include "duel_depth.h"

static unsigned mix32(unsigned x) {
    x += 0x9E3779B9u;
    x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
    x = (x ^ (x >> 13)) * 0xC2B2AE35u;
    x ^= x >> 16;
    return x;
}

unsigned duel_depth_hash(int layer, int cx, int cy, int cz, int stream) {
    /* Unsigned arithmetic: deterministic on every platform. */
    unsigned h = (unsigned)stream + 0x9E3779B9u;
    h = mix32(h + (unsigned)layer);
    h = mix32(h + (unsigned)cx);
    h = mix32(h + (unsigned)cy);
    h = mix32(h + (unsigned)cz);
    return h;
}

void duel_depth_cell(int layer, int cx, int cy, int cz,
                     DuelDepthP *out, int *count) {
    float cell = layer == DUEL_DEPTH_MID ? DUEL_DEPTH_MID_CELL : DUEL_DEPTH_NEAR_CELL;
    unsigned h0 = duel_depth_hash(layer, cx, cy, cz, 0);
    int n, i;
    /* 0..2 particles, avg 0.875/cell: 5/8 one, 1/8 two, 2/8 empty. */
    n = (h0 % 8 < 5) ? 1 : ((h0 % 8 == 5) ? 2 : 0);
    if (count) *count = n;
    for (i = 0; i < n; i++) {
        unsigned hx = duel_depth_hash(layer, cx, cy, cz, 1 + 3 * i);
        unsigned hy = duel_depth_hash(layer, cx, cy, cz, 2 + 3 * i);
        unsigned hz = duel_depth_hash(layer, cx, cy, cz, 3 + 3 * i);
        unsigned hm = duel_depth_hash(layer, cx, cy, cz, 101 + i);
        unsigned hb = duel_depth_hash(layer, cx, cy, cz, 201 + i);
        unsigned ht = duel_depth_hash(layer, cx, cy, cz, 301 + i);
        float fx = (float)(hx % 1000) / 1000.0f;
        float fy = (float)(hy % 1000) / 1000.0f;
        float fz = (float)(hz % 1000) / 1000.0f;
        out[i].pos.x = ((float)cx + fx) * cell;
        out[i].pos.y = ((float)cy + fy) * cell;
        out[i].pos.z = ((float)cz + fz) * cell;
        out[i].size = 0.5f + (float)(hm % 1000) / 1000.0f;
        out[i].bright = 0.25f + 0.75f * (float)(hb % 1000) / 1000.0f;
        out[i].type = (ht % 8 == 0) ? 2 : (int)(ht % 2); /* rare glints */
        out[i].cx = cx; out[i].cy = cy; out[i].cz = cz;
        out[i].slot = i;
    }
}

float duel_depth_fade(int layer, float dist) {
    float mn, fi, fo, mx, t;
    if (layer == DUEL_DEPTH_MID) {
        mn = DUEL_DEPTH_MID_MIN; fi = DUEL_DEPTH_MID_FADEIN;
        fo = DUEL_DEPTH_MID_FADEOUT; mx = DUEL_DEPTH_MID_MAX;
    } else {
        mn = DUEL_DEPTH_NEAR_MIN; fi = DUEL_DEPTH_NEAR_FADEIN;
        fo = DUEL_DEPTH_NEAR_FADEOUT; mx = DUEL_DEPTH_NEAR_MAX;
    }
    if (dist < mn || dist > mx) return 0.0f;
    if (dist < fi) { t = (dist - mn) / (fi - mn); return t * t * (3.0f - 2.0f * t); }
    if (dist > fo) { t = (mx - dist) / (mx - fo); return t * t * (3.0f - 2.0f * t); }
    return 1.0f;
}

int duel_depth_collect(const DuelVec3 *center, int layer,
                       DuelDepthP *out, int cap) {
    float cell = layer == DUEL_DEPTH_MID ? DUEL_DEPTH_MID_CELL : DUEL_DEPTH_NEAR_CELL;
    int half = layer == DUEL_DEPTH_MID ? DUEL_DEPTH_MID_HALF : DUEL_DEPTH_NEAR_HALF;
    float maxd = layer == DUEL_DEPTH_MID ? DUEL_DEPTH_MID_MAX : DUEL_DEPTH_NEAR_MAX;
    /* Capped nearest-first set, kept sorted by dist2 (insertion; n small). */
    DuelDepthP best[DUEL_DEPTH_NEAR_CAP > DUEL_DEPTH_MID_CAP ?
                    DUEL_DEPTH_NEAR_CAP : DUEL_DEPTH_MID_CAP];
    float bestd[DUEL_DEPTH_NEAR_CAP > DUEL_DEPTH_MID_CAP ?
                DUEL_DEPTH_NEAR_CAP : DUEL_DEPTH_MID_CAP];
    int nbest = 0;
    int ccx = (int)floorf(center->x / cell);
    int ccy = (int)floorf(center->y / cell);
    int ccz = (int)floorf(center->z / cell);
    int ix, iy, iz, i, k;
    /* Coverage: worst-case face reach from the center cell is half*cell
     * (near 900 m > 650, mid 5000 m > 4000), so every in-fade particle is
     * always visited; only fade-0 corners are culled. No popping. */
    if (cap > (int)(sizeof best / sizeof best[0])) cap = (int)(sizeof best / sizeof best[0]);
    for (ix = -half; ix <= half; ix++)
        for (iy = -half; iy <= half; iy++)
            for (iz = -half; iz <= half; iz++) {
                DuelDepthP ps[DUEL_DEPTH_PER_CELL];
                int cnt = 0, j;
                duel_depth_cell(layer, ccx + ix, ccy + iy, ccz + iz, ps, &cnt);
                for (j = 0; j < cnt; j++) {
                    float dx = ps[j].pos.x - center->x;
                    float dy = ps[j].pos.y - center->y;
                    float dz = ps[j].pos.z - center->z;
                    float d2 = dx * dx + dy * dy + dz * dz;
                    float d;
                    int at;
                    if (d2 > maxd * maxd) continue;
                    d = sqrtf(d2);
                    if (duel_depth_fade(layer, d) <= 0.0f) continue;
                    /* insert sorted (nearest first), drop farthest past cap */
                    at = nbest;
                    while (at > 0 && bestd[at - 1] > d2) at--;
                    if (at >= cap) continue;
                    for (k = (nbest < cap ? nbest : cap - 1); k > at; k--) {
                        best[k] = best[k - 1];
                        bestd[k] = bestd[k - 1];
                    }
                    best[at] = ps[j];
                    bestd[at] = d2;
                    if (nbest < cap) nbest++;
                }
            }
    for (i = 0; i < nbest; i++) out[i] = best[i];
    return nbest;
}
