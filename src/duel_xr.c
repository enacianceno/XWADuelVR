/* XWADuelVR OpenXR session implementation. */
#include "duel_xr.h"

#include <string.h>

#define DUEL_ACQUIRE_WAIT_TIMEOUT_NS (100000000LL)
/* M1 known-good: infinite wait + D24 depth + explicit vulkan backend. */

static PFN_xrGetInstanceProcAddr s_getProc = NULL;
static PFN_xrEnumerateViewConfigurationViews s_xrEnumerateViewConfigurationViews = NULL;
static PFN_xrEnumerateSwapchainImages s_xrEnumerateSwapchainImages = NULL;
static PFN_xrCreateReferenceSpace s_xrCreateReferenceSpace = NULL;
static PFN_xrDestroySpace s_xrDestroySpace = NULL;
static PFN_xrDestroySession s_xrDestroySession = NULL;
static PFN_xrDestroySwapchain s_xrDestroySwapchain = NULL;
static PFN_xrPollEvent s_xrPollEvent = NULL;
static PFN_xrBeginSession s_xrBeginSession = NULL;
static PFN_xrEndSession s_xrEndSession = NULL;
static PFN_xrWaitFrame s_xrWaitFrame = NULL;
static PFN_xrBeginFrame s_xrBeginFrame = NULL;
static PFN_xrEndFrame s_xrEndFrame = NULL;
static PFN_xrLocateViews s_xrLocateViews = NULL;
static PFN_xrAcquireSwapchainImage s_xrAcquireSwapchainImage = NULL;
static PFN_xrWaitSwapchainImage s_xrWaitSwapchainImage = NULL;
static PFN_xrReleaseSwapchainImage s_xrReleaseSwapchainImage = NULL;

static XrInstance s_instance = XR_NULL_HANDLE;
static XrSystemId s_system = XR_NULL_SYSTEM_ID;
static XrSession s_session = XR_NULL_HANDLE;
static XrSpace s_space = XR_NULL_HANDLE;
static SDL_GPUDevice *s_device = NULL;
static int s_running = 0;
static int s_shouldQuit = 0;
static int s_spaceChange = 0; /* sticky: LOCAL recenter seen, not yet consumed */

static DuelEyeSwapchain s_eyes[DUEL_EYE_COUNT];
static Uint32 s_viewCount = 0;
static SDL_GPUTextureFormat s_format = SDL_GPU_TEXTUREFORMAT_INVALID;

#define XR_LOAD(fn) \
    if (s_getProc(s_instance, #fn, (PFN_xrVoidFunction*)&s_##fn) != XR_SUCCESS || !s_##fn) { \
        SDL_Log("DUEL XR load failed: " #fn); \
        return 0; \
    }

static int load_functions(void) {
    s_getProc = (PFN_xrGetInstanceProcAddr)SDL_OpenXR_GetXrGetInstanceProcAddr();
    if (!s_getProc) {
        SDL_Log("DUEL SDL_OpenXR_GetXrGetInstanceProcAddr failed: %s", SDL_GetError());
        return 0;
    }
    XR_LOAD(xrEnumerateViewConfigurationViews);
    XR_LOAD(xrEnumerateSwapchainImages);
    XR_LOAD(xrCreateReferenceSpace);
    XR_LOAD(xrDestroySpace);
    XR_LOAD(xrDestroySession);
    XR_LOAD(xrDestroySwapchain);
    XR_LOAD(xrPollEvent);
    XR_LOAD(xrBeginSession);
    XR_LOAD(xrEndSession);
    XR_LOAD(xrWaitFrame);
    XR_LOAD(xrBeginFrame);
    XR_LOAD(xrEndFrame);
    XR_LOAD(xrLocateViews);
    XR_LOAD(xrAcquireSwapchainImage);
    XR_LOAD(xrWaitSwapchainImage);
    XR_LOAD(xrReleaseSwapchainImage);
    return 1;
}

static int create_swapchains(void) {
    XrResult r;
    XrViewConfigurationView configs[DUEL_EYE_COUNT];
    memset(configs, 0, sizeof configs);
    for (int i = 0; i < DUEL_EYE_COUNT; i++)
        configs[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    r = s_xrEnumerateViewConfigurationViews(s_instance, s_system,
                                            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                            DUEL_EYE_COUNT, &s_viewCount, configs);
    if (r != XR_SUCCESS || s_viewCount != DUEL_EYE_COUNT) {
        SDL_Log("DUEL view config failed result=%d count=%u", (int)r, s_viewCount);
        return 0;
    }
    int numFormats = 0;
    SDL_GPUTextureFormat *formats = SDL_GetGPUXRSwapchainFormats(s_device, s_session, &numFormats);
    if (!formats || numFormats == 0 || formats[0] == SDL_GPU_TEXTUREFORMAT_INVALID) {
        SDL_Log("DUEL no XR swapchain formats");
        if (formats) SDL_free(formats);
        return 0;
    }
    s_format = formats[0];
    SDL_Log("DUEL XR format=%d of %d", (int)s_format, numFormats);
    SDL_free(formats);

    memset(s_eyes, 0, sizeof s_eyes);
    for (int i = 0; i < DUEL_EYE_COUNT; i++) {
        XrSwapchainCreateInfo sci;
        memset(&sci, 0, sizeof sci);
        sci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sci.sampleCount = 1;
        sci.width = configs[i].recommendedImageRectWidth;
        sci.height = configs[i].recommendedImageRectHeight;
        sci.faceCount = 1;
        sci.arraySize = 1;
        sci.mipCount = 1;
        r = SDL_CreateGPUXRSwapchain(s_device, s_session, &sci, s_format,
                                     &s_eyes[i].swapchain, &s_eyes[i].images);
        if (r != XR_SUCCESS || !s_eyes[i].swapchain || !s_eyes[i].images) {
            SDL_Log("DUEL CreateGPUXRSwapchain eye %d failed result=%d", i, (int)r);
            return 0;
        }
        r = s_xrEnumerateSwapchainImages(s_eyes[i].swapchain, 0, &s_eyes[i].image_count, NULL);
        if (r != XR_SUCCESS) {
            SDL_Log("DUEL enumerate images eye %d failed", i);
            return 0;
        }
        s_eyes[i].width = (int32_t)sci.width;
        s_eyes[i].height = (int32_t)sci.height;
        SDL_GPUTextureCreateInfo depth;
        memset(&depth, 0, sizeof depth);
        depth.type = SDL_GPU_TEXTURETYPE_2D;
        depth.format = SDL_GPU_TEXTUREFORMAT_D24_UNORM; /* M1 known-good */
        depth.width = sci.width;
        depth.height = sci.height;
        depth.layer_count_or_depth = 1;
        depth.num_levels = 1;
        depth.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        s_eyes[i].depth_texture = SDL_CreateGPUTexture(s_device, &depth);
        if (!s_eyes[i].depth_texture) {
            SDL_Log("DUEL depth create eye %d failed: %s", i, SDL_GetError());
            return 0;
        }
        SDL_Log("DUEL swapchain %d: %dx%d images=%u", i,
                (int)s_eyes[i].width, (int)s_eyes[i].height, s_eyes[i].image_count);
    }
    return 1;
}

static void xr_device_props(SDL_PropertiesID props, XrInstance *xr_instance,
                            XrSystemId *xr_system, int hands_mode) {
    /* M3.5: FB mesh rides beside EXT; a runtime rejecting either falls
     * back via the baseline retry below (joints still work when the
     * pointer loads). */
    static const char *hands_ext[2] = { XR_EXT_HAND_TRACKING_EXTENSION_NAME,
                                        XR_FB_HAND_TRACKING_MESH_EXTENSION_NAME };
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "vulkan");
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_ENABLE_BOOLEAN, true);
    /* M3.0: request XR_EXT_hand_tracking at instance creation. SDL merges
     * user extensions with its own (SDL_gpu_openxr.c). M3.5: mode 2 =
     * EXT+FB, 1 = EXT only, 0 = baseline. */
    if (hands_mode > 0) {
        SDL_SetNumberProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_EXTENSION_COUNT_NUMBER,
                              hands_mode > 1 ? 2 : 1);
        SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_EXTENSION_NAMES_POINTER,
                               (void *)hands_ext);
    }
    SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_INSTANCE_POINTER, xr_instance);
    SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_SYSTEM_ID_POINTER, xr_system);
    SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_APPLICATION_NAME_STRING, "XWADuelVR");
    SDL_SetNumberProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_APPLICATION_VERSION_NUMBER, 1);
}

int DuelXr_Init(SDL_GPUDevice **out_device) {
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) return 0;
    XrInstance xr_instance = XR_NULL_HANDLE;
    XrSystemId xr_system = XR_NULL_SYSTEM_ID;
    xr_device_props(props, &xr_instance, &xr_system, 2);
    s_device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    if (!s_device) {
        /* M3.0 insurance (+M3.5 ladder): a runtime rejecting the FB mesh
         * still gets EXT-only hands; only a total reject falls back to
         * the exact pre-M3.0 props so boot never bricks. */
        SDL_Log("DUEL XR GPU device with hands+mesh failed (%s), retrying EXT-only",
                SDL_GetError());
        props = SDL_CreateProperties();
        if (!props) return 0;
        xr_device_props(props, &xr_instance, &xr_system, 1);
        s_device = SDL_CreateGPUDeviceWithProperties(props);
        SDL_DestroyProperties(props);
    }
    if (!s_device) {
        SDL_Log("DUEL XR GPU device with hands failed (%s), retrying baseline",
                SDL_GetError());
        props = SDL_CreateProperties();
        if (!props) return 0;
        xr_device_props(props, &xr_instance, &xr_system, 0);
        s_device = SDL_CreateGPUDeviceWithProperties(props);
        SDL_DestroyProperties(props);
    }
    if (!s_device) {
        SDL_Log("DUEL XR GPU device failed: %s", SDL_GetError());
        return 0;
    }
    s_instance = xr_instance;
    s_system = xr_system;
    SDL_Log("DUEL XR handles instance=%llu system=%llu",
            (unsigned long long)s_instance, (unsigned long long)s_system);
    if (!s_instance || !s_system) return 0;
    if (!load_functions()) return 0;
    {
        XrSessionCreateInfo info;
        memset(&info, 0, sizeof info);
        info.type = XR_TYPE_SESSION_CREATE_INFO;
        XrResult r = SDL_CreateGPUXRSession(s_device, &info, &s_session);
        if (r != XR_SUCCESS || !s_session) {
            SDL_Log("DUEL SDL_CreateGPUXRSession failed result=%d", (int)r);
            return 0;
        }
    }
    {
        XrReferenceSpaceCreateInfo space;
        memset(&space, 0, sizeof space);
        space.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
        space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        space.poseInReferenceSpace.orientation.w = 1.0f;
        XrResult r = s_xrCreateReferenceSpace(s_session, &space, &s_space);
        if (r != XR_SUCCESS) {
            SDL_Log("DUEL xrCreateReferenceSpace failed result=%d", (int)r);
            return 0;
        }
    }
    if (!create_swapchains()) return 0;
    for (int i = 0; i < 5; ++i) {
        DuelXr_PollEvents();
        SDL_Delay(50);
    }
    {
        XrSessionBeginInfo begin;
        memset(&begin, 0, sizeof begin);
        begin.type = XR_TYPE_SESSION_BEGIN_INFO;
        begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        XrResult r = XR_ERROR_SESSION_NOT_READY;
        for (int attempt = 0; attempt < 10; ++attempt) {
            r = s_xrBeginSession(s_session, &begin);
            if (r == XR_SUCCESS) break;
            if (r != XR_ERROR_SESSION_NOT_READY) {
                SDL_Log("DUEL xrBeginSession failed result=%d", (int)r);
                return 0;
            }
            SDL_Delay(200);
        }
        if (r != XR_SUCCESS) {
            SDL_Log("DUEL xrBeginSession failed after retries result=%d", (int)r);
            return 0;
        }
        s_running = 1;
        SDL_Log("DUEL session begun");
    }
    *out_device = s_device;
    return 1;
}

int DuelXr_IsRunning(void) { return s_running; }
int DuelXr_ShouldQuit(void) { return s_shouldQuit; }
SDL_GPUTextureFormat DuelXr_SwapchainFormat(void) { return s_format; }
XrInstance DuelXr_Instance(void) { return s_instance; }
PFN_xrGetInstanceProcAddr DuelXr_GetProc(void) { return s_getProc; }
XrSession DuelXr_Session(void) { return s_session; }
XrSpace DuelXr_Space(void) { return s_space; }

int DuelXr_EyeSize(int eye, int32_t *w, int32_t *h) {
    if (eye < 0 || eye >= DUEL_EYE_COUNT || s_viewCount != DUEL_EYE_COUNT) return 0;
    *w = s_eyes[eye].width;
    *h = s_eyes[eye].height;
    return 1;
}

SDL_GPUTexture *DuelXr_EyeDepth(int eye) {
    if (eye < 0 || eye >= DUEL_EYE_COUNT || s_viewCount != DUEL_EYE_COUNT) return NULL;
    return s_eyes[eye].depth_texture;
}

void DuelXr_PollEvents(void) {
    XrEventDataBuffer ev;
    memset(&ev, 0, sizeof ev);
    ev.type = XR_TYPE_EVENT_DATA_BUFFER;
    while (s_xrPollEvent && s_xrPollEvent(s_instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            XrEventDataSessionStateChanged *st = (XrEventDataSessionStateChanged *)&ev;
            if (st->state == XR_SESSION_STATE_STOPPING) {
                s_xrEndSession(s_session);
                s_running = 0;
                s_shouldQuit = 1;
            } else if (st->state == XR_SESSION_STATE_EXITING ||
                       st->state == XR_SESSION_STATE_LOSS_PENDING) {
                s_shouldQuit = 1;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            s_shouldQuit = 1;
        } else if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            /* Meta recenter redefines LOCAL (pose jumps). The seat
             * calibration consumes this and re-captures neutral; the
             * ship and cockpit never move. */
            s_spaceChange = 1;
        }
        memset(&ev, 0, sizeof ev);
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
}

/* Single-consumer latch: returns 1 once per reference-space change. */
int DuelXr_ConsumeSpaceChange(void) {
    int pending = s_spaceChange;
    s_spaceChange = 0;
    return pending;
}

int DuelXr_WaitBeginFrame(int *should_render, XrTime *display_time) {
    XrFrameState state;
    XrFrameWaitInfo wait;
    XrFrameBeginInfo begin;
    memset(&state, 0, sizeof state);
    state.type = XR_TYPE_FRAME_STATE;
    memset(&wait, 0, sizeof wait);
    wait.type = XR_TYPE_FRAME_WAIT_INFO;
    if (s_xrWaitFrame(s_session, &wait, &state) != XR_SUCCESS) return 0;
    memset(&begin, 0, sizeof begin);
    begin.type = XR_TYPE_FRAME_BEGIN_INFO;
    if (s_xrBeginFrame(s_session, &begin) != XR_SUCCESS) return 0;
    *should_render = state.shouldRender != 0;
    *display_time = state.predictedDisplayTime;
    return 1;
}

int DuelXr_LocateViews(XrTime display_time, XrView out_views[DUEL_EYE_COUNT]) {
    XrViewState vs;
    XrViewLocateInfo li;
    Uint32 count = 0;
    memset(&vs, 0, sizeof vs);
    vs.type = XR_TYPE_VIEW_STATE;
    memset(&li, 0, sizeof li);
    li.type = XR_TYPE_VIEW_LOCATE_INFO;
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = display_time;
    li.space = s_space;
    /* M1 known-good: XrView.type MUST be set before xrLocateViews.
     * Uninitialized stack garbage fails validation => no views => black. */
    for (int i = 0; i < DUEL_EYE_COUNT; i++) {
        memset(&out_views[i], 0, sizeof(out_views[i]));
        out_views[i].type = XR_TYPE_VIEW;
    }
    XrResult r = s_xrLocateViews(s_session, &li, &vs, DUEL_EYE_COUNT, &count, out_views);
    if (r != XR_SUCCESS || count != DUEL_EYE_COUNT) {
        static int n = 0;
        if (n < 3) {
            SDL_Log("DUEL LocateViews failed result=%d count=%u", (int)r, count);
            n++;
        }
        return 0;
    }
    if (!(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) ||
        !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) return 0;
    return 1;
}

int DuelXr_AcquireEye(int eye, SDL_GPUTexture **out_image) {
    if (eye < 0 || eye >= DUEL_EYE_COUNT || s_viewCount != DUEL_EYE_COUNT) return -1;
    DuelEyeSwapchain *e = &s_eyes[eye];
    Uint32 index = 0;
    XrSwapchainImageAcquireInfo ai;
    XrSwapchainImageWaitInfo wi;
    memset(&ai, 0, sizeof ai);
    ai.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    if (s_xrAcquireSwapchainImage(e->swapchain, &ai, &index) != XR_SUCCESS) return -1;
    if (index >= e->image_count) { DuelXr_ReleaseEye(eye); return -1; }
    memset(&wi, 0, sizeof wi);
    wi.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    wi.timeout = XR_INFINITE_DURATION; /* M1 known-good */
    XrResult r = s_xrWaitSwapchainImage(e->swapchain, &wi);
    if (r != XR_SUCCESS) { DuelXr_ReleaseEye(eye); return -1; }
    *out_image = e->images[index];
    return (int)index;
}

void DuelXr_ReleaseEye(int eye) {
    if (eye < 0 || eye >= DUEL_EYE_COUNT || s_viewCount != DUEL_EYE_COUNT) return;
    XrSwapchainImageReleaseInfo ri;
    memset(&ri, 0, sizeof ri);
    ri.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    s_xrReleaseSwapchainImage(s_eyes[eye].swapchain, &ri);
}

int DuelXr_EndFrame(XrTime display_time, const XrPosef poses[DUEL_EYE_COUNT],
                    const XrFovf fovs[DUEL_EYE_COUNT], int rendered) {
    XrCompositionLayerProjectionView views[DUEL_EYE_COUNT];
    XrCompositionLayerProjection layer;
    const XrCompositionLayerBaseHeader *layers[1];
    XrFrameEndInfo end;
    memset(views, 0, sizeof views);
    memset(&layer, 0, sizeof layer);
    memset(&end, 0, sizeof end);
    if (rendered) {
        for (int i = 0; i < DUEL_EYE_COUNT; i++) {
            views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            views[i].pose = poses[i];
            views[i].fov = fovs[i];
            views[i].subImage.swapchain = s_eyes[i].swapchain;
            views[i].subImage.imageRect.extent.width = s_eyes[i].width;
            views[i].subImage.imageRect.extent.height = s_eyes[i].height;
        }
        layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        layer.space = s_space;
        layer.viewCount = DUEL_EYE_COUNT;
        layer.views = views;
        layers[0] = (const XrCompositionLayerBaseHeader *)&layer;
        end.layerCount = 1;
        end.layers = layers;
    }
    end.type = XR_TYPE_FRAME_END_INFO;
    end.displayTime = display_time;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    return s_xrEndFrame(s_session, &end) == XR_SUCCESS;
}

void DuelXr_Shutdown(void) {
    if (s_running && s_xrEndSession) {
        s_xrEndSession(s_session);
        s_running = 0;
    }
    for (int i = 0; i < DUEL_EYE_COUNT; i++) {
        if (s_eyes[i].depth_texture && s_device) {
            SDL_ReleaseGPUTexture(s_device, s_eyes[i].depth_texture);
            s_eyes[i].depth_texture = NULL;
        }
        if (s_eyes[i].swapchain && s_device) {
            SDL_DestroyGPUXRSwapchain(s_device, s_eyes[i].swapchain, s_eyes[i].images);
            s_eyes[i].swapchain = XR_NULL_HANDLE;
            s_eyes[i].images = NULL;
        }
    }
    if (s_space && s_xrDestroySpace) { s_xrDestroySpace(s_space); s_space = XR_NULL_HANDLE; }
    if (s_session && s_xrDestroySession) { s_xrDestroySession(s_session); s_session = XR_NULL_HANDLE; }
    if (s_device) { SDL_DestroyGPUDevice(s_device); s_device = NULL; }
}
