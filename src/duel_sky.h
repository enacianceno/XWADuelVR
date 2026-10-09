/* XWADuelVR P1.2: original-XWA-algorithm starfield.
 *
 * Clean-room behavior port of the XWA flight starfield distribution
 * (OpenXWA sources, xwa/flight/starfield.c, FlightStarfield_Init): the same
 * LCG, the same 128x128 angular-bin reservation, the same 512-direct +
 * 2048-angular populations, the same Q15 encodings and palette-cycle
 * brightness classes -- re-expressed in DuelVR style (float math, static
 * frame-0 brightness; twinkle arrives in P1.2b behind DUEL_XWA_STAR_TWINKLE).
 *
 * Documented divergences from the 1999 renderer (all deliberate):
 *  1. The original mirrors its 512 direct (polar-cap) stars to whichever
 *     pole faces the camera, one pole at a time. Mirroring per-view would
 *     pop in VR, so the cap is SPLIT: even direct stars stay at +Z, odd
 *     ones are pre-mirrored to -Z. Both poles stay populated, the total
 *     stays exactly 2560, the sky is static.
 *  2. Transcendentals run in double (the original mixes float/double);
 *     bin-boundary instances may differ from the 1999 binary while the
 *     distribution is identical.
 *  3. The original reseeds per flight; we use a FIXED seed so the field is
 *     identical every launch (determinism test).
 *  4. The original divides by xyRadius unguarded in the direct branch
 *     (latent div-by-zero when x==y==0, probability 2^-32); we emit the
 *     pole instead of trapping.
 *
 * Coordinate conversion (ONE explicit map, rotation, det +1):
 *  XWA (x, y, z) [poles at +-Z] -> DuelVR (x, z, -y) [poles at +-Y].
 * DuelVR frame: +X right, +Y up, -Z forward.
 *
 * Brightness: original pixel = colorStep16 * value(0..23) + jitter(0..7),
 * with colorStep16 = 0x841 in RGB565 (R1/G2/B1 per step). Normalized:
 * bright = (4*value + jitter) / 99. Neutral white; no rainbow tint.
 */
#ifndef XWADUELVR_SKY_H
#define XWADUELVR_SKY_H

#define DUEL_SKY_STAR_COUNT 2560
#define DUEL_SKY_DIRECT_COUNT 512
#define DUEL_SKY_SEED 0x1234u
#define DUEL_SKY_RADIUS 14000.0f

/* P1.2b placeholder: 0 = static frame-0 brightness (this build). */
#define DUEL_XWA_STAR_TWINKLE 0

typedef struct DuelSkyStar {
    float dir[3];    /* unit direction, DuelVR frame */
    float bright;    /* 0..1, original brightness class mapping */
    unsigned cycle;  /* 0..3, original palette-cycle id */
} DuelSkyStar;

/* Deterministic original-XWA LCG step (uint16 wrap): 9421*s + 1. */
static inline unsigned duel_sky_rand(unsigned *s) {
    *s = (9421u * (*s) + 1u) & 0xFFFFu;
    return *s;
}

/* Fill `out` with exactly DUEL_SKY_STAR_COUNT stars. Deterministic:
 * same seed => bit-identical field every call. */
void duel_sky_generate(DuelSkyStar *out);

#endif /* XWADUELVR_SKY_H */
