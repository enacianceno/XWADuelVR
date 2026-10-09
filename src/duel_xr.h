/* XWADuelVR OpenXR session on SDL_GPU (Vulkan). Ported from XWAQuest
 * vr-probe/vr_openxr.c; the Aeron XR-device probe is replaced by SDL3 XR
 * GPU-device properties so this builds with zero engine code. */
#ifndef XWADUELVR_XR_H
#define XWADUELVR_XR_H

#include <openxr/openxr.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_openxr.h>

#define DUEL_EYE_COUNT 2

typedef struct DuelEyeSwapchain {
    XrSwapchain swapchain;
    SDL_GPUTexture **images; /* array indexed by xrAcquireSwapchainImage */
    Uint32 image_count;
    int32_t width, height;
    SDL_GPUTexture *depth_texture;
} DuelEyeSwapchain;

/* Creates the XR-enabled GPU device + session + stereo swapchains.
 * Returns 1 on success. Call DuelXr_PollEvents regularly. */
int DuelXr_Init(SDL_GPUDevice **out_device);
/* Frame flow (mirrors vr-probe): poll -> wait/begin -> locate -> render eyes
 * (acquire/render/release each) -> end. */
void DuelXr_PollEvents(void);
/* 1 once per Meta recenter (reference-space change), else 0. */
int DuelXr_ConsumeSpaceChange(void);
int DuelXr_IsRunning(void);
int DuelXr_ShouldQuit(void);
SDL_GPUTextureFormat DuelXr_SwapchainFormat(void);
int DuelXr_EyeSize(int eye, int32_t *w, int32_t *h);
SDL_GPUTexture *DuelXr_EyeDepth(int eye);
int DuelXr_WaitBeginFrame(int *should_render, XrTime *display_time);
int DuelXr_LocateViews(XrTime display_time, XrView out_views[DUEL_EYE_COUNT]);
/* >=0 acquired image index; -1 hard failure (skip eye). */
int DuelXr_AcquireEye(int eye, SDL_GPUTexture **out_image);
void DuelXr_ReleaseEye(int eye);
int DuelXr_EndFrame(XrTime display_time, const XrPosef poses[DUEL_EYE_COUNT],
                    const XrFovf fovs[DUEL_EYE_COUNT], int rendered);
XrInstance DuelXr_Instance(void);
XrSession DuelXr_Session(void);
XrSpace DuelXr_Space(void);
/* M3.0: loader entry for extension functions (hand tracking). */
PFN_xrGetInstanceProcAddr DuelXr_GetProc(void);
void DuelXr_Shutdown(void);

#endif /* XWADUELVR_XR_H */
