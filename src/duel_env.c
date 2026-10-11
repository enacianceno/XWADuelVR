/* XWADuelVR M27: scene table (see duel_env.h). Env 0 keeps the
 * M2.2 measured constants (planet disc + flare anchor); envs 1-2 are
 * painted to match their entries (see evidence/m27-sky/mk_envs.py). */
#include "duel_env.h"

const DuelEnv DUEL_ENVS[DUEL_ENV_COUNT] = {
    /* 0: baked space planet. */
    { "space_env.png", 0.4049f, 0.4922f, 0.9613f, 0.9175f, 0.2246f },
    /* 1-2: spacespheremaps.com Blue Nebulae (LDR PNG, cooked to
     * 2048x1024 verbatim, /tmp/cook_site_envs.py). No bodies (cull
     * disabled); flare anchor = brightest pixel, measured. M29:
     * the generated Death Star looked wrong in the visor (user). */
    { "site_blue1_env.png", 0.0f, 0.0f, 2.0f, 0.2373f, 0.4707f },
    { "site_blue3_env.png", 0.0f, 0.0f, 2.0f, 0.3066f, 0.4902f },
};
