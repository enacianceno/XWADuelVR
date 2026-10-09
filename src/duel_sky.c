/* XWADuelVR P1.2 star generator: see duel_sky.h for the port notes. */
#include "duel_sky.h"

#include <math.h>
#include <string.h>

/* Bit-exact field on every platform/libm: forbid FMA contraction so the
 * shipped VBO, the unit tests and the python oracle agree bit-for-bit. */
#pragma STDC FP_CONTRACT OFF

/* Original palette-cycle brightness tables (values 0..23). */
static const unsigned char C0[39] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1
};
static const unsigned char C1[64] = {
    0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 6, 7, 8, 10, 12, 14, 16, 17,
    18, 19, 20, 20, 21, 21, 22, 22, 22, 23, 23, 23, 23, 22, 22, 22, 21, 21,
    20, 20, 19, 18, 17, 16, 14, 12, 10, 8, 7, 6, 5, 4, 4, 3, 3, 2, 2, 1, 1, 1,
    0, 0, 0
};
static const unsigned char C2[45] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 23, 23, 22, 20, 18, 16,
    14, 12, 10, 8, 6, 4, 2, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 2, 1, 0, 0, 0, 0, 0,
    0, 0, 0
};
static const unsigned char C3[57] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 23, 23, 23, 23, 20, 22, 18, 21, 18, 20, 19, 16, 18,
    16, 17, 16, 15, 13, 14, 12, 12, 11, 10, 8, 5, 2, 0, 0, 0, 0, 0, 0, 0
};
static const unsigned char *const CYCLES[4] = { C0, C1, C2, C3 };
static const unsigned CYCLE_N[4] = { 39, 64, 45, 57 };

/* Q15 working vector (original encoding, int math). */
typedef struct { int x, y, z; } QVec;

void duel_sky_generate(DuelSkyStar *out) {
    static short bins[128 * 128];
    static QVec vecs[DUEL_SKY_STAR_COUNT];
    static unsigned char cycle_id[DUEL_SKY_STAR_COUNT];
    static unsigned char cyc_val[DUEL_SKY_STAR_COUNT];
    static unsigned char jitter[DUEL_SKY_STAR_COUNT];
    unsigned seed = DUEL_SKY_SEED;
    int i;

    memset(bins, 0xFF, sizeof(bins));
    {
        int direct = 0, angular = 0, mapped = 0, star_index = 0;
        while (direct < DUEL_SKY_DIRECT_COUNT ||
               angular < DUEL_SKY_STAR_COUNT - DUEL_SKY_DIRECT_COUNT) {
            QVec v, j;
            int xy_radius, az_bin, el_bin, bin;
            double az;
            /* RandomSignedVec3: uint16 components, sign-magnitude fold. */
            v.x = (int)duel_sky_rand(&seed);
            v.y = (int)duel_sky_rand(&seed);
            v.z = (int)duel_sky_rand(&seed);
            if (v.x & 0x8000) v.x = -(v.x & 0x7FFF);
            if (v.y & 0x8000) v.y = -(v.y & 0x7FFF);
            if (v.z & 0x8000) v.z = -(v.z & 0x7FFF);
            /* Polar coords: equatorial radius + azimuth about +Y. */
            xy_radius = (int)sqrt((double)(v.x * v.x + v.y * v.y));
            az = 0.0;
            if (xy_radius != 0) {
                double c = (double)v.y / (double)xy_radius;
                if (c > 1.0) c = 1.0;
                if (c < -1.0) c = -1.0;
                az = acos(c);
                if (v.x > 0) az = -az;
            }
            /* Angular-bin reservation (azimuth wrapped, elevation folded). */
            if (az < 0.0) az += 6.283185307179586;
            az_bin = (int)(az * 20.371832);
            if (az_bin >= 128) az_bin = 127;
            if (xy_radius == 0) {
                el_bin = 127;
            } else {
                el_bin = (int)(fabs(atan((double)v.z / (double)xy_radius)) *
                               40.74366429772945);
                if (el_bin >= 128) el_bin = 127;
            }
            bin = el_bin + (az_bin << 7);
            if (xy_radius > (v.z < 0 ? -v.z : v.z)) {
                /* Equatorial draw -> angular population. */
                if (angular >= DUEL_SKY_STAR_COUNT - DUEL_SKY_DIRECT_COUNT)
                    continue;
                if (bins[bin] != -1)
                    continue;
                bins[bin] = (short)(mapped + DUEL_SKY_DIRECT_COUNT);
                v.z = 0x7FFF * v.z / xy_radius;
                v.x = 0x7FFF * v.x / xy_radius;
                v.y = 0x7FFF * v.y / xy_radius;
                j.x = (int)duel_sky_rand(&seed);
                j.y = (int)duel_sky_rand(&seed);
                j.z = (int)duel_sky_rand(&seed);
                if (j.x & 0x8000) j.x = -(j.x & 0x7FFF);
                if (j.y & 0x8000) j.y = -(j.y & 0x7FFF);
                if (j.z & 0x8000) j.z = -(j.z & 0x7FFF);
                v.x += j.x >> 4;
                v.y += j.y >> 4;
                v.z += j.z >> 4;
                vecs[mapped + DUEL_SKY_DIRECT_COUNT] = v;
                mapped++;
                angular++;
            } else if (direct < DUEL_SKY_DIRECT_COUNT) {
                /* Polar draw -> direct (cap) population, -Z half only. */
                int pz;
                if (bins[bin] != -1)
                    continue;
                if (v.z >= 0)
                    continue;
                bins[bin] = (short)star_index;
                pz = -v.z;
                v.z = -v.z;
                if (pz != 0) {
                    int sr = 0x7FFF * xy_radius / pz;
                    if (xy_radius != 0) {
                        v.x = sr * v.x / xy_radius;
                        v.y = sr * v.y / xy_radius;
                    } else {
                        v.x = 0;
                        v.y = 0;
                    }
                } else {
                    v.x = (int)duel_sky_rand(&seed) & 0x3FFF;
                    v.y = (int)duel_sky_rand(&seed) & 0x3FFF;
                }
                v.z = 0x7FFF;
                j.x = (int)duel_sky_rand(&seed);
                j.y = (int)duel_sky_rand(&seed);
                j.z = (int)duel_sky_rand(&seed);
                if (j.x & 0x8000) j.x = -(j.x & 0x7FFF);
                if (j.y & 0x8000) j.y = -(j.y & 0x7FFF);
                if (j.z & 0x8000) j.z = -(j.z & 0x7FFF);
                v.x += j.x >> 4;
                v.y += j.y >> 4;
                v.z += j.z >> 4;
                vecs[direct] = v;
                star_index++;
                direct++;
            }
        }
    }
    /* Brightness classes: cycle id + frame-0 offset + jitter. */
    for (i = 0; i < DUEL_SKY_STAR_COUNT; i++) {
        unsigned cid = duel_sky_rand(&seed) % 4u;
        unsigned off = duel_sky_rand(&seed) % CYCLE_N[cid];
        cycle_id[i] = (unsigned char)cid;
        cyc_val[i] = CYCLES[cid][off];
        jitter[i] = (unsigned char)(duel_sky_rand(&seed) & 7u);
    }
    /* Convert: XWA (x,y,z) -> DuelVR (x,z,-y), normalize. Direct stars
     * with odd index are pre-mirrored to -Z (VR adaptation, see header). */
    for (i = 0; i < DUEL_SKY_STAR_COUNT; i++) {
        double x = (double)vecs[i].x;
        double y = (double)vecs[i].y;
        double z = (double)vecs[i].z;
        double dx, dy, dz, len;
        if (i < DUEL_SKY_DIRECT_COUNT && (i & 1))
            z = -z;
        dx = x;
        dy = z;
        dz = -y;
        len = sqrt(dx * dx + dy * dy + dz * dz);
        if (!(len > 0.0)) {
            dx = 0.0;
            dy = 1.0;
            dz = 0.0;
            len = 1.0;
        }
        out[i].dir[0] = (float)(dx / len);
        out[i].dir[1] = (float)(dy / len);
        out[i].dir[2] = (float)(dz / len);
        out[i].bright =
            (float)(((double)cyc_val[i] * 4.0 + (double)jitter[i]) / 99.0);
        out[i].cycle = cycle_id[i];
    }
}
