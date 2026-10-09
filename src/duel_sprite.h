/* XWADuelVR authentic sprite FX (P1.5/P1.7): CPU-side billboard math for
 * XWA/XWAUP DAT sprites (explosions, sparks, sun). SDL-free so the host
 * test suite can verify quad layout + animation timing. Rendering uses
 * the existing lit shaders on an additive pipe: black base texture,
 * sprite as emissive, ambient=dir=0 -> out = sprite.rgb * intensity.
 * Proprietary PNGs live in device storage (<asset_dir>/fx/), never here. */
#ifndef XWADUELVR_SPRITE_H
#define XWADUELVR_SPRITE_H

#include "duel_math.h"

#define DUEL_SPR_QUAD_VERTS 6
#define DUEL_SPR_VERT_FLOATS 8 /* lit layout: pos3 nrm3 uv2 */
#define DUEL_SPR_QUAD_FLOATS (DUEL_SPR_QUAD_VERTS * DUEL_SPR_VERT_FLOATS)
#define DUEL_SPR_SLOTS 8

/* Texture ids; the renderer maps these to GPU textures (NULL = missing). */
enum {
    DUEL_SPR_BOOM0 = 0,
    DUEL_SPR_BOOM1 = 1,
    DUEL_SPR_SPARK = 2,
    DUEL_SPR_SUN = 3,
    DUEL_SPR_FLARE = 4, /* M2.2: runtime procedural glow (never a PNG) */
    DUEL_SPR_TEX_COUNT = 5
};

typedef struct DuelSprite {
    int active;
    int tex_base;   /* first texture id of the frame run */
    int nframes;    /* frames in this anim (>=1) */
    float age, dur; /* seconds; one-shot, clamped */
    float size0, size1;     /* world diameter, grows size0 -> size1 */
    float bright0, bright1; /* emissive intensity envelope */
    DuelVec3 pos;   /* world center */
} DuelSprite;

/* Camera-facing quad (2 CCW triangles) centered at c, world size w x h,
 * using camera right/up basis; n = billboard facing normal (copied to all
 * verts; lighting is off on the sprite pipe but the layout needs it).
 * v_top: uv-v written on the quad's top row. Vulkan samples v=0 from the
 * first stb row = PNG top, so v_top=0.0f shows the image upright. */
void duel_sprite_quad(const DuelVec3 *c, const DuelVec3 *right,
                      const DuelVec3 *up, float w, float h,
                      const DuelVec3 *n, float v_top,
                      float out[DUEL_SPR_QUAD_FLOATS]);

/* One-shot animation: age 0..dur maps onto frames 0..nframes-1.
 * age<=0 -> 0, age>=dur -> nframes-1. nframes<1 -> 0. */
int duel_sprite_frame(float age, float dur, int nframes);

/* Normalized 0..1 age, clamped. dur<=0 -> 1. */
float duel_sprite_mix(float age, float dur);

/* M2.2 celestial: equirect (u,v) -> unit direction in the sky-sphere
 * frame (x=sin(pi v)cos(2 pi u), y=cos(pi v), z=sin(pi v)sin(2 pi u)),
 * matching build_sphere UVs. v=0 is the top pole (PNG row 0). */
DuelVec3 duel_equirect_dir(float u, float v);

#define DUEL_FLARE_SIZE 128
/* Procedural lens-flare glow texture (RGBA, black background, hot
 * center): premultiplied-safe for the additive pipe by construction. */
void duel_flare_tex(unsigned char out[DUEL_FLARE_SIZE * DUEL_FLARE_SIZE * 4]);

/* Flare visibility 0..1 from view alignment: 1 looking at the sun,
 * smooth falloff to 0 past ~60 deg off-axis. */
float duel_flare_gain(DuelVec3 view_dir, DuelVec3 sun_dir);

/* Age all slots by dt; slots reaching dur deactivate. */
void duel_sprite_update(DuelSprite *s, int n, float dt);

/* Spawn into the first inactive slot; returns slot index or -1 if full. */
int duel_sprite_spawn(DuelSprite *s, int n, int tex_base, int nframes,
                      DuelVec3 pos, float dur,
                      float size0, float size1, float bright0, float bright1);

#endif /* XWADUELVR_SPRITE_H */
