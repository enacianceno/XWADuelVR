/* XWADuelVR sprite CPU math (P1.5/P1.7). See duel_sprite.h. */
#include "duel_sprite.h"

void duel_sprite_quad(const DuelVec3 *c, const DuelVec3 *right,
                      const DuelVec3 *up, float w, float h,
                      const DuelVec3 *n, float v_top,
                      float out[DUEL_SPR_QUAD_FLOATS]) {
    /* corners: p0=bottom-left p1=bottom-right p2=top-right p3=top-left */
    float hx = w * 0.5f, hy = h * 0.5f;
    float px[4], py[4], pz[4];
    float rx = right->x, ry = right->y, rz = right->z;
    float ux = up->x, uy = up->y, uz = up->z;
    px[0] = c->x - rx * hx - ux * hy; py[0] = c->y - ry * hx - uy * hy; pz[0] = c->z - rz * hx - uz * hy;
    px[1] = c->x + rx * hx - ux * hy; py[1] = c->y + ry * hx - uy * hy; pz[1] = c->z + rz * hx - uz * hy;
    px[2] = c->x + rx * hx + ux * hy; py[2] = c->y + ry * hx + uy * hy; pz[2] = c->z + rz * hx + uz * hy;
    px[3] = c->x - rx * hx + ux * hy; py[3] = c->y - ry * hx + uy * hy; pz[3] = c->z - rz * hx + uz * hy;
    /* uv: u=0 left, u=1 right; v_top on the top row, 1-v_top bottom */
    float uu[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
    float vv[4] = { 1.0f - v_top, 1.0f - v_top, v_top, v_top };
    /* triangles (p0,p1,p2) (p0,p2,p3); CULL_NONE so winding is cosmetic */
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    int v;
    for (v = 0; v < 6; v++) {
        int k = idx[v];
        float *d = out + v * DUEL_SPR_VERT_FLOATS;
        d[0] = px[k]; d[1] = py[k]; d[2] = pz[k];
        d[3] = n->x; d[4] = n->y; d[5] = n->z;
        d[6] = uu[k]; d[7] = vv[k];
    }
}

int duel_sprite_frame(float age, float dur, int nframes) {
    int f;
    if (nframes < 1) return 0;
    if (nframes == 1) return 0;
    if (dur <= 0.0f) return nframes - 1;
    if (age <= 0.0f) return 0;
    if (age >= dur) return nframes - 1;
    f = (int)(age / dur * (float)nframes);
    if (f < 0) f = 0;
    if (f > nframes - 1) f = nframes - 1;
    return f;
}

float duel_sprite_mix(float age, float dur) {
    float t;
    if (dur <= 0.0f) return 1.0f;
    t = age / dur;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

void duel_sprite_update(DuelSprite *s, int n, float dt) {
    int i;
    for (i = 0; i < n; i++) {
        if (!s[i].active) continue;
        s[i].age += dt;
        if (s[i].age >= s[i].dur) s[i].active = 0;
    }
}

DuelVec3 duel_equirect_dir(float u, float v) {
    float ph = v * (float)DUEL_PI;
    float th = u * 2.0f * (float)DUEL_PI;
    float sp = sinf(ph);
    return duel_v3(sp * cosf(th), cosf(ph), sp * sinf(th));
}

void duel_flare_tex(unsigned char out[DUEL_FLARE_SIZE * DUEL_FLARE_SIZE * 4]) {
    /* Two-lobe glow: tight hot core + wide faint halo. Center white,
     * edges pure black (additive-safe). Slight warm tint on the core. */
    int x, y;
    float c = (DUEL_FLARE_SIZE - 1) * 0.5f;
    for (y = 0; y < DUEL_FLARE_SIZE; y++) {
        for (x = 0; x < DUEL_FLARE_SIZE; x++) {
            float dx = ((float)x - c) / c, dy = ((float)y - c) / c;
            float r2 = dx * dx + dy * dy; /* 0 center .. 2 corner */
            float core = expf(-r2 * 22.0f);
            float halo = expf(-r2 * 4.5f) * 0.35f;
            float w = core + halo;
            if (r2 >= 1.0f) w = 0.0f; /* hard circular mask: border exactly 0 */
            unsigned char *p = out + (y * DUEL_FLARE_SIZE + x) * 4;
            if (w > 1.0f) w = 1.0f;
            p[0] = (unsigned char)(w * 255.0f + 0.5f);
            p[1] = (unsigned char)(w * 244.0f + 0.5f);
            p[2] = (unsigned char)(w * 230.0f + 0.5f);
            p[3] = 255;
        }
    }
}

float duel_flare_gain(DuelVec3 view_dir, DuelVec3 sun_dir) {
    float d = duel_v3_dot(duel_v3_norm(view_dir), duel_v3_norm(sun_dir));
    /* 1 at d=1, smooth to 0 at d=0.5 (~60 deg), 0 beyond. */
    float t;
    if (d <= 0.5f) return 0.0f;
    if (d >= 1.0f) return 1.0f;
    t = (d - 0.5f) / 0.5f;
    return t * t * (3.0f - 2.0f * t);
}

int duel_sprite_spawn(DuelSprite *s, int n, int tex_base, int nframes,
                      DuelVec3 pos, float dur,
                      float size0, float size1, float bright0, float bright1) {
    int i;
    for (i = 0; i < n; i++) {
        if (s[i].active) continue;
        s[i].active = 1;
        s[i].tex_base = tex_base;
        s[i].nframes = nframes < 1 ? 1 : nframes;
        s[i].age = 0.0f;
        s[i].dur = dur;
        s[i].size0 = size0;
        s[i].size1 = size1;
        s[i].bright0 = bright0;
        s[i].bright1 = bright1;
        s[i].pos = pos;
        return i;
    }
    return -1;
}
