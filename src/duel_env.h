/* XWADuelVR M27: background scenes (one env spheremap per match).
 * The game ships one baked env (space planet); M27 adds a Death Star
 * and a bodiless nebula. Each env carries its body-disc cull (stars
 * sit in FRONT of the env sphere, so the disc must punch a hole) and
 * its sun direction (lens flare). cull_dot >= 2.0 = no body to cull.
 * Files live in <asset_dir>/env/ (pushed separately, like the bake). */
#ifndef XWADUELVR_ENV_H
#define XWADUELVR_ENV_H

#define DUEL_ENV_COUNT 3

typedef struct DuelEnv {
    const char *file;
    float body_u, body_v; /* disc center (equirect UV) */
    float cull_dot;       /* cos(disc radius + margin), or >= 2 = none */
    float sun_u, sun_v;   /* flare anchor (equirect UV) */
} DuelEnv;

extern const DuelEnv DUEL_ENVS[DUEL_ENV_COUNT];

/* One LCG step (XWA constants), then mod COUNT. Pure: same seed in,
 * same scene out (tests + deterministic replays). */
static inline int DuelEnv_Pick(unsigned seed) {
    seed = (9421u * seed + 1u) & 0xFFFFu;
    return (int)(seed % DUEL_ENV_COUNT);
}

#endif /* XWADUELVR_ENV_H */
