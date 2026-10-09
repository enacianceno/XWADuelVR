/* XWADuelVR renderer implementation. */
#include "duel_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "flight_config.h"
#include "duel_depth.h" /* M2.1: world-cell depth field (near/mid parallax) */
#include "duel_motion.h" /* M2.2: subtle vehicle inertia at seat level */
#include "duel_sky.h" /* P1.2: original-XWA-algorithm starfield */
#include "duel_sprite.h" /* P1.5/P1.7: authentic DAT sprite billboards */
#include "duel_hands.h" /* M3.0: native hand tracking (visual only) */
#include "duel_xr.h" /* P0.4: consume reference-space (recenter) events */
#include "duel_menu.h" /* M7: panel state/rows/hit (main-owned) */
#include "duel_font.h" /* M7: stroke glyphs for panel text */

/* P1.2 diagnostic sky: 0 = procedural planet/moon hidden (clean black +
 * XWA-stars evaluation; P1.3 replaces them with XWA/XWAUP artwork). */
#define DUEL_PROCEDURAL_PLANETS 0

/* M2.1 REALSPACE: 0 = authentic XWA sky only; 1 = sky + world depth
 * field. DEBUG recolors near=green/mid=magenta (never ship test ON). */
#define DUEL_SPACE_DEPTH 1
#define DUEL_REALSPACE_DEBUG 0

/* M2.2 vehicle motion feel: 0 = exact baseline camera/seat behavior
 * (offsets forced zero); 1 = subtle inertia enhancement. */
#define DUEL_VEHICLE_MOTION_FEEL 1

/* M3.0 virtual hands: 0 = no trackers, no hand draws (exact pre-M3
 * behavior); 1 = native XR_EXT_hand_tracking visualization. */
#define DUEL_VIRTUAL_HANDS 1
/* M3.5: platform hand meshes (0 = diagnostic joints even if fetchable). */
#define DUEL_HAND_MESH 1
/* M3.8: grip diamonds + tethers (0 = clean cockpit, user order). */
#define DUEL_GRAB_MARKERS 0

/* M2.2 celestial depth: 0 = MODE A (current environment); 1 = MODE B
 * (+ planetary HDR/spheremap + subtle lens flare). */
#define DUEL_CELESTIAL 1
/* M2.2c experiment (user order): 0 = static XWA star group OFF (only the
 * moving depth field remains); 1 = static stars drawn. Generator/VBO
 * untouched -- this gates the draw only, trivially reversible. */
#define DUEL_STATIC_STARS 0
/* Env-sun location in the spheremap (brightest pixel, measured): the
 * flare direction derives from this via duel_equirect_dir. */
#define DUEL_ENV_SUN_U 0.9175f
#define DUEL_ENV_SUN_V 0.2246f
#define DUEL_ENV_RADIUS 20000.0f
#define DUEL_FLARE_DIST 15000.0f
#define DUEL_FLARE_SIZE_M 1500.0f
#define DUEL_FLARE_MAX 0.35f
/* M2.2a/b: draw every DIV-th XWA star (user: sparser vs the env) +
 * cull the stars behind the planet disc (measured center/radius). The
 * 2560-star generator is untouched (oracle still green); this filters
 * the VBO. */
#define DUEL_STAR_DRAW_DIV 3 /* M2.2b: 854 of 2560 (-33% vs M2.2a) */
#define DUEL_PLANET_U 0.4049f /* M2.2b: full-disc center (wide-run fit) */
#define DUEL_PLANET_V 0.4922f
#define DUEL_PLANET_CULL_DOT 0.9613f /* cos(16 deg): disc r=10.2 + margin */

/* Seat (SHIP frame): R_y(PI) * EYE_MODEL. P0.5: derived from the LIVE
 * 49-mesh XWAUP cockpit (see evidence/p0.5-seat-frame/seat_geom_live.py),
 * NOT guessed. P0.4 sat at model (0,1.01,-0.34), "4 cm ahead of the
 * seat-back front (-0.38)" -- but -0.38 is the ASTROMECH's front face
 * (mesh24/25, Tex42 blue dome / Tex47 red ring; droid spans z -0.78..-0.37,
 * y 0.94..1.35): the pilot's face was 3 cm from the droid. Correct
 * landmarks (model frame, +Z nose): R2 front z=-0.37; seat-back zone
 * z~0.06..0.30, top y=0.92; panel rear z=0.55, top y=0.75; glareshield top
 * y=0.50; roof y=1.03. Eye model (0,0.85,+0.38): 75 cm ahead of R2, ~15 cm
 * ahead of the backrest, 17 cm behind the panel (looking OVER it), 18 cm
 * below the roof, 35 cm above the glareshield; the backrest top (0.92)
 * sits just over the eye: a reclined pilot, recenter-proof.
 * P0.5b: physical Quest test puts the P0.5 eye INSIDE the seat -- mesh45
 * (x +-0.29, y 0.55..1.00, z 0.32..0.73) is the seat back/headrest and the
 * eye (0,0.85,0.38) sits inside its box. User order: +40 cm forward.
 * Eye model (0,0.85,+0.78): 5 cm ahead of the seat-back front (0.73),
 * hovering 10 cm over the panel top (0.75), still 18 cm under the roof. */
/* M2.2b: user-ordered POV -15 cm (was y 0.85). Eye now sits level with
 * the glareshield instead of hovering over it; forward sightline stays
 * clear (panel front edge is behind-above the eye).
 * M2.2c: user-ordered POV +20 cm forward (was z -0.78). Eye model z is
 * now 0.98, 2 cm inside the windshield bound -- tight but passing. */
const DuelVec3 DUEL_SEAT_POS = { 0.0f, 0.75f, -0.88f }; /* M3.0b: POV 10cm back
 * (+Z ship) + 5cm up (+Y) per user order (was 0.70/-0.98 M2.2c). */

/* P0.2 diagnostics (build-time). M1 oracle proved submit works; it must not
 * stay in the visual scene. Cockpit diag colors identify which mesh is
 * misplaced: mesh%6 = red/green/blue/yellow/cyan/magenta, flat
 * (ambient-only), no textures. Mask bit i (1ULL<<i) enables mesh i.
 * P0.2d: diag OFF (textures restored). BLEND-material prims (canopy glass
 * Tex104/106 avg RGB 16,16,16 -- near black) are temporarily SKIPPED for the
 * cockpit because pipe_lit has no alpha blend: opaque glass = black wall.
 * Restore real transparency in step C (alpha pipeline), then set this to 0. */
#define DUEL_M1_ORACLE 0
#define DUEL_COCKPIT_DIAG_COLORS 0
#define DUEL_COCKPIT_MESH_MASK 0x3FULL
#define DUEL_SKIP_BLEND_PRIMS 1

/* ---------------- uniform layouts (must match shaders, std140) ---------------- */
typedef struct LitVSU { float mvp[16]; float model[16]; } LitVSU;
typedef struct LitFSU {
    float light_dir[3]; float ambient;
    float dir_intensity; float emissive_strength; float alpha; float pad;
} LitFSU;
typedef struct UnlitVSU { float mvp[16]; float point_size; float p0, p1, p2; } UnlitVSU;
typedef struct PlanetFSU {
    float light_dir[3]; float ambient;
    float band_a[3]; float band_freq;
    float band_b[3]; float pad;
} PlanetFSU;

/* ---------------- model ---------------- */
typedef struct DuelPrim {
    Uint32 first_index;
    Uint32 index_count;
    SDL_GPUTexture *base_tex;
    SDL_GPUTexture *emis_tex;
    int mesh_idx; /* source glTF mesh index (diag colors / mask) */
    int prim_idx; /* M3.2: primitive index within its mesh (lever subset) */
    int blend; /* material alphaMode == BLEND (glass/decals; skippable P0.2d) */
} DuelPrim;

typedef struct DuelTexEntry {
    char uri[256];
    SDL_GPUTexture *tex;
} DuelTexEntry;

typedef struct DuelModel {
    SDL_GPUBuffer *vbo;
    SDL_GPUBuffer *ibo;
    DuelPrim *prims;
    int prim_count;
    DuelTexEntry *texs;
    int tex_count;
    int mesh_count;
} DuelModel;

#define MAX_PARTICLES 384 /* staged booms + sparks + muzzle pops */
typedef struct DuelParticle {
    int active;
    DuelVec3 pos, vel;
    float life, max_life;
    float r, g, b;
} DuelParticle;

struct DuelRender {
    SDL_GPUDevice *device;
    char asset_dir[512];
    /* pipelines */
    SDL_GPUShader *lit_vs, *lit_fs, *unlit_vs, *unlit_fs, *planet_vs, *planet_fs;
    SDL_GPUShader *cube_vs, *cube_fs;
    SDL_GPUGraphicsPipeline *pipe_lit, *pipe_planet;
    SDL_GPUGraphicsPipeline *pipe_points_test, *pipe_points_add;
    SDL_GPUGraphicsPipeline *pipe_tri_add, *pipe_lines, *pipe_tri_alpha;
    SDL_GPUGraphicsPipeline *pipe_lines_test; /* M3.0: hand bones, depth-tested
     * (pipe_lines has no depth test: fine for the HUD reticle, wrong for
     * hands that must hide behind the dashboard). */
    SDL_GPUGraphicsPipeline *pipe_tri_test; /* M3.3: solid depth-tested tris
     * (throttle quadrant handle: occludes + occluded, writes depth). */
    SDL_GPUGraphicsPipeline *pipe_cube;
    SDL_GPUGraphicsPipeline *pipe_sprite; /* P1.5: lit shaders, ADD blend */
    SDL_GPUBuffer *spr_vbo; /* billboard quads, lit layout, rebuilt per eye */
    SDL_GPUTransferBuffer *spr_tbuf;
    SDL_GPUTexture *spr_tex[DUEL_SPR_TEX_COUNT]; /* fx PNGs, NULL if missing */
    DuelSprite sprites[DUEL_SPR_SLOTS]; /* one-shot anims (booms/sparks) */
    int spr_logged;
    SDL_GPUTexture *env_tex; /* M2.2: planetary spheremap, NULL if missing */
    /* M2.1: depth field collected once per frame (ship pos, never HMD). */
    DuelDepthP depth_near[DUEL_DEPTH_NEAR_CAP];
    DuelDepthP depth_mid[DUEL_DEPTH_MID_CAP];
    int depth_nn, depth_nm;
    float rs_timer;
    /* M2.2: vehicle inertia state (seat-level; HMD untouched). */
    DuelMotion motion;
    int mo_last_phase;
    float mo_timer;
    /* M3.0: hand tracker bitmask (1=L 2=R) + telemetry throttle. */
    int hands_mask;
    float hands_timer;
    /* M3.1: per-hand lever grab state + telemetry throttle.
     * M3.6 free grab: one machine per (hand, target); winners in
     * grab_holder[target] (-1 = none). */
    DuelGrab grab[2][2];
    int grab_holder[2];
    float grab_timer;
    /* M3.8: last grip readings per hand (band tuning telemetry). */
    float grab_fist[2], grab_pinch[2];
    /* M3.8c: winner distance per target (latch-radius validation). */
    float grab_wdist[2];
    /* M3.2: last pinch-midpoint per side in ship frame (hand throttle). */
    DuelVec3 grab_mid[2];
    int grab_mid_valid[2];
    /* M3.3: stick deflection (pd>0 back/nose-up, rd>0 right/yaw-right
     * since M6: pure yaw, roll moved to the left lever); tracks the
     * holder hand while held, spring-decays on release. */
    float defl_pd, defl_rd;
    /* M7: hand-lever sensitivity multiplier (menu-owned, 1.0 = the
     * frozen feel; applied at publish, clamped to unit). */
    float hand_sens;
    /* M7 menu panel (world-locked, placed once per screen by main). */
    int menu_show;
    int menu_hide_hud; /* menu/result: no cue/diamond/lead/bars */
    DuelVec3 menu_center; /* LOCAL anchor (see SetMenuPanel) */
    DuelQuat menu_quat; /* LOCAL; duel_q_forward points INTO the screen */
    const DuelMenu *menu; /* main-owned, valid for the run */
    int menu_hover;     /* hand-ray row (-1 none), eye 0+1 agree */
    float menu_hx, menu_hy; /* hover point, panel-local */
    int menu_pinch[2];  /* per-side pinch level (hysteresis) */
    DuelVec3 menu_ray_o[2], menu_ray_e[2]; /* ray visuals */
    int menu_ray_on[2];
    /* M3.4: right-hand trigger state (index extension while HELD).
     * M3.7: one fire machine per hand (either thumb fires the stick). */
    DuelFire fire_r, fire_l;
    /* M3.7: ship-frame palm per hand (thumb-independent stick anchor)
     * + the latched grab anchor driving relative deflection. */
    DuelVec3 grab_palm[2];
    int grab_palm_valid[2];
    DuelVec3 stick_anchor[2];
    int stick_anchor_valid[2];
    /* M6: left-lever roll deflection (latch-relative lateral, rd>0 =
     * push right = bank right) + its own latch anchor per side.
     * Spring-decays like the stick; throttle stays absolute/holding. */
    float defl_ld;
    DuelVec3 lever_anchor[2];
    int lever_anchor_valid[2];
    /* M3.5: platform hand meshes, skinned CPU-side per eye (joints stay
     * as the fallback when a runtime lacks the FB mesh). */
    int hand_mesh[2];
    /* M5 hand-pop fix: the platform mesh used to draw on bare isActive,
     * so a 1-2 frame partial track (hand entering the frustum) skinned
     * garbage joints and flashed a hand above the view. Now a side must
     * hold palm+wrist valid for 3 straight frames before it appears;
     * it still vanishes the instant validity breaks. */
    int hand_show[2];
    int hand_ok[2];
    int hand_nv[2], hand_ni[2], hand_vbase[2], hand_ibase[2];
    SDL_GPUBuffer *hand_vbo, *hand_ibo;
    SDL_GPUTransferBuffer *hand_tbuf;
    float *hand_stage; /* (nv0+nv1)*9 floats */
    SDL_GPUSampler *sampler;
    SDL_GPUTexture *white_tex, *black_tex;
    SDL_GPUTexture *diag_tex[6]; /* P0.2 mesh identity colors */
    DuelModel tie, cockpit, xwing; /* M5: xwing = ally exterior (optional) */
    SDL_GPUBuffer *cube_vbo, *cube_ibo;
    SDL_GPUTransferBuffer *dyn_tbuf; /* persistent dyn upload (no per-frame alloc) */
    /* sky */
    SDL_GPUBuffer *stars_vbo;
    int star_count;
    SDL_GPUBuffer *sphere_vbo, *sphere_ibo;
    Uint32 sphere_index_count;
    /* dynamic */
    SDL_GPUBuffer *dyn_vbo; /* lasers+lines+hud rebuilt per eye */
    /* fx state */
    DuelParticle parts[MAX_PARTICLES];
    float last_hp_player, last_hp_tie[DUEL_TIE_COUNT], last_hp_ally[DUEL_ALLY_COUNT];
    int boom_player, boom_tie[DUEL_TIE_COUNT], boom_ally[DUEL_ALLY_COUNT]; /* explosion spawned */
    float flash;
    float shake;        /* impact camera shake 0..1 (decays in Update) */
    unsigned shake_seed;
    int last_muzzle_p, last_muzzle_t[DUEL_TIE_COUNT], last_muzzle_a[DUEL_ALLY_COUNT]; /* muzzle-flash polling */
    /* P0.4 seat calibration (neutral HMD reference; ship/cockpit untouched) */
    DuelSeatCal cal;
    int cal_settle; /* valid-view frames since boot (settle before capture) */
    int last_phase; /* combat-reset transition detection */
    int cal_space;  /* a space-change recapture is pending (log reason) */
    /* P0.5 §21: last neutralized head pose (for DUEL_CTRL telemetry) */
    DuelVec3 last_corr_p;
    float last_corr_yaw;
    /* P1.2: last world eye pose (audio listener) */
    DuelQuat last_eq;
    DuelVec3 last_ep;
};

/* ---------------- helpers ---------------- */
static void *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (out_len) *out_len = buf ? (size_t)n : 0;
    return buf;
}

static SDL_GPUShader *load_shader(DuelRender *r, const char *name,
                                  SDL_GPUShaderStage stage,
                                  Uint32 num_samplers, Uint32 num_uniforms) {
    char path[640];
    snprintf(path, sizeof path, "%s/../shaders/%s", r->asset_dir, name);
    size_t len = 0;
    void *code = read_file(path, &len);
    if (!code) { SDL_Log("DUEL missing shader %s", path); return NULL; }
    SDL_GPUShaderCreateInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.code_size = len;
    ci.code = (const Uint8 *)code;
    ci.entrypoint = "main";
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    ci.stage = stage;
    ci.num_samplers = num_samplers;
    ci.num_uniform_buffers = num_uniforms;
    SDL_GPUShader *sh = SDL_CreateGPUShader(r->device, &ci);
    free(code);
    if (!sh) SDL_Log("DUEL shader create failed %s: %s", name, SDL_GetError());
    return sh;
}

static SDL_GPUBuffer *make_buffer(DuelRender *r, SDL_GPUBufferUsageFlags usage,
                                  Uint32 size, const void *data) {
    SDL_GPUBufferCreateInfo bi;
    memset(&bi, 0, sizeof bi);
    bi.usage = usage;
    bi.size = size;
    SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(r->device, &bi);
    if (!buf || !data) return buf;
    SDL_GPUTransferBufferCreateInfo ti;
    memset(&ti, 0, sizeof ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = size;
    SDL_GPUTransferBuffer *tbuf = SDL_CreateGPUTransferBuffer(r->device, &ti);
    if (!tbuf) return buf;
    void *dst = SDL_MapGPUTransferBuffer(r->device, tbuf, false);
    if (dst) {
        memcpy(dst, data, size);
        SDL_UnmapGPUTransferBuffer(r->device, tbuf);
    }
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (cmd) {
        SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src;
        memset(&src, 0, sizeof src);
        src.transfer_buffer = tbuf;
        SDL_GPUBufferRegion dstreg;
        memset(&dstreg, 0, sizeof dstreg);
        dstreg.buffer = buf;
        dstreg.size = size;
        SDL_UploadToGPUBuffer(pass, &src, &dstreg, false);
        SDL_EndGPUCopyPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
    }
    SDL_ReleaseGPUTransferBuffer(r->device, tbuf);
    return buf;
}

static SDL_GPUTexture *make_texture(DuelRender *r, int w, int h, const void *rgba, int srgb) {
    SDL_GPUTextureCreateInfo ti;
    memset(&ti, 0, sizeof ti);
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
                     : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.width = (Uint32)w;
    ti.height = (Uint32)h;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(r->device, &ti);
    if (!tex || !rgba) return tex;
    Uint32 size = (Uint32)(w * h * 4);
    SDL_GPUTransferBufferCreateInfo bi;
    memset(&bi, 0, sizeof bi);
    bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    bi.size = size;
    SDL_GPUTransferBuffer *tbuf = SDL_CreateGPUTransferBuffer(r->device, &bi);
    if (!tbuf) return tex;
    void *dst = SDL_MapGPUTransferBuffer(r->device, tbuf, false);
    if (dst) {
        memcpy(dst, rgba, size);
        SDL_UnmapGPUTransferBuffer(r->device, tbuf);
    }
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (cmd) {
        SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src;
        memset(&src, 0, sizeof src);
        src.transfer_buffer = tbuf;
        SDL_GPUTextureRegion dstreg;
        memset(&dstreg, 0, sizeof dstreg);
        dstreg.texture = tex;
        dstreg.w = (Uint32)w;
        dstreg.h = (Uint32)h;
        dstreg.d = 1;
        SDL_UploadToGPUTexture(pass, &src, &dstreg, false);
        SDL_EndGPUCopyPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
    }
    SDL_ReleaseGPUTransferBuffer(r->device, tbuf);
    return tex;
}

/* ---------------- pipelines ---------------- */
/* layout: 0 = unlit (pos3+col4+uv2), 1 = lit (pos3+nrm3+uv2),
 * 2 = M1 cube (pos3+ubyte4n). M1 known-good: LESS_OR_EQUAL, D24. */
static SDL_GPUGraphicsPipeline *make_pipeline(
    DuelRender *r, SDL_GPUShader *vs, SDL_GPUShader *fs,
    SDL_GPUPrimitiveType prim, int depth_test, int depth_write,
    int blend_add, int blend_alpha, int lines,
    SDL_GPUTextureFormat swap_fmt, int layout) {
    int lit_layout = layout == 1;
    int m1_layout = layout == 2;
    SDL_GPUVertexBufferDescription bindings[1];
    memset(bindings, 0, sizeof bindings);
    bindings[0].slot = 0;
    bindings[0].pitch = m1_layout ? 16 : (lit_layout ? 32 : 36);
    bindings[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[3];
    memset(attrs, 0, sizeof attrs);
    attrs[0].buffer_slot = 0; attrs[0].location = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
    attrs[1].buffer_slot = 0; attrs[1].location = 1;
    if (lit_layout) {
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[1].offset = 12;
        attrs[2].buffer_slot = 0; attrs[2].location = 2;
        attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[2].offset = 24;
    } else if (m1_layout) {
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attrs[1].offset = 12;
    } else {
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[1].offset = 12;
        attrs[2].buffer_slot = 0; attrs[2].location = 2;
        attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[2].offset = 28;
    }
    (void)lines;
    SDL_GPUColorTargetBlendState blend;
    memset(&blend, 0, sizeof blend);
    if (blend_add) {
        blend.enable_blend = true;
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    } else if (blend_alpha) {
        blend.enable_blend = true;
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }
    SDL_GPUColorTargetDescription target;
    memset(&target, 0, sizeof target);
    target.format = swap_fmt;
    target.blend_state = blend;
    SDL_GPUGraphicsPipelineCreateInfo pi;
    memset(&pi, 0, sizeof pi);
    pi.vertex_shader = vs;
    pi.fragment_shader = fs;
    pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_buffer_descriptions = bindings;
    pi.vertex_input_state.num_vertex_attributes = m1_layout ? 2 : 3;
    pi.vertex_input_state.vertex_attributes = attrs;
    pi.primitive_type = prim;
    pi.rasterizer_state.cull_mode = m1_layout ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.front_face = m1_layout ? SDL_GPU_FRONTFACE_CLOCKWISE
                                               : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pi.depth_stencil_state.enable_depth_test = depth_test ? true : false;
    pi.depth_stencil_state.enable_depth_write = depth_write ? true : false;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pi.target_info.num_color_targets = 1;
    pi.target_info.color_target_descriptions = &target;
    pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    pi.target_info.has_depth_stencil_target = true;
    return SDL_CreateGPUGraphicsPipeline(r->device, &pi);
}

/* ---------------- model loading (cgltf) ---------------- */
typedef struct BakeCtx {
    float *verts;      /* lit layout: pos3 norm3 uv2 */
    Uint32 *indices;
    Uint32 vcount, icount, vcap, icap;
    DuelModel *model;
    DuelRender *r;
    char dir[512];
    cgltf_data *data;
} BakeCtx;

static void bake_node(BakeCtx *ctx, cgltf_node *node, const float parent[16]);

static void xform_point(const float m[16], const float in[3], float out[3]) {
    out[0] = m[0] * in[0] + m[4] * in[1] + m[8] * in[2] + m[12];
    out[1] = m[1] * in[0] + m[5] * in[1] + m[9] * in[2] + m[13];
    out[2] = m[2] * in[0] + m[6] * in[1] + m[10] * in[2] + m[14];
}

static void xform_dir(const float m[16], const float in[3], float out[3]) {
    out[0] = m[0] * in[0] + m[4] * in[1] + m[8] * in[2];
    out[1] = m[1] * in[0] + m[5] * in[1] + m[9] * in[2];
    out[2] = m[2] * in[0] + m[6] * in[1] + m[10] * in[2];
    float l = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (l > 1e-9f) { out[0] /= l; out[1] /= l; out[2] /= l; }
}

static int acc_index(cgltf_accessor *acc, cgltf_size i, Uint32 *out) {
    if (acc->component_type == cgltf_component_type_r_16u) {
        const Uint8 *base = (const Uint8 *)acc->buffer_view->buffer->data +
            acc->buffer_view->offset + acc->offset;
        size_t stride = acc->buffer_view->stride ? acc->buffer_view->stride : 2;
        Uint16 v;
        memcpy(&v, base + i * stride, 2);
        *out = v;
        return 1;
    }
    if (acc->component_type == cgltf_component_type_r_32u) {
        const Uint8 *base = (const Uint8 *)acc->buffer_view->buffer->data +
            acc->buffer_view->offset + acc->offset;
        size_t stride = acc->buffer_view->stride ? acc->buffer_view->stride : 4;
        Uint32 v;
        memcpy(&v, base + i * stride, 4);
        *out = v;
        return 1;
    }
    return 0;
}

static int acc_vec(cgltf_accessor *acc, cgltf_size i, float out[4]) {
    if (acc->component_type != cgltf_component_type_r_32f) return 0;
    int n = (int)acc->type;
    const Uint8 *base = (const Uint8 *)acc->buffer_view->buffer->data +
        acc->buffer_view->offset + acc->offset;
    size_t stride = acc->buffer_view->stride ? acc->buffer_view->stride : (size_t)(n * 4);
    memcpy(out, base + i * stride, (size_t)(n * 4));
    return 1;
}

static SDL_GPUTexture *load_tex_uri(BakeCtx *ctx, const char *uri, int emissive) {
    DuelModel *m = ctx->model;
    for (int i = 0; i < m->tex_count; i++)
        if (strcmp(m->texs[i].uri, uri) == 0) return m->texs[i].tex;
    char path[768];
    snprintf(path, sizeof path, "%s/%s", ctx->dir, uri);
    int w, h, comp;
    stbi_uc *px = stbi_load(path, &w, &h, &comp, 4);
    if (!px) { SDL_Log("DUEL tex missing %s", path); return NULL; }
    SDL_GPUTexture *tex = make_texture(ctx->r, w, h, px, 1);
    stbi_image_free(px);
    (void)emissive;
    m->texs = (DuelTexEntry *)realloc(m->texs, (size_t)(m->tex_count + 1) * sizeof(DuelTexEntry));
    snprintf(m->texs[m->tex_count].uri, sizeof m->texs[0].uri, "%s", uri);
    m->texs[m->tex_count].tex = tex;
    m->tex_count++;
    return tex;
}

static void bake_prim(BakeCtx *ctx, cgltf_primitive *prim, const float world[16], int mesh_idx,
                      int prim_idx) {
    cgltf_accessor *pa = NULL, *na = NULL, *ua = NULL;
    for (cgltf_size i = 0; i < prim->attributes_count; i++) {
        if (prim->attributes[i].type == cgltf_attribute_type_position) pa = prim->attributes[i].data;
        if (prim->attributes[i].type == cgltf_attribute_type_normal) na = prim->attributes[i].data;
        if (prim->attributes[i].type == cgltf_attribute_type_texcoord) ua = prim->attributes[i].data;
    }
    if (!pa || !prim->indices) return;
    cgltf_size nv = pa->count, ni = prim->indices->count;
    if (ctx->vcount + nv > ctx->vcap) {
        ctx->vcap = ctx->vcount + (Uint32)nv + 1024;
        ctx->verts = (float *)realloc(ctx->verts, (size_t)ctx->vcap * 8 * sizeof(float));
    }
    if (ctx->icount + ni > ctx->icap) {
        ctx->icap = ctx->icount + (Uint32)ni + 4096;
        ctx->indices = (Uint32 *)realloc(ctx->indices, (size_t)ctx->icap * sizeof(Uint32));
    }
    if (!ctx->verts || !ctx->indices) return;
    Uint32 base = ctx->vcount;
    for (cgltf_size i = 0; i < nv; i++) {
        float p[4] = { 0, 0, 0, 1 }, n[4] = { 0, 1, 0, 0 }, t[4] = { 0, 0, 0, 0 };
        acc_vec(pa, i, p);
        if (na) acc_vec(na, i, n);
        if (ua) acc_vec(ua, i, t);
        float pw[3], nw[3];
        xform_point(world, p, pw);
        xform_dir(world, n, nw);
        float *d = ctx->verts + (size_t)(base + (Uint32)i) * 8;
        d[0] = pw[0]; d[1] = pw[1]; d[2] = pw[2];
        d[3] = nw[0]; d[4] = nw[1]; d[5] = nw[2];
        d[6] = t[0]; d[7] = t[1];
    }
    for (cgltf_size i = 0; i < ni; i++) {
        Uint32 idx = 0;
        acc_index(prim->indices, i, &idx);
        ctx->indices[ctx->icount + (Uint32)i] = base + idx;
    }
    /* material textures (default material; variants ignored P0) */
    SDL_GPUTexture *btex = NULL, *etex = NULL;
    if (prim->material) {
        if (prim->material->pbr_metallic_roughness.base_color_texture.texture &&
            prim->material->pbr_metallic_roughness.base_color_texture.texture->image &&
            prim->material->pbr_metallic_roughness.base_color_texture.texture->image->uri)
            btex = load_tex_uri(
                ctx,
                prim->material->pbr_metallic_roughness.base_color_texture.texture->image->uri, 0);
        if (prim->material->emissive_texture.texture &&
            prim->material->emissive_texture.texture->image &&
            prim->material->emissive_texture.texture->image->uri)
            etex = load_tex_uri(ctx, prim->material->emissive_texture.texture->image->uri, 1);
    }
    DuelModel *m = ctx->model;
    m->prims = (DuelPrim *)realloc(m->prims, (size_t)(m->prim_count + 1) * sizeof(DuelPrim));
    m->prims[m->prim_count].first_index = ctx->icount;
    m->prims[m->prim_count].index_count = (Uint32)ni;
    m->prims[m->prim_count].base_tex = btex;
    m->prims[m->prim_count].emis_tex = etex;
    m->prims[m->prim_count].mesh_idx = mesh_idx;
    m->prims[m->prim_count].prim_idx = prim_idx;
    m->prims[m->prim_count].blend =
        (prim->material && prim->material->alpha_mode == cgltf_alpha_mode_blend) ? 1 : 0;
    m->prim_count++;
    ctx->vcount += (Uint32)nv;
    ctx->icount += (Uint32)ni;
}

static void bake_node(BakeCtx *ctx, cgltf_node *node, const float parent[16]) {
    float local[16], world[16];
    cgltf_node_transform_local(node, local);
    /* world = parent * local (column-major) */
    for (int c = 0; c < 4; c++)
        for (int r2 = 0; r2 < 4; r2++)
            world[c * 4 + r2] = parent[r2] * local[c * 4] + parent[4 + r2] * local[c * 4 + 1] +
                                parent[8 + r2] * local[c * 4 + 2] + parent[12 + r2] * local[c * 4 + 3];
    if (node->mesh) {
        int mesh_idx = ctx->data ? (int)(node->mesh - ctx->data->meshes) : 0;
        for (cgltf_size i = 0; i < node->mesh->primitives_count; i++)
            bake_prim(ctx, &node->mesh->primitives[i], world, mesh_idx, (int)i);
    }
    for (cgltf_size i = 0; i < node->children_count; i++)
        bake_node(ctx, node->children[i], world);
}

static int load_model(DuelRender *r, DuelModel *m, const char *subdir) {
    memset(m, 0, sizeof(*m));
    char dir[512], path[640];
    snprintf(dir, sizeof dir, "%s/%s", r->asset_dir, subdir);
    snprintf(path, sizeof path, "%s/%s.gltf", dir, subdir);
    cgltf_options opt;
    memset(&opt, 0, sizeof opt);
    cgltf_data *data = NULL;
    if (cgltf_parse_file(&opt, path, &data) != cgltf_result_success) {
        SDL_Log("DUEL gltf parse failed %s", path);
        return 0;
    }
    char bin[640];
    snprintf(bin, sizeof bin, "%s/%s.bin", dir, subdir);
    size_t bin_len = 0;
    void *bin_data = read_file(bin, &bin_len);
    if (!bin_data) {
        SDL_Log("DUEL bin missing %s", bin);
        cgltf_free(data);
        return 0;
    }
    /* point the buffer at our memory (single buffer expected) */
    if (data->buffers_count > 0) {
        data->buffers[0].data = bin_data;
        data->buffers[0].size = bin_len;
    }
    BakeCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.model = m;
    ctx.r = r;
    ctx.data = data;
    snprintf(ctx.dir, sizeof ctx.dir, "%s", dir);
    float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    if (data->scene) {
        for (cgltf_size i = 0; i < data->scene->nodes_count; i++)
            bake_node(&ctx, data->scene->nodes[i], ident);
    }
    m->mesh_count = (int)data->meshes_count;
    /* P0.2c: per-mesh prim histogram + range validation. A mesh with 0
     * prims here means it can never render (bake skipped it or its
     * mesh_idx is out of range and masked out). */
    {
        /* P0.2d: 64 slots (live cockpit has 49 meshes; the 32-cap flagged
         * meshes 32..48 as bad and over-read the stack when printing). */
        int hist[64];
        memset(hist, 0, sizeof hist);
        int bad = 0, nblend = 0;
        for (int i = 0; i < m->prim_count; i++) {
            int mi = m->prims[i].mesh_idx;
            if (mi < 0 || mi >= m->mesh_count || mi >= 64) { bad++; continue; }
            hist[mi]++;
            nblend += m->prims[i].blend;
        }
        char hb[320];
        int n = 0;
        n += snprintf(hb + n, sizeof(hb) - (size_t)n, "[");
        for (int k = 0; k < m->mesh_count && k < 64 && n < (int)sizeof(hb) - 8; k++)
            n += snprintf(hb + n, sizeof(hb) - (size_t)n, "%s%d", k ? "," : "", hist[k]);
        snprintf(hb + n, sizeof(hb) - (size_t)n, "]");
        SDL_Log("DUEL model %s: %u verts %u idx %d prims %d texs %d meshes meshprims=%s badidx=%d blendprims=%d",
                subdir, ctx.vcount, ctx.icount, m->prim_count, m->tex_count,
                m->mesh_count, hb, bad, nblend);
    }
    /* P0.4 seat calibration audit (§9): bounds + instrument centroid from
     * the baked verts of the LIVE asset, eye from DUEL_SEAT_POS, and the
     * §8 inside-checks. A recooked asset that breaks the seat screams here
     * instead of silently displacing the pilot. */
    if (strcmp(subdir, "XwingCockpit") == 0 && ctx.vcount > 0 && ctx.icount > 0) {
        float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
        for (Uint32 i = 0; i < ctx.vcount; i++) {
            const float *d = ctx.verts + (size_t)i * 8;
            for (int a = 0; a < 3; a++) {
                if (d[a] < mn[a]) mn[a] = d[a];
                if (d[a] > mx[a]) mx[a] = d[a];
            }
        }
        double cx = 0, cy = 0, cz = 0;
        Uint32 cn = 0;
        for (int i = 0; i < m->prim_count; i++) {
            int mi = m->prims[i].mesh_idx;
            if (mi < 40 || mi > 48) continue; /* dashboard meshes */
            for (Uint32 k = 0; k < m->prims[i].index_count; k++) {
                Uint32 vi = ctx.indices[m->prims[i].first_index + k];
                if (vi >= ctx.vcount) continue;
                const float *d = ctx.verts + (size_t)vi * 8;
                cx += d[0]; cy += d[1]; cz += d[2]; cn++;
            }
        }
        DuelVec3 eye = duel_q_rot(duel_q_opt2ship(), DUEL_SEAT_POS);
        /* P0.5b landmarks (model frame, +Z nose): the eye sits OVER the
         * panel (top y=0.75), so fore-aft is bounded by the seat-back
         * front (mesh45 z=0.73) behind and the windshield zone ahead.
         * Roof y=1.03. P0.5's eye passed PRCOX yet sat inside mesh45. */
        int ok_seat = eye.z > 0.74f, ok_shield = eye.z < 1.00f;
        int ok_roof = eye.y < 0.95f, ok_over = eye.y > 0.63f; /* M2.2b: POV -15cm */
        int ok_center = eye.x > -0.10f && eye.x < 0.10f;
        SDL_Log("DUEL_SEAT_CAL eye=(%.2f,%.2f,%.2f) bounds=x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f] inst=(%.2f,%.2f,%.2f)n=%u checks=%s%s%s%s%s",
                eye.x, eye.y, eye.z, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2],
                cn ? (float)(cx / cn) : 0.0f, cn ? (float)(cy / cn) : 0.0f,
                cn ? (float)(cz / cn) : 0.0f, cn,
                ok_seat ? "S" : "s", ok_shield ? "W" : "w", ok_roof ? "C" : "c",
                ok_over ? "O" : "o", ok_center ? "X" : "x");
    }
    if (ctx.vcount && ctx.icount) {
        m->vbo = make_buffer(r, SDL_GPU_BUFFERUSAGE_VERTEX,
                             ctx.vcount * 8 * sizeof(float), ctx.verts);
        m->ibo = make_buffer(r, SDL_GPU_BUFFERUSAGE_INDEX,
                             ctx.icount * sizeof(Uint32), ctx.indices);
    }
    free(ctx.verts);
    free(ctx.indices);
    data->buffers[0].data = NULL; /* we own bin_data */
    cgltf_free(data);
    free(bin_data);
    return m->vbo && m->ibo;
}

/* ---------------- M1 known-good cube (R0 oracle) ----------------
 * Verbatim port of XWAQuest M1 (app/src/main/cpp/main.c): same math, same
 * winding, same shader bytecode (shaders/m1_cube_*.spv, SDL zlib license).
 * Drawn at LOCAL (0,0,-2): visible iff submit+swapchain+views work. */
typedef struct M1Mat4 { float m[16]; } M1Mat4;

static M1Mat4 m1_mul(M1Mat4 a, M1Mat4 b) {
    M1Mat4 r = { { 0 } };
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                r.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
    return r;
}

static M1Mat4 m1_view_from_pose(const XrPosef *pose) {
    float x = pose->orientation.x, y = pose->orientation.y;
    float z = pose->orientation.z, w = pose->orientation.w;
    float rx = 1 - 2 * (y * y + z * z), ry = 2 * (x * y + w * z), rz = 2 * (x * z - w * y);
    float ux = 2 * (x * y - w * z), uy = 1 - 2 * (x * x + z * z), uz = 2 * (y * z + w * x);
    float fx = 2 * (x * z + w * y), fy = 2 * (y * z - w * x), fz = 1 - 2 * (x * x + y * y);
    float px = pose->position.x, py = pose->position.y, pz = pose->position.z;
    float dr = -(rx * px + ry * py + rz * pz);
    float du = -(ux * px + uy * py + uz * pz);
    float df = -(fx * px + fy * py + fz * pz);
    M1Mat4 m = { { rx, ux, fx, 0, ry, uy, fy, 0, rz, uz, fz, 0, dr, du, df, 1 } };
    return m;
}

static M1Mat4 m1_proj(const XrFovf *fov, float nearZ, float farZ) {
    float tL = tanf(fov->angleLeft), tR = tanf(fov->angleRight);
    float tU = tanf(fov->angleUp), tD = tanf(fov->angleDown);
    float w = tR - tL, h = tU - tD;
    M1Mat4 m = { { 2 / w, 0, 0, 0,
                   0, 2 / h, 0, 0,
                   (tR + tL) / w, (tU + tD) / h, -farZ / (farZ - nearZ), -1,
                   0, 0, -(farZ * nearZ) / (farZ - nearZ), 0 } };
    return m;
}

typedef struct M1Vert { float x, y, z; Uint8 r, g, b, a; } M1Vert;

static void build_m1_cube(DuelRender *r) {
    float s = 0.25f;
    M1Vert v[24] = {
        {-s,-s,-s, 255,0,0,255}, {s,-s,-s, 255,0,0,255}, {s,s,-s, 255,0,0,255}, {-s,s,-s, 255,0,0,255},
        {s,-s,s, 0,255,0,255}, {-s,-s,s, 0,255,0,255}, {-s,s,s, 0,255,0,255}, {s,s,s, 0,255,0,255},
        {-s,-s,s, 0,0,255,255}, {-s,-s,-s, 0,0,255,255}, {-s,s,-s, 0,0,255,255}, {-s,s,s, 0,0,255,255},
        {s,-s,-s, 255,255,0,255}, {s,-s,s, 255,255,0,255}, {s,s,s, 255,255,0,255}, {s,s,-s, 255,255,0,255},
        {-s,s,-s, 255,0,255,255}, {s,s,-s, 255,0,255,255}, {s,s,s, 255,0,255,255}, {-s,s,s, 255,0,255,255},
        {-s,-s,s, 0,255,255,255}, {s,-s,s, 0,255,255,255}, {s,-s,-s, 0,255,255,255}, {-s,-s,-s, 0,255,255,255}
    };
    Uint16 ix[36] = {
        0,1,2, 0,2,3, 4,5,6, 4,6,7, 8,9,10, 8,10,11,
        12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23
    };
    r->cube_vbo = make_buffer(r, SDL_GPU_BUFFERUSAGE_VERTEX, sizeof v, v);
    r->cube_ibo = make_buffer(r, SDL_GPU_BUFFERUSAGE_INDEX, sizeof ix, ix);
}

/* ---------------- sky + sphere ---------------- */
static unsigned duel_lcg(unsigned *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

static void build_stars(DuelRender *r) {
    /* unlit layout: pos3 color4 uv2 = 9 floats. P1.2: original-XWA
     * distribution (duel_sky); neutral white, static frame-0 brightness.
     * M2.2a: deterministic 1/DIV decimation + planet-disc cull (env only).
     * NOTE: call AFTER the env cook load (r->env_tex gates the cull). */
    const int n = DUEL_SKY_STAR_COUNT;
    float *v = (float *)malloc((size_t)n * 9 * sizeof(float));
    DuelSkyStar *sky = (DuelSkyStar *)malloc((size_t)n * sizeof(*sky));
    DuelVec3 pdir = duel_equirect_dir(DUEL_PLANET_U, DUEL_PLANET_V);
    int kept = 0, culled = 0;
    duel_sky_generate(sky);
    for (int i = 0; i < n; i++) {
        float *d;
        float b;
        if (i % DUEL_STAR_DRAW_DIV != 0) continue;
        if (r->env_tex) {
            float dot = sky[i].dir[0] * pdir.x + sky[i].dir[1] * pdir.y +
                        sky[i].dir[2] * pdir.z;
            if (dot > DUEL_PLANET_CULL_DOT) { culled++; continue; }
        }
        d = v + (size_t)kept * 9;
        b = sky[i].bright;
        d[0] = sky[i].dir[0] * DUEL_SKY_RADIUS;
        d[1] = sky[i].dir[1] * DUEL_SKY_RADIUS;
        d[2] = sky[i].dir[2] * DUEL_SKY_RADIUS;
        d[3] = b; d[4] = b; d[5] = b;
        d[6] = 1.0f;
        d[7] = 0.0f; d[8] = 0.0f;
        kept++;
    }
    r->stars_vbo = make_buffer(r, SDL_GPU_BUFFERUSAGE_VERTEX, (Uint32)kept * 9 * sizeof(float), v);
    r->star_count = kept;
    free(sky);
    free(v);
    SDL_Log("DUEL_SKY mode=XWA_ORIGINAL gen=%d drawn=%d culled_planet=%d twinkle=%d draws=1 background=BLACK",
            n, kept, culled, DUEL_XWA_STAR_TWINKLE);
}

static void build_sphere(DuelRender *r) {
    /* lit layout: pos3 norm3 uv2 */
    const int lon = 48, lat = 32;
    int nv = (lon + 1) * (lat + 1);
    float *v = (float *)malloc((size_t)nv * 8 * sizeof(float));
    for (int j = 0; j <= lat; j++) {
        float ph = (float)j / (float)lat * (float)DUEL_PI;
        for (int i = 0; i <= lon; i++) {
            float th = (float)i / (float)lon * 2.0f * (float)DUEL_PI;
            float x = sinf(ph) * cosf(th), y = cosf(ph), z = sinf(ph) * sinf(th);
            float *d = v + (size_t)(j * (lon + 1) + i) * 8;
            d[0] = x; d[1] = y; d[2] = z;
            d[3] = x; d[4] = y; d[5] = z;
            d[6] = (float)i / (float)lon; d[7] = (float)j / (float)lat;
        }
    }
    int nq = lon * lat;
    Uint32 *ix = (Uint32 *)malloc((size_t)nq * 6 * sizeof(Uint32));
    int k = 0;
    for (int j = 0; j < lat; j++)
        for (int i = 0; i < lon; i++) {
            Uint32 a = (Uint32)(j * (lon + 1) + i), b = a + 1;
            Uint32 c = a + (Uint32)(lon + 1), e = c + 1;
            ix[k++] = a; ix[k++] = c; ix[k++] = b;
            ix[k++] = b; ix[k++] = c; ix[k++] = e;
        }
    r->sphere_vbo = make_buffer(r, SDL_GPU_BUFFERUSAGE_VERTEX, (Uint32)nv * 8 * sizeof(float), v);
    r->sphere_ibo = make_buffer(r, SDL_GPU_BUFFERUSAGE_INDEX, (Uint32)nq * 6 * sizeof(Uint32), ix);
    r->sphere_index_count = (Uint32)nq * 6;
    free(v);
    free(ix);
}

/* ---------------- create / destroy ---------------- */
/* P1.5: one cook PNG from <asset_dir>/<sub>/. Missing file -> NULL
 * (draw skipped); cooks are optional until pushed. */
static SDL_GPUTexture *load_cook_tex(DuelRender *r, const char *sub,
                                     const char *name, const char *tag) {
    char path[768];
    int w, h, comp;
    stbi_uc *px;
    SDL_GPUTexture *tex;
    snprintf(path, sizeof path, "%s/%s/%s", r->asset_dir, sub, name);
    px = stbi_load(path, &w, &h, &comp, 4);
    if (!px) { SDL_Log("%s missing %s", tag, path); return NULL; }
    tex = make_texture(r, w, h, px, 1);
    SDL_Log("%s %s %dx%d", tag, name, w, h);
    stbi_image_free(px);
    return tex;
}

/* P1.5: one authentic DAT sprite PNG from <asset_dir>/fx/. */
static SDL_GPUTexture *load_sprite_tex(DuelRender *r, const char *name) {
    return load_cook_tex(r, "fx", name, "DUEL_SPR");
}

DuelRender *DuelRender_Create(SDL_GPUDevice *device, SDL_GPUTextureFormat swap_fmt,
                              const char *asset_dir) {
    DuelRender *r = (DuelRender *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->device = device;
    snprintf(r->asset_dir, sizeof r->asset_dir, "%s", asset_dir);
    r->lit_vs = load_shader(r, "lit.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    r->lit_fs = load_shader(r, "lit.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
    r->unlit_vs = load_shader(r, "unlit.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    r->unlit_fs = load_shader(r, "unlit.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    r->planet_vs = load_shader(r, "planet.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    r->planet_fs = load_shader(r, "planet.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    r->cube_vs = load_shader(r, "m1_cube_vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    r->cube_fs = load_shader(r, "m1_cube_frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!r->lit_vs || !r->lit_fs || !r->unlit_vs || !r->unlit_fs || !r->planet_vs || !r->planet_fs ||
        !r->cube_vs || !r->cube_fs)
        return r; /* caller checks via DuelRender_Ready */
    r->pipe_lit = make_pipeline(r, r->lit_vs, r->lit_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                                1, 1, 0, 0, 0, swap_fmt, 1);
    /* P1.5: sprite pipe reuses the lit shaders (black base + sprite as
     * emissive, ambient=dir=0 at draw time). ADD blend makes black texels
     * vanish; depth test on (occluded by ships), write off (order-free). */
    r->pipe_sprite = make_pipeline(r, r->lit_vs, r->lit_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                                   1, 0, 1, 0, 0, swap_fmt, 1);
    r->pipe_planet = make_pipeline(r, r->planet_vs, r->planet_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                                   1, 1, 0, 0, 0, swap_fmt, 1);
    r->pipe_points_test = make_pipeline(r, r->unlit_vs, r->unlit_fs, SDL_GPU_PRIMITIVETYPE_POINTLIST,
                                        1, 0, 0, 0, 0, swap_fmt, 0);
    r->pipe_points_add = make_pipeline(r, r->unlit_vs, r->unlit_fs, SDL_GPU_PRIMITIVETYPE_POINTLIST,
                                       1, 0, 1, 0, 0, swap_fmt, 0);
    r->pipe_tri_add = make_pipeline(r, r->unlit_vs, r->unlit_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                                    1, 0, 1, 0, 0, swap_fmt, 0);
    r->pipe_lines = make_pipeline(r, r->unlit_vs, r->unlit_fs, SDL_GPU_PRIMITIVETYPE_LINELIST,
                                  0, 0, 0, 1, 1, swap_fmt, 0);
#if DUEL_VIRTUAL_HANDS
    /* M3.0: same alpha line blend, but depth-tested so the cockpit
     * occludes the hands (depth write off: lines never carve). */
    r->pipe_lines_test = make_pipeline(r, r->unlit_vs, r->unlit_fs, SDL_GPU_PRIMITIVETYPE_LINELIST,
                                       1, 0, 0, 1, 1, swap_fmt, 0);
    /* M3.3: opaque depth-tested tris for the quadrant handle. */
    r->pipe_tri_test = make_pipeline(r, r->unlit_vs, r->unlit_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                                     1, 1, 0, 0, 0, swap_fmt, 0);
#endif
    r->pipe_tri_alpha = make_pipeline(r, r->unlit_vs, r->unlit_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                                      0, 0, 0, 1, 0, swap_fmt, 0);
    r->pipe_cube = make_pipeline(r, r->cube_vs, r->cube_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                                 1, 1, 0, 0, 0, swap_fmt, 2);
    SDL_GPUSamplerCreateInfo si;
    memset(&si, 0, sizeof si);
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    r->sampler = SDL_CreateGPUSampler(device, &si);
    {
        Uint8 w[4] = { 255, 255, 255, 255 }, b[4] = { 0, 0, 0, 255 };
        r->white_tex = make_texture(r, 1, 1, w, 0);
        r->black_tex = make_texture(r, 1, 1, b, 0);
        /* mesh identity colors: red green blue yellow cyan magenta */
        static const Uint8 dc[6][4] = {
            { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 },
            { 255, 255, 0, 255 }, { 0, 255, 255, 255 }, { 255, 0, 255, 255 }
        };
        for (int i = 0; i < 6; i++) r->diag_tex[i] = make_texture(r, 1, 1, dc[i], 0);
    }
    if (!load_model(r, &r->tie, "TieFighter")) SDL_Log("DUEL TIE load failed");
    if (!load_model(r, &r->cockpit, "XwingCockpit")) SDL_Log("DUEL cockpit load failed");
    /* M5 ally exterior: optional (not in the ready gate: allies simply
     * don't draw until XwingExterior is pushed to device storage). */
    if (!load_model(r, &r->xwing, "XwingExterior"))
        SDL_Log("DUEL XwingExterior missing: allies fly invisible");
    build_sphere(r);
    build_m1_cube(r);
    /* dynamic buffer: lasers (64*6 verts) + lines + hud + flash, 9 floats each */
    {
        SDL_GPUBufferCreateInfo bi;
        memset(&bi, 0, sizeof bi);
        bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size = 4096 * 9 * sizeof(float);
        r->dyn_vbo = SDL_CreateGPUBuffer(device, &bi);
        SDL_GPUTransferBufferCreateInfo ti;
        memset(&ti, 0, sizeof ti);
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = 4096 * 9 * sizeof(float);
        r->dyn_tbuf = SDL_CreateGPUTransferBuffer(device, &ti);
    }
    /* P1.5: sprite billboard buffer (slots + sun, lit layout 8 floats) */
    {
        SDL_GPUBufferCreateInfo bi;
        memset(&bi, 0, sizeof bi);
        bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size = (DUEL_SPR_SLOTS + 1) * DUEL_SPR_QUAD_FLOATS * sizeof(float);
        r->spr_vbo = SDL_CreateGPUBuffer(device, &bi);
        SDL_GPUTransferBufferCreateInfo ti;
        memset(&ti, 0, sizeof ti);
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = (DUEL_SPR_SLOTS + 1) * DUEL_SPR_QUAD_FLOATS * sizeof(float);
        r->spr_tbuf = SDL_CreateGPUTransferBuffer(device, &ti);
    }
    /* P1.5/P1.7: authentic XWA DAT art (Effects1.dat groups; see checkpoint
     * for provenance). Cook pushes these; absent files draw nothing. */
    r->spr_tex[DUEL_SPR_BOOM0] = load_sprite_tex(r, "boom_00.png"); /* g2000 fr0 */
    r->spr_tex[DUEL_SPR_BOOM1] = load_sprite_tex(r, "boom_01.png"); /* g2000 fr23 */
    r->spr_tex[DUEL_SPR_SPARK] = load_sprite_tex(r, "spark_00.png"); /* g3000 fr0 */
    r->spr_tex[DUEL_SPR_SUN] = load_sprite_tex(r, "sun.png"); /* g9001 flare */
#if DUEL_CELESTIAL
    /* M2.2: planetary spheremap (MODE B) + procedural flare glow. The env
     * cook lands separately; a missing file draws nothing (old sun stays
     * as fallback). */
    r->env_tex = load_cook_tex(r, "env", "space_env.png", "DUEL_ENV");
    {
        static unsigned char flare[DUEL_FLARE_SIZE * DUEL_FLARE_SIZE * 4];
        duel_flare_tex(flare);
        r->spr_tex[DUEL_SPR_FLARE] = make_texture(r, DUEL_FLARE_SIZE,
                                                 DUEL_FLARE_SIZE, flare, 1);
    }
#endif
    /* M2.2a: stars last -- the planet-disc cull reads r->env_tex. */
    build_stars(r);
    r->last_hp_player = DUEL_PLAYER_HP;
    { /* M5: per-ship fx memory */
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) {
            r->last_hp_tie[k] = DUEL_TIE_HP;
            r->boom_tie[k] = 0;
            r->last_muzzle_t[k] = 0;
        }
        for (k = 0; k < DUEL_ALLY_COUNT; k++) {
            r->last_hp_ally[k] = DUEL_ALLY_HP;
            r->boom_ally[k] = 0;
            r->last_muzzle_a[k] = 0;
        }
    }
#if DUEL_VIRTUAL_HANDS
    /* M3.0: native hand trackers (XR session already up; DuelXr_Init ran
     * first in main). Zero is not fatal: controllers stay authoritative. */
    r->hands_mask = DuelHands_Init(DuelXr_GetProc(), DuelXr_Instance(),
                                   DuelXr_Session());
    SDL_Log("DUEL_HANDS init: mask=%d (1=L 2=R, 0=unavailable)", r->hands_mask);
#if DUEL_HAND_MESH
    /* M3.5: fetch each platform bind mesh; per-hand fallback to the
     * diagnostic joints when a fetch fails. Combined IBO uploaded once
     * (second hand's indices offset by the first hand's vert base). */
    r->hand_mesh[0] = r->hand_mesh[1] = 0;
    if (r->hands_mask) {
        int f0 = DuelHands_FetchMesh(0), f1 = DuelHands_FetchMesh(1);
        const DuelHandMesh *m0 = DuelHands_Mesh(0), *m1 = DuelHands_Mesh(1);
        int ok0 = f0 && m0->have, ok1 = f1 && m1->have;
        int nv = (ok0 ? m0->n_verts : 0) + (ok1 ? m1->n_verts : 0);
        int ni = (ok0 ? m0->n_idx : 0) + (ok1 ? m1->n_idx : 0);
        SDL_Log("DUEL_HANDS mesh: L=%d(%d/%d) R=%d(%d/%d)", ok0,
                ok0 ? m0->n_verts : 0, ok0 ? m0->n_idx : 0, ok1,
                ok1 ? m1->n_verts : 0, ok1 ? m1->n_idx : 0);
        if (nv > 0 && ni > 0 && nv <= 30000) {
            int16_t *ibuf = (int16_t *)malloc((size_t)ni * sizeof(int16_t));
            if (ibuf) {
                int vo = 0, io = 0;
                SDL_GPUTransferBufferCreateInfo ti;
                /* M3.6: the IBO stores RAW runtime indices; the per-draw
                 * vertex_offset carries the hand base. Pre-offsetting here
                 * AND passing vertex_offset double-shifted hand 1 out of
                 * the VBO (M3.5: right hand invisible on Quest). */
                if (ok0) {
                    r->hand_vbase[0] = 0; r->hand_ibase[0] = 0;
                    r->hand_nv[0] = m0->n_verts; r->hand_ni[0] = m0->n_idx;
                    memcpy(ibuf, m0->idx,
                           (size_t)m0->n_idx * sizeof(int16_t));
                    vo += m0->n_verts; io += m0->n_idx;
                    r->hand_mesh[0] = 1;
                }
                if (ok1) {
                    r->hand_vbase[1] = vo; r->hand_ibase[1] = io;
                    r->hand_nv[1] = m1->n_verts; r->hand_ni[1] = m1->n_idx;
                    memcpy(ibuf + io, m1->idx,
                           (size_t)m1->n_idx * sizeof(int16_t));
                    r->hand_mesh[1] = 1;
                }
                r->hand_ibo = make_buffer(r, SDL_GPU_BUFFERUSAGE_INDEX,
                                          (Uint32)ni * sizeof(int16_t), ibuf);
                r->hand_vbo = make_buffer(r, SDL_GPU_BUFFERUSAGE_VERTEX,
                                          (Uint32)nv * 9 * sizeof(float), NULL);
                free(ibuf);
                memset(&ti, 0, sizeof ti);
                ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                ti.size = (Uint32)nv * 9 * sizeof(float);
                r->hand_tbuf = SDL_CreateGPUTransferBuffer(device, &ti);
                r->hand_stage = (float *)malloc(ti.size);
                if (!r->hand_ibo || !r->hand_vbo || !r->hand_tbuf ||
                    !r->hand_stage) {
                    r->hand_mesh[0] = r->hand_mesh[1] = 0;
                    SDL_Log("DUEL_HANDS mesh buffers failed, joints stay");
                }
            }
        }
    }
#endif
#else
    r->hands_mask = 0;
#endif
    /* M3.6: no grab winners until the first assignment (calloc would
     * read as LEFT holding everything). */
    r->grab_holder[0] = r->grab_holder[1] = -1;
    r->hand_sens = 1.0f; /* M7: frozen feel until the menu changes it */
    return r;
}

int DuelRender_Ready(const DuelRender *r) {
    /* sprite textures are NOT required: the cook lands separately. */
    return r && r->pipe_lit && r->pipe_planet && r->pipe_tri_add && r->pipe_lines &&
        r->pipe_tri_alpha && r->pipe_cube && r->pipe_sprite && r->tie.vbo && r->cockpit.vbo &&
        r->dyn_vbo && r->spr_vbo && r->cube_vbo && r->dyn_tbuf && r->spr_tbuf
#if DUEL_VIRTUAL_HANDS
        && r->pipe_lines_test && r->pipe_tri_test
#endif
        ;
}

void DuelRender_Destroy(DuelRender *r) {
    if (!r) return;
#if DUEL_VIRTUAL_HANDS
    DuelHands_Shutdown();
#endif
    /* P0: process-lifetime resources; release device-owned handles. */
    SDL_ReleaseGPUBuffer(r->device, r->tie.vbo);
    SDL_ReleaseGPUBuffer(r->device, r->tie.ibo);
    SDL_ReleaseGPUBuffer(r->device, r->cockpit.vbo);
    SDL_ReleaseGPUBuffer(r->device, r->cockpit.ibo);
    if (r->xwing.vbo) SDL_ReleaseGPUBuffer(r->device, r->xwing.vbo);
    if (r->xwing.ibo) SDL_ReleaseGPUBuffer(r->device, r->xwing.ibo);
    SDL_ReleaseGPUBuffer(r->device, r->stars_vbo);
    SDL_ReleaseGPUBuffer(r->device, r->sphere_vbo);
    SDL_ReleaseGPUBuffer(r->device, r->sphere_ibo);
    SDL_ReleaseGPUBuffer(r->device, r->dyn_vbo);
    SDL_ReleaseGPUBuffer(r->device, r->spr_vbo);
    SDL_ReleaseGPUBuffer(r->device, r->cube_vbo);
    SDL_ReleaseGPUBuffer(r->device, r->cube_ibo);
    SDL_ReleaseGPUTransferBuffer(r->device, r->dyn_tbuf);
    SDL_ReleaseGPUTransferBuffer(r->device, r->spr_tbuf);
    /* M3.5: hand mesh resources (NULL-safe; calloc'd when unused). */
    if (r->hand_vbo) SDL_ReleaseGPUBuffer(r->device, r->hand_vbo);
    if (r->hand_ibo) SDL_ReleaseGPUBuffer(r->device, r->hand_ibo);
    if (r->hand_tbuf) SDL_ReleaseGPUTransferBuffer(r->device, r->hand_tbuf);
    free(r->hand_stage);
    for (int i = 0; i < DUEL_SPR_TEX_COUNT; i++)
        if (r->spr_tex[i]) SDL_ReleaseGPUTexture(r->device, r->spr_tex[i]);
    if (r->env_tex) SDL_ReleaseGPUTexture(r->device, r->env_tex);
    for (int i = 0; i < r->tie.tex_count; i++) SDL_ReleaseGPUTexture(r->device, r->tie.texs[i].tex);
    for (int i = 0; i < r->cockpit.tex_count; i++)
        SDL_ReleaseGPUTexture(r->device, r->cockpit.texs[i].tex);
    for (int i = 0; i < r->xwing.tex_count; i++)
        SDL_ReleaseGPUTexture(r->device, r->xwing.texs[i].tex);
    free(r->tie.prims); free(r->tie.texs);
    free(r->cockpit.prims); free(r->cockpit.texs);
    free(r->xwing.prims); free(r->xwing.texs);
    SDL_ReleaseGPUTexture(r->device, r->white_tex);
    SDL_ReleaseGPUTexture(r->device, r->black_tex);
    for (int i = 0; i < 6; i++) SDL_ReleaseGPUTexture(r->device, r->diag_tex[i]);
    SDL_ReleaseGPUSampler(r->device, r->sampler);
    SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipe_lit);
    SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipe_sprite);
    SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipe_planet);
    SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipe_points_test);
    SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipe_points_add);
    SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipe_tri_add);
    SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipe_lines);
    if (r->pipe_lines_test) SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipe_lines_test);
    if (r->pipe_tri_test) SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipe_tri_test);
    SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipe_tri_alpha);
    SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipe_cube);
    SDL_ReleaseGPUShader(r->device, r->cube_vs);
    SDL_ReleaseGPUShader(r->device, r->cube_fs);
    SDL_ReleaseGPUShader(r->device, r->lit_vs);
    SDL_ReleaseGPUShader(r->device, r->lit_fs);
    SDL_ReleaseGPUShader(r->device, r->unlit_vs);
    SDL_ReleaseGPUShader(r->device, r->unlit_fs);
    SDL_ReleaseGPUShader(r->device, r->planet_vs);
    SDL_ReleaseGPUShader(r->device, r->planet_fs);
    free(r);
}

/* ---------------- particles / fx ---------------- */
static void spawn_burst(DuelRender *r, DuelVec3 at, float speed_lo, float speed_hi,
                        int count, float life, float cr, float cg, float cb) {
    static unsigned seed = 12345u;
    for (int n = 0; n < count; n++) {
        int slot = -1;
        for (int i = 0; i < MAX_PARTICLES; i++)
            if (!r->parts[i].active) { slot = i; break; }
        if (slot < 0) return;
        DuelParticle *p = &r->parts[slot];
        p->active = 1;
        p->pos = at;
        float u = (float)duel_lcg(&seed) / 4294967296.0f;
        float v = (float)duel_lcg(&seed) / 4294967296.0f;
        float th = u * 2.0f * (float)DUEL_PI;
        float z = v * 2.0f - 1.0f;
        float rr = sqrtf(1.0f - z * z);
        float sp = speed_lo + (speed_hi - speed_lo) * ((float)duel_lcg(&seed) / 4294967296.0f);
        p->vel = duel_v3(rr * cosf(th) * sp, z * sp, rr * sinf(th) * sp);
        p->max_life = p->life = life * (0.6f + 0.8f * ((float)duel_lcg(&seed) / 4294967296.0f));
        float w = (float)duel_lcg(&seed) / 4294967296.0f;
        p->r = cr * (0.7f + 0.3f * w); p->g = cg * (0.7f + 0.3f * w); p->b = cb;
    }
}

void DuelRender_Update(DuelRender *r, const DuelState *s, float dt) {
    /* hit sparks on hp decrease */
    if (s->player.hp < r->last_hp_player - 0.01f && s->player.alive) {
        spawn_burst(r, s->player.pos, 10.0f, 40.0f, 8, 0.6f, 1.0f, 0.6f, 0.2f);
        /* cockpit feedback (XWA-like): shake + flash + dashboard sparks */
        r->shake = 1.0f;
        if (r->flash < 0.35f) r->flash = 0.35f;
        for (int k = -1; k <= 1; k++) {
            DuelVec3 local =
                duel_v3(0.22f * (float)k, 0.70f, -0.85f); /* panel, ship frame */
            DuelVec3 at = duel_v3_add(
                s->player.pos, duel_q_rot(s->player.orient, local));
            spawn_burst(r, at, 1.0f, 5.0f, 4, 0.35f, 1.0f, 0.95f, 0.6f);
            spawn_burst(r, at, 0.5f, 3.0f, 3, 0.5f, 1.0f, 0.5f, 0.15f);
        }
        /* authentic impact flash on the panel (XWA spark sprite) */
        {
            DuelVec3 at = duel_v3_add(s->player.pos,
                duel_q_rot(s->player.orient, duel_v3(0.0f, 0.70f, -0.85f)));
            duel_sprite_spawn(r->sprites, DUEL_SPR_SLOTS, DUEL_SPR_SPARK, 1,
                              at, 0.22f, 1.6f, 0.6f, 2.2f, 0.0f);
        }
    }
    { /* M5: hit bursts per ship (ties + allies) */
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) {
            if (s->ties[k].hp < r->last_hp_tie[k] - 0.01f && s->ties[k].alive) {
                spawn_burst(r, s->ties[k].pos, 10.0f, 40.0f, 8, 0.6f, 1.0f, 0.8f, 0.3f);
                spawn_burst(r, s->ties[k].pos, 5.0f, 25.0f, 4, 0.25f, 1.0f, 1.0f, 0.9f);
                duel_sprite_spawn(r->sprites, DUEL_SPR_SLOTS, DUEL_SPR_SPARK, 1,
                                  s->ties[k].pos, 0.22f, 10.0f, 4.0f, 2.0f, 0.0f);
            }
        }
        for (k = 0; k < DUEL_ALLY_COUNT; k++) {
            if (s->allies[k].hp < r->last_hp_ally[k] - 0.01f && s->allies[k].alive) {
                spawn_burst(r, s->allies[k].pos, 10.0f, 40.0f, 8, 0.6f, 1.0f, 0.8f, 0.3f);
                spawn_burst(r, s->allies[k].pos, 5.0f, 25.0f, 4, 0.25f, 1.0f, 1.0f, 0.9f);
                duel_sprite_spawn(r->sprites, DUEL_SPR_SLOTS, DUEL_SPR_SPARK, 1,
                                  s->allies[k].pos, 0.22f, 10.0f, 4.0f, 2.0f, 0.0f);
            }
        }
    }
    /* muzzle flash: white-hot pop at the gun that just fired */
    if (s->player.muzzle != r->last_muzzle_p) {
        if (s->player.muzzle > r->last_muzzle_p && s->player.alive) {
            DuelVec3 at = Duel_MuzzleWorldAt(&s->player, DUEL_SHIP_PLAYER,
                                             s->player.muzzle - 1);
            spawn_burst(r, at, 2.0f, 8.0f, 3, 0.12f, 1.0f, 0.9f, 0.75f);
            spawn_burst(r, at, 1.0f, 4.0f, 2, 0.2f, 1.0f, 0.25f, 0.05f);
            duel_sprite_spawn(r->sprites, DUEL_SPR_SLOTS, DUEL_SPR_SPARK, 1,
                              at, 0.12f, 3.0f, 1.2f, 2.5f, 0.0f);
        }
        r->last_muzzle_p = s->player.muzzle;
    }
    { /* M5: muzzle flashes per ship */
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) {
            if (s->ties[k].muzzle != r->last_muzzle_t[k]) {
                if (s->ties[k].muzzle > r->last_muzzle_t[k] && s->ties[k].alive) {
                    DuelVec3 at = Duel_MuzzleWorldAt(&s->ties[k], DUEL_SHIP_TIE,
                                                     s->ties[k].muzzle - 1);
                    spawn_burst(r, at, 2.0f, 8.0f, 3, 0.12f, 0.85f, 1.0f, 0.85f);
                    spawn_burst(r, at, 1.0f, 4.0f, 2, 0.2f, 0.1f, 1.0f, 0.25f);
                    duel_sprite_spawn(r->sprites, DUEL_SPR_SLOTS, DUEL_SPR_SPARK, 1,
                                      at, 0.12f, 3.0f, 1.2f, 2.5f, 0.0f);
                }
                r->last_muzzle_t[k] = s->ties[k].muzzle;
            }
        }
        for (k = 0; k < DUEL_ALLY_COUNT; k++) {
            if (s->allies[k].muzzle != r->last_muzzle_a[k]) {
                if (s->allies[k].muzzle > r->last_muzzle_a[k] && s->allies[k].alive) {
                    DuelVec3 at = Duel_MuzzleWorldAt(&s->allies[k], DUEL_SHIP_ALLY,
                                                     s->allies[k].muzzle - 1);
                    spawn_burst(r, at, 2.0f, 8.0f, 3, 0.12f, 1.0f, 0.9f, 0.75f);
                    spawn_burst(r, at, 1.0f, 4.0f, 2, 0.2f, 1.0f, 0.25f, 0.05f);
                    duel_sprite_spawn(r->sprites, DUEL_SPR_SLOTS, DUEL_SPR_SPARK, 1,
                                      at, 0.12f, 3.0f, 1.2f, 2.5f, 0.0f);
                }
                r->last_muzzle_a[k] = s->allies[k].muzzle;
            }
        }
    }
    if (r->shake > 0.0f) {
        r->shake *= expf(-dt * 5.0f);
        if (r->shake < 0.003f) r->shake = 0.0f;
    }
    r->last_hp_player = s->player.hp;
    { /* M5 */
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) r->last_hp_tie[k] = s->ties[k].hp;
        for (k = 0; k < DUEL_ALLY_COUNT; k++) r->last_hp_ally[k] = s->allies[k].hp;
    }
    if (!s->player.alive && !r->boom_player) {
        r->boom_player = 1;
        /* staged: white flash + orange fireball + dark embers */
        spawn_burst(r, s->player.pos, 60.0f, 140.0f, 20, 0.25f, 1.0f, 1.0f, 0.95f);
        spawn_burst(r, s->player.pos, 20.0f, 90.0f, 60, 1.2f, 1.0f, 0.55f, 0.15f);
        spawn_burst(r, s->player.pos, 5.0f, 30.0f, 40, 2.6f, 0.7f, 0.2f, 0.05f);
        duel_sprite_spawn(r->sprites, DUEL_SPR_SLOTS, DUEL_SPR_BOOM0, 2,
                          s->player.pos, 1.2f, 30.0f, 120.0f, 1.3f, 0.25f);
        r->flash = 1.0f;
    }
    { /* M5: one staged explosion per kill (ties + allies); the wave
         * reset rearms every flag (see below). */
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) {
            if (!s->ties[k].alive && !r->boom_tie[k]) {
                r->boom_tie[k] = 1;
                spawn_burst(r, s->ties[k].pos, 60.0f, 140.0f, 20, 0.25f, 1.0f, 1.0f, 0.95f);
                spawn_burst(r, s->ties[k].pos, 20.0f, 90.0f, 60, 1.2f, 1.0f, 0.55f, 0.15f);
                spawn_burst(r, s->ties[k].pos, 5.0f, 30.0f, 40, 2.6f, 0.7f, 0.2f, 0.05f);
                duel_sprite_spawn(r->sprites, DUEL_SPR_SLOTS, DUEL_SPR_BOOM0, 2,
                                  s->ties[k].pos, 1.2f, 30.0f, 120.0f, 1.3f, 0.25f);
                r->flash = 1.0f;
            }
        }
        for (k = 0; k < DUEL_ALLY_COUNT; k++) {
            if (!s->allies[k].alive && !r->boom_ally[k]) {
                r->boom_ally[k] = 1;
                spawn_burst(r, s->allies[k].pos, 60.0f, 140.0f, 20, 0.25f, 1.0f, 1.0f, 0.95f);
                spawn_burst(r, s->allies[k].pos, 20.0f, 90.0f, 60, 1.2f, 1.0f, 0.55f, 0.15f);
                spawn_burst(r, s->allies[k].pos, 5.0f, 30.0f, 40, 2.6f, 0.7f, 0.2f, 0.05f);
                duel_sprite_spawn(r->sprites, DUEL_SPR_SLOTS, DUEL_SPR_BOOM0, 2,
                                  s->allies[k].pos, 1.2f, 30.0f, 120.0f, 1.3f, 0.25f);
                r->flash = 1.0f;
            }
        }
    }
    duel_sprite_update(r->sprites, DUEL_SPR_SLOTS, dt);
    /* M5: rearm ONLY on a fresh wave (everyone alive: exactly the
     * post-reset state). Rearming on partial survival would re-fire
     * the dead ships' explosions every frame mid-wave. */
    if (s->phase == DUEL_PHASE_COMBAT && s->player.alive &&
            Duel_TiesAlive(s) == DUEL_TIE_COUNT &&
            Duel_AlliesAlive(s) == DUEL_ALLY_COUNT) {
        int k;
        r->boom_player = 0;
        for (k = 0; k < DUEL_TIE_COUNT; k++) r->boom_tie[k] = 0;
        for (k = 0; k < DUEL_ALLY_COUNT; k++) r->boom_ally[k] = 0;
    }
    if (r->flash > 0.0f) r->flash -= dt * 1.6f;
    if (r->flash < 0.0f) r->flash = 0.0f;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        DuelParticle *p = &r->parts[i];
        if (!p->active) continue;
        p->life -= dt;
        if (p->life <= 0.0f) { p->active = 0; continue; }
        p->pos = duel_v3_add(p->pos, duel_v3_scale(p->vel, dt));
        p->vel = duel_v3_scale(p->vel, 1.0f - 0.6f * dt);
    }
#if DUEL_SPACE_DEPTH
    /* M2.1: collect the world depth field around the SHIP (never the HMD:
     * head motion must not move the world). Once per frame; both eyes
     * share the same world points with their own VP (true stereo). */
    r->depth_nn = duel_depth_collect(&s->player.pos, DUEL_DEPTH_NEAR,
                                     r->depth_near, DUEL_DEPTH_NEAR_CAP);
    r->depth_nm = duel_depth_collect(&s->player.pos, DUEL_DEPTH_MID,
                                     r->depth_mid, DUEL_DEPTH_MID_CAP);
    r->rs_timer += dt;
    if (r->rs_timer >= 6.0f) {
        int nc = 2 * DUEL_DEPTH_NEAR_HALF + 1;
        int mc = 2 * DUEL_DEPTH_MID_HALF + 1;
        r->rs_timer = 0.0f;
        SDL_Log("DUEL_REALSPACE near=%d mid=%d cells=%d draws=2 speed=%.0f mode=WORLD_GRID",
                r->depth_nn, r->depth_nm, nc * nc * nc + mc * mc * mc,
                s->player.speed);
    }
#else
    r->depth_nn = 0;
    r->depth_nm = 0;
#endif
#if DUEL_VEHICLE_MOTION_FEEL
    /* M2.2: feed the inertia springs from ship state (history-derived;
     * the sim itself is untouched). Reset on combat entry so the death/
     * respawn teleport never spikes the rates. */
    if (r->mo_last_phase != (int)DUEL_PHASE_COMBAT &&
        s->phase == DUEL_PHASE_COMBAT) {
        duel_motion_reset(&r->motion);
    }
    r->mo_last_phase = (int)s->phase;
    duel_motion_update(&r->motion, s->player.orient, s->player.speed, dt);
    r->mo_timer += dt;
    if (r->mo_timer >= 3.0f) {
        DuelVec3 fw = duel_q_forward(s->player.orient);
        r->mo_timer = 0.0f;
        SDL_Log("DUEL_MOTION pos=(%.1f,%.1f,%.1f) speed=%.0f fwd=(%.2f,%.2f,%.2f) "
                "accel=%.1f rate=(%.2f,%.2f,%.2f) "
                "off_p=(%.3f,%.3f,%.3f) off_r=(%.3f,%.3f,%.3f)",
                s->player.pos.x, s->player.pos.y, s->player.pos.z,
                s->player.speed, fw.x, fw.y, fw.z,
                r->motion.accel_fwd,
                r->motion.rate.x, r->motion.rate.y, r->motion.rate.z,
                r->motion.off_p.x, r->motion.off_p.y, r->motion.off_p.z,
                r->motion.off_r.x, r->motion.off_r.y, r->motion.off_r.z);
    }
#else
    duel_motion_reset(&r->motion);
#endif
#if DUEL_VIRTUAL_HANDS
    /* M3.0: hand telemetry at ~0.5 Hz (previous frame's joints; the locate
     * runs after Calibrate in main). Ship-local palm = seat + neutralized
     * LOCAL offset (motion offsets are cm-scale: noted, not applied). */
    r->hands_timer += dt;
    if (r->hands_mask && r->hands_timer >= 2.0f) {
        const DuelHand *hl = DuelHands_Get(DUEL_HAND_LEFT);
        const DuelHand *hr = DuelHands_Get(DUEL_HAND_RIGHT);
        DuelVec3 lp, rp;
        DuelQuat lqo, rqo;
        DuelVec3 lship, rship;
        r->hands_timer = 0.0f;
        duel_cal_neutralize(&r->cal, hl->j[0].pos, hl->j[0].quat, &lp, &lqo);
        duel_cal_neutralize(&r->cal, hr->j[0].pos, hr->j[0].quat, &rp, &rqo);
        lship = duel_v3_add(DUEL_SEAT_POS, lp);
        rship = duel_v3_add(DUEL_SEAT_POS, rp);
        SDL_Log("DUEL_HANDS L act=%d nv=%d lr=%d lc=%d palm=(%.2f,%.2f,%.2f) ship=(%.2f,%.2f,%.2f) lev=%.2f | "
                "R act=%d nv=%d lr=%d lc=%d palm=(%.2f,%.2f,%.2f) ship=(%.2f,%.2f,%.2f) lev=%.2f",
                hl->active, hl->n_valid, hl->last_result, hl->locate_calls,
                lp.x, lp.y, lp.z,
                lship.x, lship.y, lship.z,
                duel_v3_len(duel_v3_sub(lship, DUEL_LEVER_SHIP)),
                hr->active, hr->n_valid, hr->last_result, hr->locate_calls,
                rp.x, rp.y, rp.z,
                rship.x, rship.y, rship.z,
                duel_v3_len(duel_v3_sub(rship, DUEL_LEVER_SHIP)));
    }
    /* M3.1: lever grab state per hand (previous frame's joints).
     * Palm/midpoint in ship frame (seat + neutralized LOCAL, motion
     * cm-scale noted); pinch distance is frame-invariant. Gated on a
     * valid seat calibration: uncalibrated ship-local is meaningless.
     * M3.2: distances measured against the MOVED grip; mids stored for
     * the hand-throttle getter; NO phase gate (death continuity).
     * M3.3 HOTAS: LEFT targets the throttle quadrant (moved by sim
     * throttle), RIGHT targets the stick (moved by deflection).
     * M3.6 free grab: each hand runs one machine PER TARGET; winners
     * assigned by DuelGrab_Assign (nearest HELD). Either hand may
     * hold the stick or the quadrant (user order). */
    {
        static int pqh = -2, psh = -2;
        DuelVec3 tgt[2];
        float mdist[2][2];
        int side;
        DuelLever_GripShip(s->player.throttle, r->defl_ld, &tgt[DUEL_TGT_QUAD]);
        DuelStick_GripShip(r->defl_pd, r->defl_rd, &tgt[DUEL_TGT_STICK]);
        for (side = 0; side < 2; side++) {
            const DuelHand *h = DuelHands_Get(side);
            DuelVec3 pp, pm;
            DuelQuat pq, mq;
            float pinch_d = 99.0f, fist_d = 99.0f;
            int gated, have_palm = 0, have_mid = 0;
            int was_stick, was_quad;
            r->grab_mid_valid[side] = 0;
            r->grab_palm_valid[side] = 0;
            gated = r->hands_mask && r->cal.valid && h->active;
            if (gated) {
                if (h->j[0].pos_valid) {
                    duel_cal_neutralize(&r->cal, h->j[0].pos, h->j[0].quat,
                                        &pp, &pq);
                    pp = duel_v3_add(DUEL_SEAT_POS, pp);
                    r->grab_palm[side] = pp;
                    r->grab_palm_valid[side] = 1;
                    have_palm = 1;
                }
                if (h->j[5].pos_valid && h->j[10].pos_valid) {
                    DuelVec3 t = h->j[5].pos, f = h->j[10].pos;
                    DuelVec3 mid;
                    pinch_d = duel_v3_len(duel_v3_sub(t, f));
                    mid = duel_v3_scale(duel_v3_add(t, f), 0.5f);
                    duel_cal_neutralize(&r->cal, mid, duel_q_identity(),
                                        &pm, &mq);
                    pm = duel_v3_add(DUEL_SEAT_POS, pm);
                    r->grab_mid[side] = pm;
                    r->grab_mid_valid[side] = 1;
                    have_mid = 1;
                }
                fist_d = DuelHand_FistDist(h);
            }
            r->grab_fist[side] = fist_d;
            r->grab_pinch[side] = pinch_d;
            /* M3.7: quadrant keeps the validated pinch; the stick uses
             * the fist (thumb-independent) against the palm point, so
             * lifting the thumb to fire never breaks the hold. */
            was_quad = r->grab[side][DUEL_TGT_QUAD].state;
            {
                float palm_d = have_palm ?
                    duel_v3_len(duel_v3_sub(pp, tgt[DUEL_TGT_QUAD])) : 99.0f;
                float mid_d = have_mid ?
                    duel_v3_len(duel_v3_sub(pm, tgt[DUEL_TGT_QUAD])) : 99.0f;
                mdist[side][DUEL_TGT_QUAD] = mid_d;
                DuelGrab_Update(&r->grab[side][DUEL_TGT_QUAD], gated,
                                palm_d, pinch_d, mid_d);
            }
            /* M6: lever roll anchor mirrors the stick anchor (latch =
             * zero roll; grabbing never rolls by itself). */
            if (r->grab[side][DUEL_TGT_QUAD].state == DUEL_GRAB_HELD) {
                if (was_quad != DUEL_GRAB_HELD && have_palm) {
                    r->lever_anchor[side] = pp;
                    r->lever_anchor_valid[side] = 1;
                }
            } else {
                r->lever_anchor_valid[side] = 0;
            }
            was_stick = r->grab[side][DUEL_TGT_STICK].state;
            {
                float palm_d = have_palm ?
                    duel_v3_len(duel_v3_sub(pp, tgt[DUEL_TGT_STICK])) : 99.0f;
                mdist[side][DUEL_TGT_STICK] = palm_d;
                DuelGrab_UpdateT(&r->grab[side][DUEL_TGT_STICK], gated,
                                 palm_d, fist_d, palm_d,
                                 DUEL_FIST_SHUT, DUEL_FIST_OPEN,
                                 DUEL_GRAB_STICK_LATCH_R, DUEL_GRAB_LOSE_R);
            }
            /* Latch-relative anchor: grabbing never steers by itself. */
            if (r->grab[side][DUEL_TGT_STICK].state == DUEL_GRAB_HELD) {
                if (was_stick != DUEL_GRAB_HELD && have_palm) {
                    r->stick_anchor[side] = pp;
                    r->stick_anchor_valid[side] = 1;
                }
            } else {
                r->stick_anchor_valid[side] = 0;
            }
        }
        DuelGrab_Assign((const DuelGrab (*)[2])r->grab, mdist,
                        r->grab_holder);
        /* M3.3: stick deflection tracks the HOLDER's palm from its grab
         * anchor while held, else spring-decays. M3.6: the holder is
         * either hand. M3.7: relative (no absolute grip dependence). */
        {
            int hs = r->grab_holder[DUEL_TGT_STICK];
            if (hs >= 0 && r->grab_palm_valid[hs] &&
                r->stick_anchor_valid[hs]) {
                DuelStick_DeflRel(r->grab_palm[hs], r->stick_anchor[hs],
                                  &r->defl_pd, &r->defl_rd);
            } else if (hs < 0) {
                DuelStick_Decay(&r->defl_pd, &r->defl_rd, dt);
            }
            /* Held but anchorless (palm invalid at latch): freeze, the
             * next valid palm re-anchors on the following latch. */
        }
        /* M6: left-lever roll tracks the HOLDER's palm lateral offset
         * from its latch anchor (same DeflRel/range as the stick, pd
         * discarded: the slot owns fore/aft). Throttle is NOT here:
         * it stays absolute via DuelRender_HandThrottle and holds its
         * value on release while roll spring-decays to neutral. */
        {
            int hl = r->grab_holder[DUEL_TGT_QUAD];
            if (hl >= 0 && r->grab_palm_valid[hl] &&
                r->lever_anchor_valid[hl]) {
                float drop = 0.0f;
                DuelStick_DeflRel(r->grab_palm[hl], r->lever_anchor[hl],
                                  &drop, &r->defl_ld);
            } else if (hl < 0) {
                float z = 0.0f;
                DuelStick_Decay(&z, &r->defl_ld, dt);
            }
        }
        /* M3.4: trigger = thumb gesture while the hand holds the
         * stick. M3.5: thumb, not index (fist grip). M3.6: + index
         * curled gate (pointing splays the thumb). M3.7: either
         * thumb. M3.8 (user): thumb RESTING ON the index fires
         * continuously; lifted/away = no fire. Metric is thumb tip
         * to index-intermediate in LOCAL (frame-invariant). */
        {
            int fside;
            for (fside = 0; fside < 2; fside++) {
                const DuelHand *h = DuelHands_Get(fside);
                DuelFire *f = fside == 0 ? &r->fire_l : &r->fire_r;
                int fok = h->j[DUEL_FIRE_JOINT].pos_valid &&
                    h->j[DUEL_FIRE_REF_JOINT].pos_valid;
                int iok = h->j[DUEL_FIRE_INDEX_JOINT].pos_valid &&
                    h->j[0].pos_valid;
                float dst = fok ?
                    duel_v3_len(duel_v3_sub(h->j[DUEL_FIRE_JOINT].pos,
                                            h->j[DUEL_FIRE_REF_JOINT].pos))
                    : 99.0f;
                float iext = iok ?
                    duel_v3_len(duel_v3_sub(h->j[DUEL_FIRE_INDEX_JOINT].pos,
                                            h->j[0].pos)) : 99.0f;
                int held = DuelFire_HoldOk(
                    r->grab_holder[DUEL_TGT_STICK] == fside,
                    h->active, iok, iext);
                int was = f->firing;
                DuelFire_Update(f, held, fok, dst);
                if (f->firing != was)
                    SDL_Log("DUEL_FIRE %c %s (dst=%.3f idx=%.3f)",
                            fside == 0 ? 'L' : 'R',
                            f->firing ? "on" : "off", dst, iext);
            }
        }
        {
            int qh = r->grab_holder[DUEL_TGT_QUAD];
            int sh = r->grab_holder[DUEL_TGT_STICK];
            r->grab_wdist[DUEL_TGT_QUAD] =
                qh >= 0 ? mdist[qh][DUEL_TGT_QUAD] : 99.0f;
            r->grab_wdist[DUEL_TGT_STICK] =
                sh >= 0 ? mdist[sh][DUEL_TGT_STICK] : 99.0f;
            int any = r->grab[0][0].state != DUEL_GRAB_FAR ||
                r->grab[0][1].state != DUEL_GRAB_FAR ||
                r->grab[1][0].state != DUEL_GRAB_FAR ||
                r->grab[1][1].state != DUEL_GRAB_FAR;
            if (qh != pqh || sh != psh)
                SDL_Log("DUEL_GRAB holders Q=%d S=%d", qh, sh);
            pqh = qh;
            psh = sh;
            r->grab_timer += dt;
            if (r->grab_timer >= 1.0f && any) {
                r->grab_timer = 0.0f;
                SDL_Log("DUEL_GRAB Qh=%d Sh=%d thr=%.2f tsrc=%c defl=(%+.2f,%+.2f) lev=%+.2f ssrc=%c fist=(%.3f,%.3f) pinch=(%.3f,%.3f) wd=(%.2f,%.2f)",
                        qh, sh, s->player.throttle, qh >= 0 ? 'H' : 'S',
                        r->defl_pd, r->defl_rd, r->defl_ld, sh >= 0 ? 'H' : 'S',
                        r->grab_fist[0], r->grab_fist[1],
                        r->grab_pinch[0], r->grab_pinch[1],
                        r->grab_wdist[0], r->grab_wdist[1]);
            }
        }
    }
    /* M3.0a: active transitions log immediately (event, not spam): proves
     * the exact moment the runtime starts/stops seeing each hand. */
    {
        const DuelHand *hl = DuelHands_Get(DUEL_HAND_LEFT);
        const DuelHand *hr = DuelHands_Get(DUEL_HAND_RIGHT);
        static int pl = -1, pr = -1;
        if (pl >= 0 && hl->active != pl)
            SDL_Log("DUEL_HANDS transition L %d->%d nv=%d lc=%d",
                    pl, hl->active, hl->n_valid, hl->locate_calls);
        if (pr >= 0 && hr->active != pr)
            SDL_Log("DUEL_HANDS transition R %d->%d nv=%d lc=%d",
                    pr, hr->active, hr->n_valid, hr->locate_calls);
        pl = hl->active;
        pr = hr->active;
    }
#endif
}

/* ---------------- eye rendering ---------------- */
typedef struct DynWriter {
    float *v;
    int count, cap; /* verts */
} DynWriter;

static float *dyn_reserve(DynWriter *w, int n) {
    if (w->count + n > w->cap) return NULL;
    float *d = w->v + (size_t)w->count * 9;
    w->count += n;
    return d;
}

static void dyn_vert(float *d, DuelVec3 p, float r, float g, float b, float a) {
    d[0] = p.x; d[1] = p.y; d[2] = p.z;
    d[3] = r; d[4] = g; d[5] = b; d[6] = a;
    d[7] = 0.0f; d[8] = 0.0f;
}

/* M3.2: one model pass. lever_only: -1 = no lever filter (legacy path),
 * 0 = everything except the lever prims, 1 = only the lever prims. */
static void draw_model_pass(DuelRender *r, SDL_GPUCommandBuffer *cmd,
                            SDL_GPURenderPass *rp, const DuelModel *m,
                            const float *vp, const float *model_m,
                            int diag, int skip_blend, int lever_only) {
    SDL_BindGPUGraphicsPipeline(rp, r->pipe_lit);
    SDL_GPUBufferBinding vb;
    memset(&vb, 0, sizeof vb);
    vb.buffer = m->vbo;
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_GPUBufferBinding ib;
    memset(&ib, 0, sizeof ib);
    ib.buffer = m->ibo;
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    LitVSU vsu;
    DuelMat4 vp_m, md_m;
    memcpy(vp_m.m, vp, sizeof vp_m.m);
    memcpy(md_m.m, model_m, sizeof md_m.m);
    DuelMat4 mvp = duel_m4_mul(vp_m, md_m);
    memcpy(vsu.mvp, mvp.m, sizeof vsu.mvp);
    memcpy(vsu.model, model_m, 16 * sizeof(float));
    SDL_PushGPUVertexUniformData(cmd, 0, &vsu, sizeof vsu);
    LitFSU fsu;
    if (diag) {
        /* flat mesh-identity colors, no textures, no lighting */
        fsu.light_dir[0] = 0.0f; fsu.light_dir[1] = 1.0f; fsu.light_dir[2] = 0.0f;
        fsu.ambient = 1.0f;
        fsu.dir_intensity = 0.0f;
        fsu.emissive_strength = 0.0f;
    } else {
        fsu.light_dir[0] = 0.4f; fsu.light_dir[1] = 0.7f; fsu.light_dir[2] = 0.5f;
        fsu.ambient = 0.35f;
        fsu.dir_intensity = 1.1f;
        fsu.emissive_strength = 1.0f;
    }
    fsu.alpha = 1.0f;
    fsu.pad = 0.0f;
    SDL_PushGPUFragmentUniformData(cmd, 0, &fsu, sizeof fsu);
    for (int i = 0; i < m->prim_count; i++) {
        int mi = m->prims[i].mesh_idx;
        if (lever_only >= 0 &&
            DuelLever_IsPrim(mi, m->prims[i].prim_idx) != lever_only)
            continue;
        if (diag && !(DUEL_COCKPIT_MESH_MASK & (1ULL << (mi & 63)))) continue;
        if (skip_blend && m->prims[i].blend) continue;
        SDL_GPUTextureSamplerBinding binds[2];
        memset(binds, 0, sizeof binds);
        if (diag) {
            binds[0].texture = r->diag_tex[mi < 0 ? 0 : (mi % 6)];
            binds[1].texture = r->black_tex;
        } else {
            binds[0].texture = m->prims[i].base_tex ? m->prims[i].base_tex : r->white_tex;
            binds[1].texture = m->prims[i].emis_tex ? m->prims[i].emis_tex : r->black_tex;
        }
        binds[0].sampler = r->sampler;
        binds[1].sampler = r->sampler;
        SDL_BindGPUFragmentSamplers(rp, 0, binds, 2);
        SDL_DrawGPUIndexedPrimitives(rp, m->prims[i].index_count, 1, m->prims[i].first_index, 0, 0);
    }
}

/* M3.2: lever_m = OPT-frame extra matrix (16 floats) for the stick-column
 * prims, or NULL for the legacy single pass. The lever pass reuses the
 * same pipeline with MVP = vp * model * lever_m (lighting model too). */
static void draw_model(DuelRender *r, SDL_GPUCopyPass *pass, SDL_GPUCommandBuffer *cmd,
                       SDL_GPURenderPass *rp, const DuelModel *m,
                       const float *vp, const float *model_m, int diag, int skip_blend,
                       const float *lever_m) {
    (void)pass;
    if (!lever_m) {
        draw_model_pass(r, cmd, rp, m, vp, model_m, diag, skip_blend, -1);
        return;
    }
    draw_model_pass(r, cmd, rp, m, vp, model_m, diag, skip_blend, 0);
    {
        DuelMat4 md_m, le_m;
        memcpy(md_m.m, model_m, sizeof md_m.m);
        memcpy(le_m.m, lever_m, 16 * sizeof(float));
        {
            DuelMat4 ml = duel_m4_mul(md_m, le_m);
            draw_model_pass(r, cmd, rp, m, vp, ml.m, diag, skip_blend, 1);
        }
    }
}

#define DUEL_CAL_SETTLE_FRAMES 150 /* ~2 s donning transient at 72 Hz */
#define DUEL_CAL_SPACE_SETTLE 30   /* frames for the recenter jump to land */

/* P0.4: seat calibration driver. Called once per frame with both views
 * (main loop, before the eye loop). Captures the neutral HMD reference
 * ONLY at: combat entry (after settle), reference-space recenter, and
 * combat-reset transitions -- never per frame. The ship and cockpit are
 * never touched; only the camera-side neutral moves. */
void DuelRender_Calibrate(DuelRender *r, const DuelState *s,
                          const XrPosef *vl, const XrPosef *vr, int have_views) {
    int reset_edge = (r->last_phase != (int)DUEL_PHASE_COMBAT &&
                      s->phase == DUEL_PHASE_COMBAT);
    r->last_phase = (int)s->phase;
    /* Consume recenter only when views exist to capture from; else the
     * latch stays sticky until tracking is valid. */
    if (have_views && DuelXr_ConsumeSpaceChange()) {
        if (r->cal.valid) SDL_Log("DUEL cal: space change, recalibrating");
        r->cal.valid = 0;
        r->cal_space = 1;
        r->cal_settle = DUEL_CAL_SETTLE_FRAMES - DUEL_CAL_SPACE_SETTLE;
    }
    if (!have_views) return;
    if (r->cal.valid && !reset_edge) return; /* steady state: never re-capture */
    if (!r->cal.valid) {
        if (++r->cal_settle < DUEL_CAL_SETTLE_FRAMES) return;
    }
    /* Capture: mid-eye position + left-eye orientation (single shared
     * neutral; per-eye IPD offsets survive the subtraction). */
    DuelVec3 mid = duel_v3((vl->position.x + vr->position.x) * 0.5f,
                           (vl->position.y + vr->position.y) * 0.5f,
                           (vl->position.z + vr->position.z) * 0.5f);
    DuelQuat lq = duel_q(vl->orientation.x, vl->orientation.y,
                         vl->orientation.z, vl->orientation.w);
    int reason = reset_edge ? DUEL_CAL_RESET : (r->cal_space ? DUEL_CAL_RECENTER : DUEL_CAL_INIT);
    duel_cal_capture(&r->cal, mid, lq, reason);
    /* P0.5 §8 discriminator proof: neutralize the capture pose itself --
     * corrected MUST read ~0 (CASE A if not: neutralization broken; CASE B
     * if 0 but the seat still wrong: anchor broken). Camera world proves
     * the eye lands on the ship (ship/cockpit never moved by calibration). */
    DuelVec3 cpos; DuelQuat cquat;
    duel_cal_neutralize(&r->cal, mid, lq, &cpos, &cquat);
    DuelVec3 cam_ship = duel_v3_add(DUEL_SEAT_POS, cpos);
    DuelVec3 cam_world = duel_v3_add(s->player.pos,
        duel_q_rot(s->player.orient, cam_ship));
    SDL_Log("DUEL_CAL gen=%d reason=%s raw_pos=(%.2f,%.2f,%.2f) raw_yaw=%.1fdeg "
            "corr_pos=(%.3f,%.3f,%.3f) corr_yaw=%.2fdeg seat=(%.2f,%.2f,%.2f) "
            "cam_ship=(%.2f,%.2f,%.2f) cam_world=(%.1f,%.1f,%.1f)",
            r->cal.generation,
            reason == DUEL_CAL_RESET ? "RESET" : (reason == DUEL_CAL_RECENTER ? "RECENTER" : "INIT"),
            mid.x, mid.y, mid.z, r->cal.neutral_yaw * 180.0f / (float)DUEL_PI,
            cpos.x, cpos.y, cpos.z, duel_yaw_of(cquat) * 180.0f / (float)DUEL_PI,
            DUEL_SEAT_POS.x, DUEL_SEAT_POS.y, DUEL_SEAT_POS.z,
            cam_ship.x, cam_ship.y, cam_ship.z,
            cam_world.x, cam_world.y, cam_world.z);
    r->cal_space = 0;
}

/* P0.5 §21: calibration generation/reason + last neutralized head pose
 * for the DUEL_CTRL line (rate-limited in main). */
void DuelRender_CalTelemetry(const DuelRender *r, int *generation, int *reason,
                             DuelVec3 *corr_pos, float *corr_yaw) {
    if (generation) *generation = r->cal.generation;
    if (reason) *reason = r->cal.reason;
    if (corr_pos) *corr_pos = r->last_corr_p;
    if (corr_yaw) *corr_yaw = r->last_corr_yaw;
}

/* P1.2: last world eye pose for the audio listener. */
void DuelRender_HeadTelemetry(const DuelRender *r, DuelQuat *q, DuelVec3 *p) {
    if (q) *q = r->last_eq;
    if (p) *p = r->last_ep;
}

/* M3.2: hand throttle ownership for the main loop (previous frame's
 * grab state + mids). M3.3 HOTAS: LEFT hand only owns the throttle.
 * M3.6: the quadrant HOLDER (either hand) owns it. */
int DuelRender_HandThrottle(const DuelRender *r, float *value) {
#if DUEL_VIRTUAL_HANDS
    int hq = r->grab_holder[DUEL_TGT_QUAD];
    if (hq >= 0 && r->grab_mid_valid[hq]) {
        if (value)
            *value = DuelQuad_ThrottleForMid(r->grab_mid[hq].z);
        return 1;
    }
    if (value) *value = 0.0f;
    return 0;
#else
    (void)r;
    if (value) *value = 0.0f;
    return 0;
#endif
}

/* M6: left-lever roll ownership for the main loop. Same contract as
 * the stick (held OR spring settling); the sim applies it to roll
 * only, independent of the stick's pitch/yaw. */
void DuelRender_SetHandSens(DuelRender *r, float sens) {
    if (!r) return;
    if (sens < 0.5f) sens = 0.5f;
    if (sens > 1.5f) sens = 1.5f;
    r->hand_sens = sens;
}

static float duel_sens_apply(const DuelRender *r, float v) {
    float s = r->hand_sens > 0.0f ? r->hand_sens : 1.0f;
    v *= s;
    if (v < -1.0f) v = -1.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

int DuelRender_HandLever(const DuelRender *r, float *ld) {
#if DUEL_VIRTUAL_HANDS
    int held = r->grab_holder[DUEL_TGT_QUAD] >= 0;
    int live = r->defl_ld != 0.0f;
    if (ld) *ld = duel_sens_apply(r, r->defl_ld);
    return (held || live) ? 1 : 0;
#else
    (void)r;
    if (ld) *ld = 0.0f;
    return 0;
#endif
}

/* M3.3: stick ownership for the main loop. Active while the HOLDER hand
 * (M3.6: either) holds OR the spring is still settling (smooth visual
 * + sim return). M6: pd = pitch, rd = PURE yaw (roll left the stick). */
int DuelRender_HandStick(const DuelRender *r, float *pd, float *rd) {
#if DUEL_VIRTUAL_HANDS
    int held = r->grab_holder[DUEL_TGT_STICK] >= 0;
    int live = r->defl_pd != 0.0f || r->defl_rd != 0.0f;
    if (pd) *pd = duel_sens_apply(r, r->defl_pd);
    if (rd) *rd = duel_sens_apply(r, r->defl_rd);
    return (held || live) ? 1 : 0;
#else
    (void)r;
    if (pd) *pd = 0.0f;
    if (rd) *rd = 0.0f;
    return 0;
#endif
}

/* M7: world-locked menu panel. Placed ONCE per screen by main (from
 * the head pose at transition); never follows the head afterwards. */
void DuelRender_SetMenuPanel(DuelRender *r, int show, const DuelMenu *m,
                             DuelVec3 center, DuelQuat quat) {
    if (!r) return;
    r->menu_show = show ? 1 : 0;
    r->menu = m;
    r->menu_center = center;
    r->menu_quat = quat;
    if (!show) { r->menu_hover = -1; r->menu_pinch[0] = r->menu_pinch[1] = 0; }
}

/* M7: hide the combat HUD (cue/diamond/lead/bars) behind menu screens.
 * Pause keeps it (frozen combat state stays informative). */
void DuelRender_SetCombatHud(DuelRender *r, int show) {
    if (!r) return;
    r->menu_hide_hud = show ? 0 : 1;
}

/* M7: drop every grab + deflection + fire latch (screen transitions
 * require a FRESH grip before the levers respond again). */
void DuelRender_ClearGrabs(DuelRender *r) {
    int i, t;
    if (!r) return;
    for (i = 0; i < 2; i++) {
        for (t = 0; t < 2; t++) DuelGrab_Reset(&r->grab[i][t]);
        r->grab_holder[i] = -1;
        r->stick_anchor_valid[i] = 0;
        r->lever_anchor_valid[i] = 0;
        r->grab_palm_valid[i] = 0;
        r->grab_mid_valid[i] = 0;
    }
    r->defl_pd = r->defl_rd = r->defl_ld = 0.0f;
    DuelFire_Reset(&r->fire_l);
    DuelFire_Reset(&r->fire_r);
}

/* M7 options RECENTRAR: same path as a Meta recenter (ship/cockpit
 * never move; the neutral head reference is recaptured). */
void DuelRender_RequestRecenter(DuelRender *r) {
    if (!r) return;
    SDL_Log("DUEL cal: manual recenter requested");
    r->cal.valid = 0;
    r->cal_space = 1;
    r->cal_settle = DUEL_CAL_SETTLE_FRAMES - DUEL_CAL_SPACE_SETTLE;
}

/* M7: hand-ray hover row + panel-local point + any-pinch level.
 * Computed during the eye pass (previous frame's data: same 1-frame
 * lag as the hand-lever publish). */
int DuelRender_MenuPointer(const DuelRender *r, int *row, float *x, float *y) {
    if (!r || !r->menu_show || !r->menu) return 0;
    if (row) *row = r->menu_hover;
    if (x) *x = r->menu_hx;
    if (y) *y = r->menu_hy;
    return (r->menu_pinch[0] || r->menu_pinch[1]) ? 1 : 0;
}

/* M3.4: 1 while a thumb trigger is pulled, else 0 (M3.7: either). */
int DuelRender_HandFire(const DuelRender *r) {
#if DUEL_VIRTUAL_HANDS
    return (r->fire_l.firing || r->fire_r.firing) ? 1 : 0;
#else
    (void)r;
    return 0;
#endif
}

/* M3.0: locate both hands once per frame (main loop, after Calibrate,
 * before the eye loop). No-op when trackers are unavailable. */
void DuelRender_HandsLocate(DuelRender *r, XrTime display_time) {
#if DUEL_VIRTUAL_HANDS
    if (r->hands_mask)
        DuelHands_Locate(DuelXr_Session(), DuelXr_Space(), display_time);
#else
    (void)r;
    (void)display_time;
#endif
}

#if DUEL_VIRTUAL_HANDS && DUEL_HAND_MESH
/* M3.5: skin the platform meshes into the staging area (world verts, 9
 * floats, opaque skin tone). Same seat path as the joints (Local2Game);
 * returns staged bytes (0 = nothing active, upload skipped). */
static Uint32 skin_hands(DuelRender *r, const DuelState *s) {
    float game16[16];
    int side, any = 0;
    if (!r->hand_stage) return 0;
    DuelHands_Local2Game(&r->cal, s->player.orient, s->player.pos,
                         DUEL_SEAT_POS, r->motion.off_p,
                         duel_motion_quat(&r->motion), game16);
    for (side = 0; side < 2; side++) {
        const DuelHand *h;
        const DuelHandMesh *m;
        DuelMat4 jm[26], gj[26];
        int jv[26], j, k, base;
        if (!r->hand_mesh[side]) { r->hand_show[side] = 0; r->hand_ok[side] = 0; continue; }
        h = DuelHands_Get(side);
        m = DuelHands_Mesh(side);
        /* M5 pop gate: palm(0)+wrist(1) anchor the skin; without them the
         * wrist-fallback verts teleport (the above-view flash). */
        if (h->active && m->have && h->j[0].pos_valid && h->j[1].pos_valid &&
                h->n_valid >= 6) {
            if (r->hand_ok[side] < 3) r->hand_ok[side]++;
            if (r->hand_ok[side] >= 3) r->hand_show[side] = 1;
        } else {
            r->hand_ok[side] = 0;
            r->hand_show[side] = 0;
        }
        if (!r->hand_show[side]) continue;
        any = 1;
        for (j = 0; j < 26; j++) jv[j] = h->j[j].pos_valid;
        DuelSkin_JointMats(h, m, jm);
        DuelSkin_GameMats(game16, jm, gj);
        base = r->hand_vbase[side];
        for (k = 0; k < m->n_verts; k++) {
            DuelVec3 w;
            float *d = r->hand_stage + (size_t)(base + k) * 9;
            DuelSkin_Vert(m->pos[k], m->bi + (size_t)k * 4,
                          m->bw + (size_t)k * 4, gj, jv, &w);
            d[0] = w.x; d[1] = w.y; d[2] = w.z;
            d[3] = 0.86f; d[4] = 0.64f; d[5] = 0.52f; d[6] = 1.0f;
            d[7] = 0.0f; d[8] = 0.0f;
        }
    }
    if (!any) return 0;
    return (Uint32)(r->hand_nv[0] + r->hand_nv[1]) * 9 * sizeof(float);
}
#endif

/* M7 menu text: stroke glyphs as world-space line pairs on the panel
 * plane. align: 0 left (x = left edge), 1 center, 2 right (x = edge).
 * size = meters per font unit; y = text BASELINE in panel-local. */
static void duel_menu_text(DynWriter *dw, DuelVec3 c, DuelVec3 px, DuelVec3 py,
                           const char *s, float x, float y, float size,
                           int align, float cr, float cg, float cb) {
    float w;
    const char *p;
    DuelFontSeg segs[DUEL_FONT_MAXSEGS];
    if (!s || !*s) return;
    w = DuelFont_TextW(s) * size;
    if (align == 1) x -= w * 0.5f;
    else if (align == 2) x -= w;
    for (p = s; *p; p++) {
        int n = DuelFont_Glyph(*p, segs, DUEL_FONT_MAXSEGS);
        int i;
        if (*p == ' ') { x += DUEL_FONT_ADV * size; continue; }
        for (i = 0; i < n; i++) {
            float *v = dyn_reserve(dw, 2);
            DuelVec3 a, b;
            if (!v) return;
            a = duel_v3_add(c, duel_v3_add(
                duel_v3_scale(px, x + (float)segs[i].x0 * size),
                duel_v3_scale(py, y + (float)segs[i].y0 * size)));
            b = duel_v3_add(c, duel_v3_add(
                duel_v3_scale(px, x + (float)segs[i].x1 * size),
                duel_v3_scale(py, y + (float)segs[i].y1 * size)));
            dyn_vert(v, a, cr, cg, cb, 1.0f);
            dyn_vert(v + 9, b, cr, cg, cb, 1.0f);
        }
        x += DUEL_FONT_ADV * size;
    }
}

/* Flat quad on the panel plane (2 tris, 6 verts) into a tri range. */
/* Slider fill: same label convention as duel_menu.c, normalized 0..1. */
static float duel_menu_selfrac(const DuelMenu *m, int row) {
    DuelMenuRow rows[DUEL_MENU_MAXROWS];
    int n = DuelMenu_Rows(m, rows, DUEL_MENU_MAXROWS);
    if (row < 0 || row >= n || rows[row].kind != DUEL_ROW_SLIDER) return 0.0f;
    {
        const char *lab = rows[row].label;
        if (!strcmp(lab, "VOLUMEN")) return m->vol_master;
        if (!strcmp(lab, "MUSICA")) return m->vol_music;
        if (!strcmp(lab, "EFECTOS")) return m->vol_sfx;
        if (!strcmp(lab, "SENSIBILIDAD")) return m->sens - 0.5f;
        if (!strcmp(lab, "ZONA MUERTA")) return (m->dead - 0.05f) / 0.25f;
    }
    return 0.0f;
}

static void duel_menu_quad(DynWriter *dw, DuelVec3 c, DuelVec3 px, DuelVec3 py,
                           float x0, float y0, float x1, float y1,
                           float cr, float cg, float cb, float ca) {
    float *d = dyn_reserve(dw, 6);
    DuelVec3 p0, p1, p2, p3;
    if (!d) return;
    p0 = duel_v3_add(c, duel_v3_add(duel_v3_scale(px, x0), duel_v3_scale(py, y0)));
    p1 = duel_v3_add(c, duel_v3_add(duel_v3_scale(px, x1), duel_v3_scale(py, y0)));
    p2 = duel_v3_add(c, duel_v3_add(duel_v3_scale(px, x1), duel_v3_scale(py, y1)));
    p3 = duel_v3_add(c, duel_v3_add(duel_v3_scale(px, x0), duel_v3_scale(py, y1)));
    dyn_vert(d + 0, p0, cr, cg, cb, ca); dyn_vert(d + 9, p1, cr, cg, cb, ca);
    dyn_vert(d + 18, p2, cr, cg, cb, ca); dyn_vert(d + 27, p0, cr, cg, cb, ca);
    dyn_vert(d + 36, p2, cr, cg, cb, ca); dyn_vert(d + 45, p3, cr, cg, cb, ca);
}

void DuelRender_Eye(DuelRender *r, SDL_GPUCommandBuffer *cmd,
                    SDL_GPUTexture *target, SDL_GPUTexture *depth,
                    int32_t w, int32_t h,
                    const XrPosef *hmd, const XrFovf *fov,
                    const DuelState *s) {
    /* camera: ship * seat * hmd_cal (exact closed form; the old
     * matrix->quat extraction snapped to identity past ~120 deg).
     * P0.4: the raw LOCAL pose carries meters of room-scale offset, so it
     * is neutralized into the seat frame first (no-op until calibrated). */
    DuelQuat hq = duel_q(hmd->orientation.x, hmd->orientation.y, hmd->orientation.z,
                         hmd->orientation.w);
    DuelVec3 hp = duel_v3(hmd->position.x, hmd->position.y, hmd->position.z);
    duel_cal_neutralize(&r->cal, hp, hq, &hp, &hq);
    r->last_corr_p = hp;
    r->last_corr_yaw = duel_yaw_of(hq);
    DuelQuat eq;
    DuelVec3 ep;
#if DUEL_VEHICLE_MOTION_FEEL
    {
        DuelQuat mo_q = duel_motion_quat(&r->motion);
        duel_eye_pose_motion(s->player.orient, s->player.pos, DUEL_SEAT_POS,
                             r->motion.off_p, mo_q, hq, hp, &eq, &ep);
    }
#else
    duel_eye_pose(s->player.orient, s->player.pos, DUEL_SEAT_POS, hq, hp, &eq, &ep);
#endif
    if (r->shake > 0.0f) {
        /* impact shake: small random rotation (~2 deg at full shake) */
        float ax = (float)duel_lcg(&r->shake_seed) / 4294967296.0f - 0.5f;
        float ay = (float)duel_lcg(&r->shake_seed) / 4294967296.0f - 0.5f;
        float az = (float)duel_lcg(&r->shake_seed) / 4294967296.0f - 0.5f;
        DuelVec3 axis = duel_v3(ax, ay, az);
        if (duel_v3_len(axis) < 1e-3f) axis = duel_v3(1.0f, 0.0f, 0.0f);
        axis = duel_v3_norm(axis);
        eq = duel_q_norm(duel_q_mul(
            duel_q_axis_angle(axis, r->shake * 0.035f), eq));
    }
    r->last_eq = eq;
    r->last_ep = ep;
    DuelMat4 view = duel_m4_rigid_inv(eq, ep);
    DuelMat4 proj = duel_frustum_xr(fov->angleLeft, fov->angleRight, fov->angleUp, fov->angleDown,
                                    0.05f, 30000.0f);
    DuelMat4 vp = duel_m4_mul(proj, view);
    DuelVec3 cam_pos = ep;
    DuelVec3 cam_fwd = duel_q_forward(eq);
    /* M7: the stored panel anchor is LOCAL (main placed it from the LOCAL
     * head pose); bring it to game space through the exact eye path, or
     * the panel lands in the wrong frame (user saw it underfoot). */
    DuelVec3 menu_c = r->menu_center;
    DuelQuat menu_q = r->menu_quat;
    if (r->menu_show && r->menu) {
        DuelVec3 mcp;
        DuelQuat mcq;
        duel_cal_neutralize(&r->cal, r->menu_center, r->menu_quat, &mcp, &mcq);
#if DUEL_VEHICLE_MOTION_FEEL
        {
            DuelQuat mo_q = duel_motion_quat(&r->motion);
            duel_eye_pose_motion(s->player.orient, s->player.pos, DUEL_SEAT_POS,
                                 r->motion.off_p, mo_q, mcq, mcp,
                                 &menu_q, &menu_c);
        }
#else
        duel_eye_pose(s->player.orient, s->player.pos, DUEL_SEAT_POS,
                      mcq, mcp, &menu_q, &menu_c);
#endif
    }

    SDL_GPUColorTargetInfo color;
    memset(&color, 0, sizeof color);
    color.texture = target;
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    color.clear_color.r = 0.0f; color.clear_color.g = 0.0f; color.clear_color.b = 0.0f; /* P1.1: true black space */
    color.clear_color.a = 1.0f;
    SDL_GPUDepthStencilTargetInfo ds;
    memset(&ds, 0, sizeof ds);
    ds.texture = depth;
    ds.load_op = SDL_GPU_LOADOP_CLEAR;
    ds.store_op = SDL_GPU_STOREOP_DONT_CARE;
    ds.clear_depth = 1.0f;
    ds.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    ds.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    ds.cycle = true; /* M1 known-good */
    /* ---- dynamic buffer content for this eye ---- */
    static float dyn[4096 * 9];
    DynWriter dw;
    dw.v = dyn; dw.count = 0; dw.cap = 4096;
    /* lasers: billboard quads as 2 triangles */
    struct { int first, count; } laser_range = { 0, 0 }, part_range = { 0, 0 },
        line_range = { 0, 0 }, hud_range = { 0, 0 }, flash_range = { 0, 0 },
        near_range = { 0, 0 }, mid_range = { 0, 0 }, hand_range = { 0, 0 },
        hbone_range = { 0, 0 }, quad_range = { 0, 0 },
        menu_tri_range = { 0, 0 }, menu_line_range = { 0, 0 };
    laser_range.first = dw.count;
    for (int i = 0; i < DUEL_LASER_POOL; i++) {
        const DuelLaser *l = &s->lasers[i];
        if (!l->active) continue;
        DuelVec3 axis = duel_v3_norm(l->vel);
        DuelVec3 to_cam = duel_v3_norm(duel_v3_sub(cam_pos, l->pos));
        DuelVec3 side = duel_v3_cross(axis, to_cam);
        if (duel_v3_len(side) < 1e-4f) side = duel_v3_cross(axis, cam_fwd);
        side = duel_v3_norm(side);
        /* P1.5: colored glow quad + white-hot core (XWA hook colors:
         * rebel FF3300, imperial 00FF33). Additive pipe. */
        float hl = 7.0f;
        float gr, gg, gb, cr, cg, cb;
        if (l->owner == DUEL_SHIP_PLAYER) {
            gr = 1.0f; gg = 0.20f; gb = 0.0f;
            cr = 1.0f; cg = 0.88f; cb = 0.72f;
        } else {
            gr = 0.0f; gg = 1.0f; gb = 0.20f;
            cr = 0.80f; cg = 1.0f; cb = 0.82f;
        }
        DuelVec3 a = duel_v3_sub(l->pos, duel_v3_scale(axis, hl));
        DuelVec3 b = duel_v3_add(l->pos, duel_v3_scale(axis, hl));
        for (int pass = 0; pass < 2; pass++) {
            float hw = pass == 0 ? 0.55f : 0.16f;
            float qr = pass == 0 ? gr * 0.55f : cr;
            float qg = pass == 0 ? gg * 0.55f : cg;
            float qb = pass == 0 ? gb * 0.55f : cb;
            DuelVec3 s3 = duel_v3_scale(side, hw);
            float *d = dyn_reserve(&dw, 6);
            if (!d) break;
            DuelVec3 p0 = duel_v3_sub(a, s3), p1 = duel_v3_add(a, s3);
            DuelVec3 p2 = duel_v3_add(b, s3), p3 = duel_v3_sub(b, s3);
            dyn_vert(d + 0, p0, qr, qg, qb, 1); dyn_vert(d + 9, p1, qr, qg, qb, 1);
            dyn_vert(d + 18, p2, qr, qg, qb, 1); dyn_vert(d + 27, p0, qr, qg, qb, 1);
            dyn_vert(d + 36, p2, qr, qg, qb, 1); dyn_vert(d + 45, p3, qr, qg, qb, 1);
        }
    }
    laser_range.count = dw.count - laser_range.first;
    /* particles as points */
    part_range.first = dw.count;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const DuelParticle *p = &r->parts[i];
        if (!p->active) continue;
        float *d = dyn_reserve(&dw, 1);
        if (!d) break;
        float a = p->life / p->max_life;
        dyn_vert(d, p->pos, p->r * a + 0.2f * a, p->g * a, p->b * a, 1.0f);
    }
    part_range.count = dw.count - part_range.first;
#if DUEL_SPACE_DEPTH
    /* M2.1: world depth points. Positions are the collected WORLD coords
     * (ship-keyed, HMD-free); only the fade uses eye distance. Drawn with
     * the normal world VP per eye (true stereo), depth-tested so the
     * cockpit occludes them. Subtle by design: felt, not seen. */
    near_range.first = dw.count;
    {
        int i;
        for (i = 0; i < r->depth_nn; i++) {
            const DuelDepthP *p = &r->depth_near[i];
            float d = duel_v3_len(duel_v3_sub(p->pos, cam_pos));
            float f = duel_depth_fade(DUEL_DEPTH_NEAR, d);
            float g = (0.10f + 0.12f * p->bright) * f;
            float *v = dyn_reserve(&dw, 1);
            if (!v) break;
#if DUEL_REALSPACE_DEBUG
            dyn_vert(v, p->pos, 0.0f, f, 0.0f, 1.0f);
#else
            if (p->type == 2) {
                float b = g * 1.6f > 0.5f ? 0.5f : g * 1.6f;
                dyn_vert(v, p->pos, b * 0.95f, b, b > 0.48f ? 0.5f : b * 1.05f, 1.0f);
            } else if (p->type == 1) {
                dyn_vert(v, p->pos, g, g * 0.92f, g * 0.82f, 1.0f);
            } else {
                dyn_vert(v, p->pos, g, g, g, 1.0f);
            }
#endif
        }
    }
    near_range.count = dw.count - near_range.first;
    mid_range.first = dw.count;
    {
        int i;
        for (i = 0; i < r->depth_nm; i++) {
            const DuelDepthP *p = &r->depth_mid[i];
            float d = duel_v3_len(duel_v3_sub(p->pos, cam_pos));
            float f = duel_depth_fade(DUEL_DEPTH_MID, d);
            float g = (0.05f + 0.09f * p->bright) * f;
            float *v = dyn_reserve(&dw, 1);
            if (!v) break;
#if DUEL_REALSPACE_DEBUG
            dyn_vert(v, p->pos, f, 0.0f, f, 1.0f);
#else
            if (p->type == 2) {
                float b = g * 1.6f > 0.4f ? 0.4f : g * 1.6f;
                dyn_vert(v, p->pos, b * 0.95f, b, b * 1.05f > 0.4f ? 0.4f : b * 1.05f, 1.0f);
            } else if (p->type == 1) {
                dyn_vert(v, p->pos, g, g * 0.92f, g * 0.82f, 1.0f);
            } else {
                dyn_vert(v, p->pos, g, g, g, 1.0f);
            }
#endif
        }
    }
    mid_range.count = dw.count - mid_range.first;
#endif
#if DUEL_VIRTUAL_HANDS
    /* M3.0: diagnostic virtual hands. Joints (points) + bones (line pairs)
     * in WORLD space through the shared seat path; only position-valid
     * joints emit. Left = cyan, right = amber; palms brighter. Both
     * ranges are depth-tested so the cockpit occludes them. */
    hand_range.first = dw.count;
    hbone_range.first = dw.count;
    if (r->hands_mask) {
        DuelQuat hmo = duel_motion_quat(&r->motion);
        static const float hcol[2][3] = { { 0.30f, 0.95f, 1.0f },
                                           { 1.0f, 0.68f, 0.20f } };
        static const float hpalm[2][3] = { { 0.85f, 1.0f, 1.0f },
                                            { 1.0f, 0.90f, 0.70f } };
        DuelVec3 hw[2][DUEL_HAND_JOINTS];
        int side;
        for (side = 0; side < 2; side++) {
            const DuelHand *h = DuelHands_Get(side);
            int k;
            if (!h->active) continue;
#if DUEL_HAND_MESH
            if (r->hand_mesh[side]) continue; /* platform mesh owns it */
#endif
            for (k = 0; k < DUEL_HAND_JOINTS; k++) {
                const DuelHandJoint *j = &h->j[k];
                float *v;
                const float *c;
                if (!j->pos_valid) continue;
                DuelHands_JointWorld(&r->cal, s->player.orient, s->player.pos,
                                     DUEL_SEAT_POS, r->motion.off_p, hmo,
                                     j, &hw[side][k]);
                v = dyn_reserve(&dw, 1);
                if (!v) break;
                c = (k == 0) ? hpalm[side] : hcol[side];
                dyn_vert(v, hw[side][k], c[0], c[1], c[2], 1.0f);
            }
        }
        hand_range.count = dw.count - hand_range.first;
        /* M7: menu hand-ray hover (index MCP->tip) + pinch levels. Both
         * eyes compute the same inputs: idempotent, no per-eye drift. */
        if (r->menu_show && r->menu) {
            DuelVec3 pax = duel_q_right(menu_q);
            DuelVec3 pay = duel_q_up(menu_q);
            DuelVec3 pn = duel_v3_scale(duel_q_forward(menu_q), -1.0f);
            float ph = DuelMenu_PanelH(r->menu);
            int order[2] = { 1, 0 }; /* right hand wins ties */
            int o;
            r->menu_hover = -1;
            for (o = 0; o < 2; o++) {
                int sd = order[o];
                const DuelHand *h = DuelHands_Get(sd);
                float pd = r->grab_pinch[sd];
                /* Pinch level with hysteresis (click = make edge in main). */
                if (pd < 0.035f) r->menu_pinch[sd] = 1;
                else if (pd > 0.050f) r->menu_pinch[sd] = 0;
                r->menu_ray_on[sd] = 0;
                if (!h->active) continue;
                if (!h->j[6].pos_valid || !h->j[10].pos_valid) continue;
                {
                    /* NOT hw[][]: with platform meshes the joint loop
                     * above is skipped, leaving hw uninitialized. */
                    DuelVec3 tip, mcp;
                    DuelVec3 d;
                    DuelHands_JointWorld(&r->cal, s->player.orient,
                                         s->player.pos, DUEL_SEAT_POS,
                                         r->motion.off_p, hmo, &h->j[10],
                                         &tip);
                    DuelHands_JointWorld(&r->cal, s->player.orient,
                                         s->player.pos, DUEL_SEAT_POS,
                                         r->motion.off_p, hmo, &h->j[6],
                                         &mcp);
                    d = duel_v3_sub(tip, mcp);
                    float dl = duel_v3_len(d);
                    float denom, t;
                    if (dl < 1e-6f) continue;
                    d = duel_v3_scale(d, 1.0f / dl);
                    r->menu_ray_on[sd] = 1;
                    r->menu_ray_o[sd] = tip;
                    r->menu_ray_e[sd] = duel_v3_add(tip, duel_v3_scale(d, 1.5f));
                    denom = duel_v3_dot(d, pn);
                    if (fabsf(denom) < 1e-6f) continue;
                    t = duel_v3_dot(duel_v3_sub(menu_c, tip), pn) / denom;
                    if (t < 0.0f) continue;
                    {
                        DuelVec3 hit = duel_v3_add(tip, duel_v3_scale(d, t));
                        DuelVec3 rel = duel_v3_sub(hit, menu_c);
                        float lx = duel_v3_dot(rel, pax);
                        float ly = duel_v3_dot(rel, pay);
                        if (fabsf(lx) > DUEL_MENU_PANEL_W * 0.5f ||
                            fabsf(ly) > ph * 0.5f) continue;
                        r->menu_ray_e[sd] = hit;
                        if (r->menu_hover < 0) {
                            r->menu_hover = DuelMenu_HitRow(r->menu, lx, ly);
                            r->menu_hx = lx;
                            r->menu_hy = ly;
                        }
                    }
                }
            }
        } else {
            r->menu_hover = -1;
            r->menu_pinch[0] = r->menu_pinch[1] = 0;
            r->menu_ray_on[0] = r->menu_ray_on[1] = 0;
        }
        /* bones start where joints ended */
        hbone_range.first = dw.count;
        for (side = 0; side < 2; side++) {
            const DuelHand *h = DuelHands_Get(side);
            int b;
            if (!h->active) continue;
#if DUEL_HAND_MESH
            if (r->hand_mesh[side]) continue; /* platform mesh owns it */
#endif
            for (b = 0; b < DUEL_HAND_BONES; b++) {
                int a, z;
                float *v;
                DuelHands_Bone(b, &a, &z);
                if (!h->j[a].pos_valid || !h->j[z].pos_valid) continue;
                v = dyn_reserve(&dw, 2);
                if (!v) break;
                dyn_vert(v, hw[side][a], hcol[side][0], hcol[side][1],
                         hcol[side][2], 0.95f);
                dyn_vert(v + 9, hw[side][z], hcol[side][0], hcol[side][1],
                         hcol[side][2], 0.95f);
            }
        }
#if DUEL_GRAB_MARKERS
        /* M3.1: grip markers (camera-facing diamonds) + pinch tethers.
         * FAR = faint locator, NEAR = green, HELD = orange.
         * M3.3 HOTAS: one marker per side (LEFT quadrant, RIGHT stick),
         * each tracking its moved target. Depth-tested with the bones.
         * M3.6 free grab: markers are PER TARGET (0 quad, 1 stick);
         * the state follows the HOLDER, else the nearest NEAR hand. */
        {
            DuelVec3 tgtw[2];
            DuelVec3 crt = duel_q_right(eq), cup = duel_q_up(eq);
            int mside;
            {
                DuelVec3 g0, g1;
                DuelLever_GripShip(s->player.throttle, r->defl_ld, &g0);
                DuelStick_GripShip(r->defl_pd, r->defl_rd, &g1);
                tgtw[0] = duel_v3_add(s->player.pos,
                    duel_q_rot(s->player.orient, g0));
                tgtw[1] = duel_v3_add(s->player.pos,
                    duel_q_rot(s->player.orient, g1));
            }
            for (mside = 0; mside < 2; mside++) {
                int holder = r->grab_holder[mside];
                int th = holder, hh;
                int st;
                int firing;
                if (holder >= 0) {
                    st = (int)r->grab[holder][mside].state;
                } else {
                    st = DUEL_GRAB_FAR;
                    for (hh = 0; hh < 2; hh++) {
                        int ok = mside == DUEL_TGT_STICK ?
                            r->grab_palm_valid[hh] : r->grab_mid_valid[hh];
                        if (r->grab[hh][mside].state == DUEL_GRAB_NEAR &&
                            ok) {
                            st = DUEL_GRAB_NEAR;
                            th = hh;
                            break;
                        }
                    }
                }
                firing = mside == DUEL_TGT_STICK &&
                    (r->fire_l.firing || r->fire_r.firing);
                float mr = st == DUEL_GRAB_HELD ? 0.070f :
                    (st == DUEL_GRAB_NEAR ? 0.060f : 0.045f);
                float mcr = firing ? 1.0f : st == DUEL_GRAB_HELD ? 1.0f :
                    (st == DUEL_GRAB_NEAR ? 0.25f : 0.30f);
                float mcg = firing ? 0.12f : st == DUEL_GRAB_HELD ? 0.55f :
                    (st == DUEL_GRAB_NEAR ? 1.0f : 0.30f);
                float mcb = firing ? 0.08f : st == DUEL_GRAB_HELD ? 0.10f :
                    (st == DUEL_GRAB_NEAR ? 0.35f : 0.32f);
                float mca = st == DUEL_GRAB_FAR ? 0.45f : 0.95f;
                float *md = dyn_reserve(&dw, 8);
                DuelVec3 midw;
                float *tv;
                if (md) {
                    DuelVec3 m0 = duel_v3_add(tgtw[mside], duel_v3_scale(cup, mr));
                    DuelVec3 m1 = duel_v3_add(tgtw[mside], duel_v3_scale(crt, mr));
                    DuelVec3 m2 = duel_v3_sub(tgtw[mside], duel_v3_scale(cup, mr));
                    DuelVec3 m3 = duel_v3_sub(tgtw[mside], duel_v3_scale(crt, mr));
                    dyn_vert(md + 0, m0, mcr, mcg, mcb, mca);
                    dyn_vert(md + 9, m1, mcr, mcg, mcb, mca);
                    dyn_vert(md + 18, m1, mcr, mcg, mcb, mca);
                    dyn_vert(md + 27, m2, mcr, mcg, mcb, mca);
                    dyn_vert(md + 36, m2, mcr, mcg, mcb, mca);
                    dyn_vert(md + 45, m3, mcr, mcg, mcb, mca);
                    dyn_vert(md + 54, m3, mcr, mcg, mcb, mca);
                    dyn_vert(md + 63, m0, mcr, mcg, mcb, mca);
                }
                /* M3.6: tether from the holder's (or NEAR hand's) stored
                 * ship-frame point to the target (ship -> world).
                 * M3.7: palm for the stick, pinch-mid for the quad. */
                if (st == DUEL_GRAB_FAR || th < 0) continue;
                {
                    DuelVec3 src;
                    if (mside == DUEL_TGT_STICK) {
                        if (!r->grab_palm_valid[th]) continue;
                        src = r->grab_palm[th];
                    } else {
                        if (!r->grab_mid_valid[th]) continue;
                        src = r->grab_mid[th];
                    }
                    midw = duel_v3_add(s->player.pos,
                        duel_q_rot(s->player.orient, src));
                }
                tv = dyn_reserve(&dw, 2);
                if (!tv) break;
                dyn_vert(tv, midw, mcr, mcg, mcb, 0.9f);
                dyn_vert(tv + 9, tgtw[mside], mcr, mcg, mcb, 0.9f);
            }
        }
#endif
    }
    hbone_range.count = dw.count - hbone_range.first;
#if DUEL_VIRTUAL_HANDS
    /* M3.3: throttle quadrant handle sliding along the left slot
     * with the sim throttle (a physical gauge even without hands).
     * M6: reshaped into a lever grip that also swings with roll. */
    quad_range.first = dw.count;
    {
        static const int faces[36] = {
            0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6,
            0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2,
            0, 4, 5, 0, 5, 1, 3, 2, 6, 3, 6, 7
        };
        DuelVec3 qc;
        DuelVec3 c[8];
        float *qv = dyn_reserve(&dw, 36);
        int vi;
        /* M6: the quadrant box becomes a lever handle: a slim vertical
         * grip riding the slot (throttle) and swinging sideways with
         * roll, same gray physical material (not a marker). */
        DuelLever_GripShip(s->player.throttle, r->defl_ld, &qc);
        c[0] = duel_v3(qc.x - 0.020f, qc.y - 0.055f, qc.z - 0.020f);
        c[1] = duel_v3(qc.x + 0.020f, qc.y - 0.055f, qc.z - 0.020f);
        c[2] = duel_v3(qc.x + 0.020f, qc.y + 0.055f, qc.z - 0.020f);
        c[3] = duel_v3(qc.x - 0.020f, qc.y + 0.055f, qc.z - 0.020f);
        c[4] = duel_v3(qc.x - 0.020f, qc.y - 0.055f, qc.z + 0.020f);
        c[5] = duel_v3(qc.x + 0.020f, qc.y - 0.055f, qc.z + 0.020f);
        c[6] = duel_v3(qc.x + 0.020f, qc.y + 0.055f, qc.z + 0.020f);
        c[7] = duel_v3(qc.x - 0.020f, qc.y + 0.055f, qc.z + 0.020f);
        for (vi = 0; vi < 8; vi++)
            c[vi] = duel_v3_add(s->player.pos,
                duel_q_rot(s->player.orient, c[vi]));
        if (qv) {
            int fi;
            for (fi = 0; fi < 36; fi++) {
                int top = fi >= 30; /* last face (+Y): lighter cap */
                float g = top ? 0.45f : 0.22f;
                dyn_vert(qv + fi * 9, c[faces[fi]], g, g, g + 0.02f, 1.0f);
            }
        }
    }
    quad_range.count = dw.count - quad_range.first;
#endif
#endif
    /* M7 menu panel (world-locked; hands AND controllers). Two pure
     * passes: tris (bg/rows/sliders/border) then glyph/ray lines.
     * Pipes need pure slices, so text runs in a second loop. */
    menu_tri_range.first = dw.count;
    menu_line_range.first = dw.count;
    if (r->menu_show && r->menu) {
        DuelVec3 pax = duel_q_right(menu_q);
        DuelVec3 pay = duel_q_up(menu_q);
        DuelVec3 cc = duel_v3_add(menu_c,
            duel_v3_scale(duel_q_forward(menu_q), -0.002f));
        float ph = DuelMenu_PanelH(r->menu);
        float pw = DUEL_MENU_PANEL_W;
        DuelMenuRow rows[DUEL_MENU_MAXROWS];
        int n = DuelMenu_Rows(r->menu, rows, DUEL_MENU_MAXROWS);
        int i, hl = r->menu_hover >= 0 ? r->menu_hover : r->menu->sel;
        float bw = 0.006f;
        duel_menu_quad(&dw, cc, pax, pay, -pw * 0.5f, -ph * 0.5f,
                       pw * 0.5f, ph * 0.5f, 0.015f, 0.020f, 0.045f, 0.93f);
        duel_menu_quad(&dw, cc, pax, pay, -pw * 0.5f, ph * 0.5f - bw,
                       pw * 0.5f, ph * 0.5f, 1.0f, 0.72f, 0.25f, 1.0f);
        duel_menu_quad(&dw, cc, pax, pay, -pw * 0.5f, -ph * 0.5f,
                       pw * 0.5f, -ph * 0.5f + bw, 1.0f, 0.72f, 0.25f, 1.0f);
        duel_menu_quad(&dw, cc, pax, pay, -pw * 0.5f, -ph * 0.5f,
                       -pw * 0.5f + bw, ph * 0.5f, 1.0f, 0.72f, 0.25f, 1.0f);
        duel_menu_quad(&dw, cc, pax, pay, pw * 0.5f - bw, -ph * 0.5f,
                       pw * 0.5f, ph * 0.5f, 1.0f, 0.72f, 0.25f, 1.0f);
        for (i = 0; i < n; i++) {
            float x0, y0, x1, y1;
            if (rows[i].kind == DUEL_ROW_LABEL) continue;
            if (!DuelMenu_RowRect(r->menu, i, &x0, &y0, &x1, &y1)) continue;
            if (i == hl)
                duel_menu_quad(&dw, cc, pax, pay, x0, y0, x1, y1,
                               0.10f, 0.32f, 0.42f, 0.90f);
            else
                duel_menu_quad(&dw, cc, pax, pay, x0, y0, x1, y1,
                               0.05f, 0.07f, 0.12f, 0.90f);
            if (rows[i].kind == DUEL_ROW_SLIDER) {
                float fy = y0 + 0.018f;
                float fx = x0 + 0.03f + (x1 - x0 - 0.06f) *
                    duel_menu_selfrac(r->menu, i);
                duel_menu_quad(&dw, cc, pax, pay, x0 + 0.03f, fy,
                               x1 - 0.03f, fy + 0.008f,
                               0.20f, 0.22f, 0.30f, 1.0f);
                duel_menu_quad(&dw, cc, pax, pay, x0 + 0.03f, fy, fx,
                               fy + 0.008f, 0.30f, 0.85f, 1.0f, 1.0f);
            }
        }
    }
    menu_tri_range.count = dw.count - menu_tri_range.first;
    menu_line_range.first = dw.count;
    if (r->menu_show && r->menu) {
        DuelVec3 pax = duel_q_right(menu_q);
        DuelVec3 pay = duel_q_up(menu_q);
        DuelVec3 cc = duel_v3_add(menu_c,
            duel_v3_scale(duel_q_forward(menu_q), -0.004f));
        float ph = DuelMenu_PanelH(r->menu);
        DuelMenuRow rows[DUEL_MENU_MAXROWS];
        int n = DuelMenu_Rows(r->menu, rows, DUEL_MENU_MAXROWS);
        float tsz = 0.0121f, rsz = 0.0072f, hsz = 0.0050f;
        int i;
        float ty = ph * 0.5f - DUEL_MENU_PAD - DUEL_MENU_TITLE_H * 0.5f -
            3.5f * tsz;
        duel_menu_text(&dw, cc, pax, pay, DuelMenu_Title(r->menu), 0.0f,
                       ty, tsz, 1, 1.0f, 0.72f, 0.25f);
        if (r->menu->state == DUEL_APP_RESULT) {
            char kb[24];
            float ky = ph * 0.5f - DUEL_MENU_PAD - DUEL_MENU_TITLE_H -
                DUEL_MENU_ROW_GAP - (DUEL_MENU_ROW_H * 0.5f + 3.5f * rsz);
            snprintf(kb, sizeof(kb), "BAJAS: %d", r->menu->result_kills);
            duel_menu_text(&dw, cc, pax, pay, kb, 0.0f, ky, rsz, 1,
                           0.85f, 0.88f, 0.95f);
        }
        for (i = 0; i < n; i++) {
            float top = ph * 0.5f - DUEL_MENU_PAD - DUEL_MENU_TITLE_H -
                DUEL_MENU_ROW_GAP - (float)i *
                (DUEL_MENU_ROW_H + DUEL_MENU_ROW_GAP);
            float by;
            if (r->menu->state == DUEL_APP_RESULT)
                top -= DUEL_MENU_ROW_H + DUEL_MENU_ROW_GAP;
            by = top - DUEL_MENU_ROW_H * 0.5f - 3.5f * rsz;
            if (rows[i].kind == DUEL_ROW_LABEL) {
                duel_menu_text(&dw, cc, pax, pay, rows[i].label, 0.0f, by,
                               rsz, 1, 0.55f, 0.58f, 0.65f);
                continue;
            }
            {
                float x0, y0, x1, y1;
                char val[16];
                if (!DuelMenu_RowRect(r->menu, i, &x0, &y0, &x1, &y1)) continue;
                by = y0 + (DUEL_MENU_ROW_H - 7.0f * rsz) * 0.5f;
                duel_menu_text(&dw, cc, pax, pay, rows[i].label, x0 + 0.03f,
                               by, rsz, 0, 0.85f, 0.88f, 0.95f);
                DuelMenu_RowValue(r->menu, i, val, sizeof(val));
                if (val[0])
                    duel_menu_text(&dw, cc, pax, pay, val, x1 - 0.03f, by,
                                   rsz, 2, 0.65f, 0.95f, 1.0f);
            }
        }
        duel_menu_text(&dw, cc, pax, pay, "APUNTA + PELLIZCA / GATILLO",
                       0.0f, ph * -0.5f - 0.055f, hsz, 1,
                       0.55f, 0.60f, 0.68f);
        for (i = 0; i < 2; i++) {
            float *v;
            if (!r->menu_ray_on[i]) continue;
            v = dyn_reserve(&dw, 2);
            if (!v) break;
            dyn_vert(v, r->menu_ray_o[i], 0.3f, 0.9f, 1.0f, 0.9f);
            dyn_vert(v + 9, r->menu_ray_e[i], 0.3f, 0.9f, 1.0f, 0.15f);
        }
    }
    menu_line_range.count = dw.count - menu_line_range.first;
    /* reticle: XWA-style gunsight (cross + circle + center dot) collimated
     * at 120 m on the SHIP forward axis. P0.2f: the 8 m v1 sat 6.3 deg off
     * the bolt direction (eye 0.88 m above the ship axis); at 120 m the
     * parallax is 0.4 deg -- the sight finally points where bolts go.
     * Sizes x15 preserve the v1 angular size. Frame = ship right/up
     * (roll-aligned); GPU frustum-clips when looking away. */
    line_range.first = dw.count;
    {
        DuelVec3 sf = duel_q_forward(s->player.orient);
        DuelVec3 c = duel_v3_add(s->player.pos, duel_v3_scale(sf, 120.0f));
        DuelVec3 cr = duel_q_right(s->player.orient), cu = duel_q_up(s->player.orient);
        /* x0.6 user order (-40%): was rs 5.25, gap 1.80.
         * M2.2b: x0.9 more (-10% diameter). */
        float rs = 2.835f, g = 0.972f;
        DuelVec3 pts[8] = {
            duel_v3_sub(c, duel_v3_scale(cr, rs)), duel_v3_sub(c, duel_v3_scale(cr, g)),
            duel_v3_add(c, duel_v3_scale(cr, g)), duel_v3_add(c, duel_v3_scale(cr, rs)),
            duel_v3_sub(c, duel_v3_scale(cu, rs)), duel_v3_sub(c, duel_v3_scale(cu, g)),
            duel_v3_add(c, duel_v3_scale(cu, g)), duel_v3_add(c, duel_v3_scale(cu, rs)),
        };
        float *d = dyn_reserve(&dw, 8);
        if (d) for (int i = 0; i < 8; i++) dyn_vert(d + i * 9, pts[i], 0.2f, 1.0f, 0.3f, 0.9f);
        /* circle */
        const int segs = 20;
        float *dc = dyn_reserve(&dw, segs * 2);
        if (dc) {
            for (int i = 0; i < segs; i++) {
                float a0 = (float)i / segs * 2.0f * (float)DUEL_PI;
                float a1 = (float)(i + 1) / segs * 2.0f * (float)DUEL_PI;
                DuelVec3 p0 = duel_v3_add(c, duel_v3_add(duel_v3_scale(cr, cosf(a0) * 4.455f),
                                                         duel_v3_scale(cu, sinf(a0) * 4.455f)));
                DuelVec3 p1 = duel_v3_add(c, duel_v3_add(duel_v3_scale(cr, cosf(a1) * 4.455f),
                                                         duel_v3_scale(cu, sinf(a1) * 4.455f)));
                dyn_vert(dc + (i * 2) * 9, p0, 0.2f, 1.0f, 0.3f, 0.9f);
                dyn_vert(dc + (i * 2 + 1) * 9, p1, 0.2f, 1.0f, 0.3f, 0.9f);
            }
        }
        /* center dot: small diamond at the exact aim point */
        {
            float *dd0 = dyn_reserve(&dw, 8);
            if (dd0) {
                DuelVec3 t = duel_v3_add(c, duel_v3_scale(cu, 0.486f));
                DuelVec3 r = duel_v3_add(c, duel_v3_scale(cr, 0.486f));
                DuelVec3 b = duel_v3_sub(c, duel_v3_scale(cu, 0.486f));
                DuelVec3 l = duel_v3_sub(c, duel_v3_scale(cr, 0.486f));
                dyn_vert(dd0 + 0, t, 0.2f, 1.0f, 0.3f, 0.9f); dyn_vert(dd0 + 9, r, 0.2f, 1.0f, 0.3f, 0.9f);
                dyn_vert(dd0 + 18, r, 0.2f, 1.0f, 0.3f, 0.9f); dyn_vert(dd0 + 27, b, 0.2f, 1.0f, 0.3f, 0.9f);
                dyn_vert(dd0 + 36, b, 0.2f, 1.0f, 0.3f, 0.9f); dyn_vert(dd0 + 45, l, 0.2f, 1.0f, 0.3f, 0.9f);
                dyn_vert(dd0 + 54, l, 0.2f, 1.0f, 0.3f, 0.9f); dyn_vert(dd0 + 63, t, 0.2f, 1.0f, 0.3f, 0.9f);
            }
        }
        /* M3.8b head look-cue (replaces the M2.2c ship chevron per user
         * order): amber arrowhead riding 2 m in front of the HEAD,
         * pointing in the head plane toward the TIE. Turn the HEAD
         * toward it; hidden inside the 12-deg view cone (you're looking
         * at it: red diamond + green lead pip cover it). No-depth HUD
         * lines: always visible, wherever you look. */
        /* M5: auto-target = nearest living tie; cue + diamonds + lead
         * pip all follow it. */
        int tgt = Duel_AutoTarget(s);
        const DuelShip *tp = tgt >= 0 ? &s->ties[tgt] : NULL;
        if (tp) {
            float gdx = 0.0f, gdy = 0.0f;
            if (Duel_HeadGuide(eq, cam_pos, tp->pos, &gdx, &gdy)) {
                DuelVec3 hr = duel_q_right(eq), hu = duel_q_up(eq);
                DuelVec3 hf = duel_q_forward(eq);
                DuelVec3 hc = duel_v3_add(cam_pos, duel_v3_add(
                    duel_v3_scale(hf, 2.0f), duel_v3_scale(hu, -0.18f)));
                DuelVec3 gd = duel_v3_add(duel_v3_scale(hr, gdx),
                                          duel_v3_scale(hu, gdy));
                DuelVec3 gp = duel_v3_add(duel_v3_scale(hr, -gdy),
                                          duel_v3_scale(hu, gdx));
                DuelVec3 tip = duel_v3_add(hc, duel_v3_scale(gd, 0.14f));
                DuelVec3 wl = duel_v3_add(duel_v3_add(hc, duel_v3_scale(gd, 0.07f)),
                                          duel_v3_scale(gp, 0.05f));
                DuelVec3 wr = duel_v3_sub(duel_v3_add(hc, duel_v3_scale(gd, 0.07f)),
                                          duel_v3_scale(gp, 0.05f));
                float *ga = dyn_reserve(&dw, 4);
                if (ga) {
                    dyn_vert(ga + 0, wl, 1.0f, 0.6f, 0.1f, 0.9f);
                    dyn_vert(ga + 9, tip, 1.0f, 0.6f, 0.1f, 0.9f);
                    dyn_vert(ga + 18, tip, 1.0f, 0.6f, 0.1f, 0.9f);
                    dyn_vert(ga + 27, wr, 1.0f, 0.6f, 0.1f, 0.9f);
                }
            }
        }
        /* target diamond billboard at TIE */
        if (tp) {
            DuelVec3 to_t = duel_v3_sub(tp->pos, cam_pos);
            float dist = duel_v3_len(to_t);
            DuelVec3 fw = duel_v3_scale(to_t, 1.0f / (dist > 1e-3f ? dist : 1.0f));
            DuelVec3 rt = duel_v3_norm(duel_v3_cross(fw, duel_v3(0, 1, 0)));
            DuelVec3 up = duel_v3_cross(rt, fw);
            float R = 8.0f;
            DuelVec3 d0 = duel_v3_add(tp->pos, duel_v3_scale(up, R));
            DuelVec3 d1 = duel_v3_add(tp->pos, duel_v3_scale(rt, R));
            DuelVec3 d2 = duel_v3_sub(tp->pos, duel_v3_scale(up, R));
            DuelVec3 d3 = duel_v3_sub(tp->pos, duel_v3_scale(rt, R));
            float *dd = dyn_reserve(&dw, 8);
            if (dd) {
                dyn_vert(dd + 0, d0, 1, 0.2f, 0.2f, 0.9f); dyn_vert(dd + 9, d1, 1, 0.2f, 0.2f, 0.9f);
                dyn_vert(dd + 18, d1, 1, 0.2f, 0.2f, 0.9f); dyn_vert(dd + 27, d2, 1, 0.2f, 0.2f, 0.9f);
                dyn_vert(dd + 36, d2, 1, 0.2f, 0.2f, 0.9f); dyn_vert(dd + 45, d3, 1, 0.2f, 0.2f, 0.9f);
                dyn_vert(dd + 54, d3, 1, 0.2f, 0.2f, 0.9f); dyn_vert(dd + 63, d0, 1, 0.2f, 0.2f, 0.9f);
            }
            /* lead pip: green diamond where to aim so bolts connect (XWA
             * lead computer). Same billboard frame as the red box. */
            DuelVec3 lead = duel_lead_point(&s->player, tp, DUEL_PLAYER_LASER_SPEED);
            float RL = 4.0f;
            DuelVec3 l0 = duel_v3_add(lead, duel_v3_scale(up, RL));
            DuelVec3 l1 = duel_v3_add(lead, duel_v3_scale(rt, RL));
            DuelVec3 l2 = duel_v3_sub(lead, duel_v3_scale(up, RL));
            DuelVec3 l3 = duel_v3_sub(lead, duel_v3_scale(rt, RL));
            float *dl = dyn_reserve(&dw, 8);
            if (dl) {
                dyn_vert(dl + 0, l0, 0.2f, 1.0f, 0.3f, 0.9f); dyn_vert(dl + 9, l1, 0.2f, 1.0f, 0.3f, 0.9f);
                dyn_vert(dl + 18, l1, 0.2f, 1.0f, 0.3f, 0.9f); dyn_vert(dl + 27, l2, 0.2f, 1.0f, 0.3f, 0.9f);
                dyn_vert(dl + 36, l2, 0.2f, 1.0f, 0.3f, 0.9f); dyn_vert(dl + 45, l3, 0.2f, 1.0f, 0.3f, 0.9f);
                dyn_vert(dl + 54, l3, 0.2f, 1.0f, 0.3f, 0.9f); dyn_vert(dl + 63, l0, 0.2f, 1.0f, 0.3f, 0.9f);
            }
            (void)dist;
        }
    }
    line_range.count = dw.count - line_range.first;
    /* HUD bars in NDC (identity MVP used at draw) */
    hud_range.first = dw.count;
    {
        float ph = s->player.hp / DUEL_PLAYER_HP;
        /* M5: right bar follows the auto-target (nearest living tie). */
        int htgt = Duel_AutoTarget(s);
        float th = htgt >= 0 ? s->ties[htgt].hp / DUEL_TIE_HP : 0.0f;
        /* bg + fg for player (left) and tie (right) */
        struct { float x0, x1, frac, r, g, b; } bars[2] = {
            { -0.9f, -0.08f, ph, 0.2f, 1.0f, 0.3f },
            { 0.08f, 0.9f, th, 1.0f, 0.25f, 0.2f },
        };
        for (int i = 0; i < 2; i++) {
            float y0 = -0.95f, y1 = -0.90f;
            float xm = bars[i].x0 + (bars[i].x1 - bars[i].x0) * bars[i].frac;
            float *d = dyn_reserve(&dw, 12);
            if (!d) break;
            /* bg quad */
            dyn_vert(d + 0, duel_v3(bars[i].x0, y0, 0), 0.1f, 0.1f, 0.1f, 0.7f);
            dyn_vert(d + 9, duel_v3(bars[i].x1, y0, 0), 0.1f, 0.1f, 0.1f, 0.7f);
            dyn_vert(d + 18, duel_v3(bars[i].x1, y1, 0), 0.1f, 0.1f, 0.1f, 0.7f);
            dyn_vert(d + 27, duel_v3(bars[i].x0, y0, 0), 0.1f, 0.1f, 0.1f, 0.7f);
            dyn_vert(d + 36, duel_v3(bars[i].x1, y1, 0), 0.1f, 0.1f, 0.1f, 0.7f);
            dyn_vert(d + 45, duel_v3(bars[i].x0, y1, 0), 0.1f, 0.1f, 0.1f, 0.7f);
            /* fg quad */
            dyn_vert(d + 54, duel_v3(bars[i].x0, y0, 0), bars[i].r, bars[i].g, bars[i].b, 0.9f);
            dyn_vert(d + 63, duel_v3(xm, y0, 0), bars[i].r, bars[i].g, bars[i].b, 0.9f);
            dyn_vert(d + 72, duel_v3(xm, y1, 0), bars[i].r, bars[i].g, bars[i].b, 0.9f);
            dyn_vert(d + 81, duel_v3(bars[i].x0, y0, 0), bars[i].r, bars[i].g, bars[i].b, 0.9f);
            dyn_vert(d + 90, duel_v3(xm, y1, 0), bars[i].r, bars[i].g, bars[i].b, 0.9f);
            dyn_vert(d + 99, duel_v3(bars[i].x0, y1, 0), bars[i].r, bars[i].g, bars[i].b, 0.9f);
        }
    }
    hud_range.count = dw.count - hud_range.first;
    /* explosion flash fullscreen */
    flash_range.first = dw.count;
    if (r->flash > 0.01f) {
        float *d = dyn_reserve(&dw, 3);
        if (d) {
            float a = r->flash * 0.55f;
            dyn_vert(d + 0, duel_v3(-1, -1, 0), 1.0f, 0.5f, 0.15f, a);
            dyn_vert(d + 9, duel_v3(3, -1, 0), 1.0f, 0.5f, 0.15f, a);
            dyn_vert(d + 18, duel_v3(-1, 3, 0), 1.0f, 0.5f, 0.15f, a);
        }
    }
    flash_range.count = dw.count - flash_range.first;

    /* P1.5: authentic sprite billboards (one-shot slots + fixed sun).
     * Quads face the camera; world positions come from sim/render state,
     * never from the HMD beyond the camera basis (same as lasers). */
    static float spr[(DUEL_SPR_SLOTS + 1) * DUEL_SPR_QUAD_FLOATS];
    struct { int first; int tex; float bright; } spr_draw[DUEL_SPR_SLOTS + 1];
    int spr_count = 0;
    {
        DuelVec3 basis_r = duel_q_rot(eq, duel_v3(1.0f, 0.0f, 0.0f));
        DuelVec3 basis_u = duel_q_rot(eq, duel_v3(0.0f, 1.0f, 0.0f));
        DuelVec3 face = duel_q_rot(eq, duel_v3(0.0f, 0.0f, 1.0f));
        int i;
        for (i = 0; i < DUEL_SPR_SLOTS; i++) {
            const DuelSprite *sp = &r->sprites[i];
            float t, size, bright;
            int fr, tex;
            if (!sp->active) continue;
            fr = duel_sprite_frame(sp->age, sp->dur, sp->nframes);
            tex = sp->tex_base + fr;
            if (tex < 0 || tex >= DUEL_SPR_TEX_COUNT) continue;
            if (!r->spr_tex[tex]) continue;
            t = duel_sprite_mix(sp->age, sp->dur);
            size = sp->size0 + (sp->size1 - sp->size0) * t;
            bright = sp->bright0 + (sp->bright1 - sp->bright0) * t;
            duel_sprite_quad(&sp->pos, &basis_r, &basis_u, size, size,
                             &face, 0.0f, spr + spr_count * DUEL_SPR_QUAD_FLOATS);
            spr_draw[spr_count].first = spr_count * DUEL_SPR_QUAD_VERTS;
            spr_draw[spr_count].tex = tex;
            spr_draw[spr_count].bright = bright;
            spr_count++;
        }
        /* fixed sun (XWA backdrop flare sprite): world direction, far but
         * inside the 30000 m frustum; depth-tested like everything else.
         * Retired while the M2.2 env is present (it brings its own sun). */
        if (r->spr_tex[DUEL_SPR_SUN] && !r->env_tex) {
            DuelVec3 sundir = duel_v3_norm(duel_v3(-0.30f, 0.35f, -0.90f));
            DuelVec3 c = duel_v3_add(ep, duel_v3_scale(sundir, 8000.0f));
            duel_sprite_quad(&c, &basis_r, &basis_u, 2400.0f, 2400.0f,
                             &face, 0.0f, spr + spr_count * DUEL_SPR_QUAD_FLOATS);
            spr_draw[spr_count].first = spr_count * DUEL_SPR_QUAD_VERTS;
            spr_draw[spr_count].tex = DUEL_SPR_SUN;
            spr_draw[spr_count].bright = 0.8f;
            spr_count++;
        }
#if DUEL_CELESTIAL
        /* M2.2: subtle lens flare on the env sun. World-anchored billboard
         * (never face-glued); gain follows view alignment, 0 past ~60 deg.
         * The glow texture is black-bordered by construction (no white
         * quads, ever). */
        if (r->env_tex && r->spr_tex[DUEL_SPR_FLARE]) {
            DuelVec3 fsun = duel_equirect_dir(DUEL_ENV_SUN_U, DUEL_ENV_SUN_V);
            float fg = duel_flare_gain(cam_fwd, fsun);
            /* sun and flare are mutually exclusive (env_tex gate), so at
             * most slots+1 quads; the cap below is belt-and-braces. */
            if (fg > 0.01f && spr_count < DUEL_SPR_SLOTS + 1) {
                DuelVec3 c = duel_v3_add(ep, duel_v3_scale(fsun, DUEL_FLARE_DIST));
                duel_sprite_quad(&c, &basis_r, &basis_u,
                                 DUEL_FLARE_SIZE_M, DUEL_FLARE_SIZE_M,
                                 &face, 0.0f,
                                 spr + spr_count * DUEL_SPR_QUAD_FLOATS);
                spr_draw[spr_count].first = spr_count * DUEL_SPR_QUAD_VERTS;
                spr_draw[spr_count].tex = DUEL_SPR_FLARE;
                spr_draw[spr_count].bright = DUEL_FLARE_MAX * fg;
                spr_count++;
            }
        }
#endif
    }

    /* upload dynamic verts via the persistent transfer buffer (created once;
     * per-frame alloc+pre-submit release was an M1 divergence risk) */
#if DUEL_VIRTUAL_HANDS && DUEL_HAND_MESH
    Uint32 hand_nb = skin_hands(r, s);
#else
    Uint32 hand_nb = 0;
#endif
    if ((dw.count > 0 || spr_count > 0 || hand_nb > 0) && r->dyn_tbuf) {
        SDL_GPUCopyPass *cp;
        SDL_GPUTransferBufferLocation src;
        SDL_GPUBufferRegion dstreg;
        memset(&src, 0, sizeof src);
        memset(&dstreg, 0, sizeof dstreg);
        cp = SDL_BeginGPUCopyPass(cmd);
        if (dw.count > 0) {
            Uint32 nbytes = (Uint32)dw.count * 9 * sizeof(float);
            void *dst = SDL_MapGPUTransferBuffer(r->device, r->dyn_tbuf, true);
            if (dst) {
                memcpy(dst, dyn, nbytes);
                SDL_UnmapGPUTransferBuffer(r->device, r->dyn_tbuf);
            }
            src.transfer_buffer = r->dyn_tbuf;
            dstreg.buffer = r->dyn_vbo;
            dstreg.size = nbytes;
            SDL_UploadToGPUBuffer(cp, &src, &dstreg, true);
        }
        if (spr_count > 0 && r->spr_tbuf) {
            Uint32 sn = (Uint32)spr_count * DUEL_SPR_QUAD_FLOATS * sizeof(float);
            void *sd = SDL_MapGPUTransferBuffer(r->device, r->spr_tbuf, true);
            if (sd) {
                memcpy(sd, spr, sn);
                SDL_UnmapGPUTransferBuffer(r->device, r->spr_tbuf);
            }
            src.transfer_buffer = r->spr_tbuf;
            dstreg.buffer = r->spr_vbo;
            dstreg.size = sn;
            SDL_UploadToGPUBuffer(cp, &src, &dstreg, true);
        }
#if DUEL_VIRTUAL_HANDS && DUEL_HAND_MESH
        if (hand_nb > 0 && r->hand_tbuf) {
            void *hd = SDL_MapGPUTransferBuffer(r->device, r->hand_tbuf, true);
            if (hd) {
                memcpy(hd, r->hand_stage, hand_nb);
                SDL_UnmapGPUTransferBuffer(r->device, r->hand_tbuf);
            }
            src.transfer_buffer = r->hand_tbuf;
            dstreg.buffer = r->hand_vbo;
            dstreg.size = hand_nb;
            SDL_UploadToGPUBuffer(cp, &src, &dstreg, true);
        }
#endif
        SDL_EndGPUCopyPass(cp);
    }
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &color, 1, &ds);
    /* M1 known-good: explicit full-target viewport + scissor per eye. */
    {
        SDL_GPUViewport vport;
        memset(&vport, 0, sizeof vport);
        vport.x = 0; vport.y = 0;
        vport.w = (float)w; vport.h = (float)h;
        vport.min_depth = 0; vport.max_depth = 1;
        SDL_SetGPUViewport(rp, &vport);
        SDL_Rect sc;
        sc.x = 0; sc.y = 0; sc.w = w; sc.h = h;
        SDL_SetGPUScissor(rp, &sc);
    }
    /* R0 oracle: M1 cube at LOCAL (0,0,-2), M1 math + M1 bytecode. Spin proves
     * frames advance. If the cube shows, submit/swapchain/views work.
     * P0.2: retired from the visual scene (DUEL_M1_ORACLE=0); the block
     * stays behind the define for future submit-level bisection. */
#if DUEL_M1_ORACLE
    {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_cube);
        SDL_GPUBufferBinding cvb, cib;
        memset(&cvb, 0, sizeof cvb); cvb.buffer = r->cube_vbo;
        memset(&cib, 0, sizeof cib); cib.buffer = r->cube_ibo;
        SDL_BindGPUVertexBuffers(rp, 0, &cvb, 1);
        SDL_BindGPUIndexBuffer(rp, &cib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
        M1Mat4 m1view = m1_view_from_pose(hmd);
        M1Mat4 m1proj = m1_proj(fov, 0.05f, 100.0f);
        float rot = s->time * 0.5f;
        float c = cosf(rot), sn = sinf(rot);
        M1Mat4 rotY = { { c, 0, -sn, 0, 0, 1, 0, 0, sn, 0, c, 0, 0, 0, 0, 1 } };
        float rot2 = rot * 0.7f;
        float c2 = cosf(rot2), s2 = sinf(rot2);
        M1Mat4 rotX = { { 1, 0, 0, 0, 0, c2, s2, 0, 0, -s2, c2, 0, 0, 0, 0, 1 } };
        M1Mat4 trans = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -2, 1 } };
        M1Mat4 model = m1_mul(m1_mul(rotY, rotX), trans);
        M1Mat4 mv = m1_mul(model, m1view);
        M1Mat4 mvp = m1_mul(mv, m1proj);
        SDL_PushGPUVertexUniformData(cmd, 0, &mvp, sizeof mvp);
        SDL_DrawGPUIndexedPrimitives(rp, 36, 1, 0, 0, 0);
    }
#endif

    /* planet + moon (opaque, depth on). P1.2: hidden for the clean
     * black+XWA-stars evaluation (DUEL_PROCEDURAL_PLANETS=0). */
#if DUEL_PROCEDURAL_PLANETS
    {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_planet);
        SDL_GPUBufferBinding vb, ib;
        memset(&vb, 0, sizeof vb); vb.buffer = r->sphere_vbo;
        memset(&ib, 0, sizeof ib); ib.buffer = r->sphere_ibo;
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        struct { DuelVec3 pos; float rad; float a[3]; float b[3]; float freq; } bodies[2] = {
            { { 3500.0f, 900.0f, -9500.0f }, 1500.0f, { 0.55f, 0.42f, 0.30f }, { 0.30f, 0.24f, 0.38f }, 9.0f },
            { { -4200.0f, -500.0f, -7000.0f }, 420.0f, { 0.45f, 0.45f, 0.48f }, { 0.30f, 0.30f, 0.33f }, 14.0f },
        };
        for (int i = 0; i < 2; i++) {
            DuelMat4 sm = { { bodies[i].rad, 0, 0, 0, 0, bodies[i].rad, 0, 0, 0, 0, bodies[i].rad, 0,
                               bodies[i].pos.x, bodies[i].pos.y, bodies[i].pos.z, 1 } };
            DuelMat4 mvp = duel_m4_mul(vp, sm);
            LitVSU vsu;
            memcpy(vsu.mvp, mvp.m, sizeof vsu.mvp);
            memcpy(vsu.model, sm.m, sizeof sm.m);
            SDL_PushGPUVertexUniformData(cmd, 0, &vsu, sizeof vsu);
            PlanetFSU fsu;
            fsu.light_dir[0] = 0.4f; fsu.light_dir[1] = 0.7f; fsu.light_dir[2] = 0.5f;
            fsu.ambient = 0.12f;
            memcpy(fsu.band_a, bodies[i].a, sizeof fsu.band_a);
            fsu.band_freq = bodies[i].freq;
            memcpy(fsu.band_b, bodies[i].b, sizeof fsu.band_b);
            fsu.pad = 0;
            SDL_PushGPUFragmentUniformData(cmd, 0, &fsu, sizeof fsu);
            SDL_DrawGPUIndexedPrimitives(rp, r->sphere_index_count, 1, 0, 0, 0);
        }
    }
#endif
    /* M2.2: planetary spheremap on the shared sphere geometry, additive
     * pipe (black = invisible), rotation-only sky VP: the planet sits at
     * effective infinity with zero translation parallax. Drawn before the
     * XWA stars (both depth-write-free, order irrelevant). */
#if DUEL_CELESTIAL
    if (r->env_tex) {
        DuelMat4 esm = { { DUEL_ENV_RADIUS, 0, 0, 0, 0, DUEL_ENV_RADIUS, 0, 0,
                            0, 0, DUEL_ENV_RADIUS, 0, 0, 0, 0, 1 } };
        DuelMat4 esky = duel_m4_rigid_inv(eq, duel_v3(0.0f, 0.0f, 0.0f));
        DuelMat4 emvp = duel_m4_mul(duel_m4_mul(proj, esky), esm);
        LitVSU evsu;
        LitFSU efsu;
        SDL_GPUBufferBinding evb, eib;
        SDL_GPUTextureSamplerBinding ebinds[2];
        memcpy(evsu.mvp, emvp.m, sizeof evsu.mvp);
        memcpy(evsu.model, esm.m, sizeof evsu.model);
        efsu.light_dir[0] = 0.0f; efsu.light_dir[1] = 1.0f; efsu.light_dir[2] = 0.0f;
        efsu.ambient = 0.0f;
        efsu.dir_intensity = 0.0f;
        efsu.emissive_strength = 1.0f;
        efsu.alpha = 1.0f;
        efsu.pad = 0.0f;
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_sprite);
        memset(&evb, 0, sizeof evb); evb.buffer = r->sphere_vbo;
        memset(&eib, 0, sizeof eib); eib.buffer = r->sphere_ibo;
        SDL_BindGPUVertexBuffers(rp, 0, &evb, 1);
        SDL_BindGPUIndexBuffer(rp, &eib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(cmd, 0, &evsu, sizeof evsu);
        SDL_PushGPUFragmentUniformData(cmd, 0, &efsu, sizeof efsu);
        memset(ebinds, 0, sizeof ebinds);
        ebinds[0].texture = r->black_tex;
        ebinds[1].texture = r->env_tex;
        ebinds[0].sampler = r->sampler;
        ebinds[1].sampler = r->sampler;
        SDL_BindGPUFragmentSamplers(rp, 0, ebinds, 2);
        SDL_DrawGPUIndexedPrimitives(rp, r->sphere_index_count, 1, 0, 0, 0);
    }
#endif
    /* stars (depth test on, write off). P1.2: rotation-only sky -- the
     * view translation is stripped so ship motion creates zero parallax,
     * exactly like the original (camera-matrix rotation of directions).
     * M2.2c: draw gated by DUEL_STATIC_STARS (experiment: depth only). */
#if DUEL_STATIC_STARS
    {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_points_test);
        SDL_GPUBufferBinding vb;
        memset(&vb, 0, sizeof vb); vb.buffer = r->stars_vbo;
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        UnlitVSU vsu;
        DuelMat4 sky_view = duel_m4_rigid_inv(eq, duel_v3(0.0f, 0.0f, 0.0f));
        DuelMat4 sky_vp = duel_m4_mul(proj, sky_view);
        memcpy(vsu.mvp, sky_vp.m, sizeof vsu.mvp);
        vsu.point_size = 2.5f; vsu.p0 = vsu.p1 = vsu.p2 = 0;
        SDL_PushGPUVertexUniformData(cmd, 0, &vsu, sizeof vsu);
        SDL_DrawGPUPrimitives(rp, (Uint32)r->star_count, 1, 0, 0);
    }
#endif
    /* M5: the furball. OPT frame: duel_model_matrix (R_y(PI) basis).
     * Allies reuse the X-wing exterior (guarded: the model may be
     * missing from device storage until pushed). */
    {
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) {
            if (!s->ties[k].alive) continue;
            DuelMat4 tm = duel_model_matrix(s->ties[k].orient, s->ties[k].pos);
            draw_model(r, NULL, cmd, rp, &r->tie, vp.m, tm.m, 0, 0, NULL);
        }
        if (r->xwing.vbo) {
            for (k = 0; k < DUEL_ALLY_COUNT; k++) {
                if (!s->allies[k].alive) continue;
                DuelMat4 am = duel_model_matrix(s->allies[k].orient, s->allies[k].pos);
                draw_model(r, NULL, cmd, rp, &r->xwing, vp.m, am.m, 0, 0, NULL);
            }
        }
    }
    /* cockpit (always; ship frame, OPT basis included). Player exterior is
     * NEVER drawn in cockpit mode (no exterior model is even loaded). */
    {
        static int draws_logged = 0;
        if (!draws_logged) {
            draws_logged = 1;
            SDL_Log("DUEL draws: exterior=0 cockpit=1 ties=%d allies=%d xwingvbo=%d diag=%d mask=0x%llX skipblend=%d",
                    Duel_TiesAlive(s), Duel_AlliesAlive(s), r->xwing.vbo ? 1 : 0,
                    DUEL_COCKPIT_DIAG_COLORS,
                    (unsigned long long)DUEL_COCKPIT_MESH_MASK, DUEL_SKIP_BLEND_PRIMS);
        }
        DuelMat4 cm = duel_model_matrix(s->player.orient, s->player.pos);
        /* M3.2: two passes for the stick column. M3.3: it follows the
         * HAND deflection (steering), spring-centered on release. */
        {
            float lever_m[16];
            DuelStick_ModelOffset(r->defl_pd, r->defl_rd, lever_m);
            draw_model(r, NULL, cmd, rp, &r->cockpit, vp.m, cm.m, DUEL_COCKPIT_DIAG_COLORS,
                       DUEL_SKIP_BLEND_PRIMS, lever_m);
        }
    }

    SDL_GPUBufferBinding dvb;
    memset(&dvb, 0, sizeof dvb);
    dvb.buffer = r->dyn_vbo;
    SDL_BindGPUVertexBuffers(rp, 0, &dvb, 1);
    UnlitVSU uvsu;
    memcpy(uvsu.mvp, vp.m, sizeof uvsu.mvp);
    uvsu.point_size = 3.0f; uvsu.p0 = uvsu.p1 = uvsu.p2 = 0;
    /* lasers */
    if (laser_range.count > 0) {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_tri_add);
        SDL_PushGPUVertexUniformData(cmd, 0, &uvsu, sizeof uvsu);
        SDL_DrawGPUPrimitives(rp, (Uint32)laser_range.count, 1, (Uint32)laser_range.first, 0);
    }
    /* particles */
    if (part_range.count > 0) {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_points_add);
        UnlitVSU psu = uvsu;
        psu.point_size = 5.0f;
        SDL_PushGPUVertexUniformData(cmd, 0, &psu, sizeof psu);
        SDL_DrawGPUPrimitives(rp, (Uint32)part_range.count, 1, (Uint32)part_range.first, 0);
    }
#if DUEL_SPACE_DEPTH
    /* M2.1: depth layers share the world-VP uniforms (NOT sky_vp). */
    if (near_range.count > 0) {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_points_add);
        UnlitVSU nsu = uvsu;
        nsu.point_size = 4.0f;
        SDL_PushGPUVertexUniformData(cmd, 0, &nsu, sizeof nsu);
        SDL_DrawGPUPrimitives(rp, (Uint32)near_range.count, 1, (Uint32)near_range.first, 0);
    }
    if (mid_range.count > 0) {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_points_add);
        UnlitVSU msu = uvsu;
        msu.point_size = 2.5f;
        SDL_PushGPUVertexUniformData(cmd, 0, &msu, sizeof msu);
        SDL_DrawGPUPrimitives(rp, (Uint32)mid_range.count, 1, (Uint32)mid_range.first, 0);
    }
#endif
#if DUEL_VIRTUAL_HANDS
    /* M3.0: hand joints (points) + bones (depth-tested lines). */
    if (hbone_range.count > 0) {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_lines_test);
        SDL_PushGPUVertexUniformData(cmd, 0, &uvsu, sizeof uvsu);
        SDL_DrawGPUPrimitives(rp, (Uint32)hbone_range.count, 1, (Uint32)hbone_range.first, 0);
    }
    if (hand_range.count > 0) {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_points_add);
        UnlitVSU hsu = uvsu;
        hsu.point_size = 7.0f;
        SDL_PushGPUVertexUniformData(cmd, 0, &hsu, sizeof hsu);
        SDL_DrawGPUPrimitives(rp, (Uint32)hand_range.count, 1, (Uint32)hand_range.first, 0);
    }
    /* M3.3: quadrant handle box (solid, depth-tested). */
    if (quad_range.count > 0) {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_tri_test);
        SDL_PushGPUVertexUniformData(cmd, 0, &uvsu, sizeof uvsu);
        SDL_DrawGPUPrimitives(rp, (Uint32)quad_range.count, 1, (Uint32)quad_range.first, 0);
    }
#if DUEL_HAND_MESH
    /* M3.5: platform hand meshes (opaque, depth-tested; one indexed draw
     * per active hand so a lost hand vanishes instead of freezing). */
    if ((r->hand_mesh[0] || r->hand_mesh[1]) && r->hand_vbo && r->hand_ibo) {
        SDL_GPUBufferBinding hvb, hib;
        int hside;
        memset(&hvb, 0, sizeof hvb);
        hvb.buffer = r->hand_vbo;
        SDL_BindGPUVertexBuffers(rp, 0, &hvb, 1);
        memset(&hib, 0, sizeof hib);
        hib.buffer = r->hand_ibo;
        SDL_BindGPUIndexBuffer(rp, &hib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_tri_test);
        SDL_PushGPUVertexUniformData(cmd, 0, &uvsu, sizeof uvsu);
        for (hside = 0; hside < 2; hside++) {
            if (!r->hand_mesh[hside]) continue;
            if (!r->hand_show[hside]) continue; /* M5 pop gate (see skin) */
            SDL_DrawGPUIndexedPrimitives(rp, (Uint32)r->hand_ni[hside], 1,
                                         (Uint32)r->hand_ibase[hside],
                                         r->hand_vbase[hside], 0);
        }
        /* line/hud/flash draws below assume dyn_vbo is still bound. */
        SDL_BindGPUVertexBuffers(rp, 0, &dvb, 1);
    }
#endif
#endif
    /* lines (reticle + diamond) */
    if (line_range.count > 0) {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_lines);
        SDL_PushGPUVertexUniformData(cmd, 0, &uvsu, sizeof uvsu);
        SDL_DrawGPUPrimitives(rp, (Uint32)line_range.count, 1, (Uint32)line_range.first, 0);
    }
    /* HUD + flash with identity MVP (NDC) */
    if (hud_range.count + flash_range.count > 0) {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_tri_alpha);
        UnlitVSU idsu;
        memset(&idsu, 0, sizeof idsu);
        idsu.mvp[0] = idsu.mvp[5] = idsu.mvp[10] = idsu.mvp[15] = 1.0f;
        SDL_PushGPUVertexUniformData(cmd, 0, &idsu, sizeof idsu);
        if (hud_range.count > 0)
            SDL_DrawGPUPrimitives(rp, (Uint32)hud_range.count, 1, (Uint32)hud_range.first, 0);
        if (flash_range.count > 0)
            SDL_DrawGPUPrimitives(rp, (Uint32)flash_range.count, 1, (Uint32)flash_range.first, 0);
    }
    /* M7 menu panel: world-space tris (alpha, depth off) first, then
     * glyph strokes + hand rays (lines, depth off). Empty ranges when
     * the panel is hidden, so count>0 is the only gate needed. */
    if (menu_tri_range.count > 0) {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_tri_alpha);
        SDL_PushGPUVertexUniformData(cmd, 0, &uvsu, sizeof uvsu);
        SDL_DrawGPUPrimitives(rp, (Uint32)menu_tri_range.count, 1, (Uint32)menu_tri_range.first, 0);
    }
    if (menu_line_range.count > 0) {
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_lines);
        SDL_PushGPUVertexUniformData(cmd, 0, &uvsu, sizeof uvsu);
        SDL_DrawGPUPrimitives(rp, (Uint32)menu_line_range.count, 1, (Uint32)menu_line_range.first, 0);
    }
    /* P1.5: authentic sprites. Lit shaders, vertex-baked world quads
     * (model=identity, mvp=vp): black base kills the lit term, the sprite
     * rides as emissive x bright. One draw per sprite per eye. */
    if (spr_count > 0) {
        SDL_GPUBufferBinding svb;
        LitVSU svsu;
        LitFSU sfsu;
        int i;
        memset(&svb, 0, sizeof svb);
        svb.buffer = r->spr_vbo;
        SDL_BindGPUGraphicsPipeline(rp, r->pipe_sprite);
        SDL_BindGPUVertexBuffers(rp, 0, &svb, 1);
        memcpy(svsu.mvp, vp.m, sizeof svsu.mvp);
        memset(svsu.model, 0, sizeof svsu.model);
        svsu.model[0] = svsu.model[5] = svsu.model[10] = svsu.model[15] = 1.0f;
        SDL_PushGPUVertexUniformData(cmd, 0, &svsu, sizeof svsu);
        if (!r->spr_logged) {
            r->spr_logged = 1;
            SDL_Log("DUEL_SPR draw: pipe=additive-lit quads=%d", spr_count);
        }
        sfsu.light_dir[0] = 0.0f; sfsu.light_dir[1] = 1.0f; sfsu.light_dir[2] = 0.0f;
        sfsu.ambient = 0.0f;
        sfsu.dir_intensity = 0.0f;
        sfsu.alpha = 1.0f;
        sfsu.pad = 0.0f;
        for (i = 0; i < spr_count; i++) {
            SDL_GPUTextureSamplerBinding binds[2];
            sfsu.emissive_strength = spr_draw[i].bright;
            SDL_PushGPUFragmentUniformData(cmd, 0, &sfsu, sizeof sfsu);
            memset(binds, 0, sizeof binds);
            binds[0].texture = r->black_tex;
            binds[1].texture = r->spr_tex[spr_draw[i].tex];
            binds[0].sampler = r->sampler;
            binds[1].sampler = r->sampler;
            SDL_BindGPUFragmentSamplers(rp, 0, binds, 2);
            SDL_DrawGPUPrimitives(rp, DUEL_SPR_QUAD_VERTS, 1,
                                  (Uint32)spr_draw[i].first, 0);
        }
    }
    SDL_EndGPURenderPass(rp);
    (void)w;
    (void)h;
}
