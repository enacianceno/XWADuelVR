/* XWADuelVR renderer: SDL_GPU stereo. Ships from cooked glTF, procedural
 * stars/planet, CPU lasers/particles, line reticle, quad HUD bars. */
#ifndef XWADUELVR_RENDER_H
#define XWADUELVR_RENDER_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <openxr/openxr.h>

#include "duel_math.h"
#include "duel_sim.h"

typedef struct DuelRender DuelRender;

/* asset_dir: folder containing XwingCockpit/, TieFighter/ subdirs with
 * .gltf + .bin + textures/. swap_fmt from DuelXr_SwapchainFormat(). */
DuelRender *DuelRender_Create(SDL_GPUDevice *device, SDL_GPUTextureFormat swap_fmt,
                              const char *asset_dir);
int DuelRender_Ready(const DuelRender *r);
void DuelRender_Destroy(DuelRender *r);

/* Per-frame CPU updates (lasers quads, particles, explosion timers). */
void DuelRender_Update(DuelRender *r, const DuelState *s, float dt);

/* P0.4 seat calibration driver: call once per frame with both view poses
 * (before the eye loop). have_views=0 skips capture but tracks phase. */
void DuelRender_Calibrate(DuelRender *r, const DuelState *s,
                          const XrPosef *vl, const XrPosef *vr, int have_views);

/* Renders one eye into target with its depth texture. */
void DuelRender_Eye(DuelRender *r, SDL_GPUCommandBuffer *cmd,
                    SDL_GPUTexture *target, SDL_GPUTexture *depth,
                    int32_t w, int32_t h,
                    const XrPosef *hmd, const XrFovf *fov,
                    const DuelState *s);

/* P0.5 §21: calibration generation/reason + last neutralized head pose
 * for the DUEL_CTRL telemetry line. Any out-pointer may be NULL. */
void DuelRender_CalTelemetry(const DuelRender *r, int *generation, int *reason,
                             DuelVec3 *corr_pos, float *corr_yaw);

/* P1.2: last world eye pose for the audio listener. */
void DuelRender_HeadTelemetry(const DuelRender *r, DuelQuat *q, DuelVec3 *p);

/* M3.0: locate hand joints once per frame (after Calibrate, before eyes). */
void DuelRender_HandsLocate(DuelRender *r, XrTime display_time);

/* M3.2: 1 + value when a held hand owns the throttle, else 0. */
int DuelRender_HandThrottle(const DuelRender *r, float *value);

/* M3.3: 1 + deflection while the right hand steers (or spring settles). */
int DuelRender_HandStick(const DuelRender *r, float *pd, float *rd);
/* M6: 1 + roll while the left lever is held (or spring settles). */
int DuelRender_HandLever(const DuelRender *r, float *ld);

/* M3.4: 1 while the right index trigger is pulled, else 0. */
int DuelRender_HandFire(const DuelRender *r);

/* Seat (pilot eye) offset in ship space. Tunable; see checkpoint. */
extern const DuelVec3 DUEL_SEAT_POS;

#endif /* XWADUELVR_RENDER_H */
