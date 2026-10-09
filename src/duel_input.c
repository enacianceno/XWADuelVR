/* XWADuelVR input implementation. */
#include "duel_input.h"

#include <SDL3/SDL_openxr.h>
#include <string.h>

#include "flight_config.h"

enum { A_LSTICK = 0, A_RSTICK, A_RTRIGGER, A_LTRIGGER, A_MENUBTN, A_COUNT };

static XrInstance s_instance = XR_NULL_HANDLE;
static XrSession s_session = XR_NULL_HANDLE;
static XrActionSet s_set = XR_NULL_HANDLE;
static XrAction s_actions[A_COUNT];
static PFN_xrSyncActions s_sync = NULL;
static PFN_xrGetActionStateFloat s_getFloat = NULL;
static PFN_xrGetActionStateVector2f s_getVec = NULL;
static PFN_xrGetActionStateBoolean s_getBool = NULL;
static PFN_xrDestroyActionSet s_destroySet = NULL;

static float s_throttle = 0.5f;
static int s_fire = 0;
static int s_armed = 0;

static int check(XrResult r, const char *op) {
    if (r == XR_SUCCESS) return 1;
    SDL_Log("DUEL input error %s result=%d", op, (int)r);
    return 0;
}

int DuelInput_Init(XrInstance instance, XrSession session) {
    s_instance = instance;
    s_session = session;
    PFN_xrGetInstanceProcAddr proc = SDL_OpenXR_GetXrGetInstanceProcAddr();
    PFN_xrCreateActionSet create_set;
    PFN_xrCreateAction create_action;
    PFN_xrStringToPath to_path;
    PFN_xrSuggestInteractionProfileBindings suggest;
    PFN_xrAttachSessionActionSets attach;
    if (!proc) return 0;
#define LOAD(var, name) do { \
    if (proc(instance, #name, (PFN_xrVoidFunction *)&var) != XR_SUCCESS || !var) return 0; \
} while (0)
    LOAD(create_set, xrCreateActionSet);
    LOAD(create_action, xrCreateAction);
    LOAD(to_path, xrStringToPath);
    LOAD(suggest, xrSuggestInteractionProfileBindings);
    LOAD(attach, xrAttachSessionActionSets);
    LOAD(s_destroySet, xrDestroyActionSet);
    LOAD(s_sync, xrSyncActions);
    LOAD(s_getFloat, xrGetActionStateFloat);
    LOAD(s_getVec, xrGetActionStateVector2f);
    LOAD(s_getBool, xrGetActionStateBoolean);
#undef LOAD

    XrActionSetCreateInfo sci;
    memset(&sci, 0, sizeof sci);
    sci.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    SDL_strlcpy(sci.actionSetName, "duel_flight", sizeof sci.actionSetName);
    SDL_strlcpy(sci.localizedActionSetName, "Duel flight", sizeof sci.localizedActionSetName);
    if (!check(create_set(instance, &sci, &s_set), "xrCreateActionSet")) return 0;

    static const char *names[A_COUNT] = { "lstick", "rstick", "rtrigger", "ltrigger", "menubtn" };
    static const char *paths[A_COUNT] = {
        "/user/hand/left/input/thumbstick",
        "/user/hand/right/input/thumbstick",
        "/user/hand/right/input/trigger/value",
        "/user/hand/left/input/trigger/value",
        "/user/hand/left/input/menu/click",
    };
    XrActionSuggestedBinding bindings[A_COUNT];
    memset(bindings, 0, sizeof bindings);
    for (int i = 0; i < A_COUNT; i++) {
        XrActionCreateInfo ci;
        memset(&ci, 0, sizeof ci);
        ci.type = XR_TYPE_ACTION_CREATE_INFO;
        ci.actionType = i < A_RTRIGGER ? XR_ACTION_TYPE_VECTOR2F_INPUT
            : i == A_MENUBTN ? XR_ACTION_TYPE_BOOLEAN_INPUT : XR_ACTION_TYPE_FLOAT_INPUT;
        SDL_strlcpy(ci.actionName, names[i], sizeof ci.actionName);
        SDL_strlcpy(ci.localizedActionName, names[i], sizeof ci.localizedActionName);
        if (!check(create_action(s_set, &ci, &s_actions[i]), "xrCreateAction")) return 0;
        bindings[i].action = s_actions[i];
        if (!check(to_path(instance, paths[i], &bindings[i].binding), "xrStringToPath")) return 0;
    }
    XrInteractionProfileSuggestedBinding profile;
    memset(&profile, 0, sizeof profile);
    profile.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
    profile.countSuggestedBindings = A_COUNT;
    profile.suggestedBindings = bindings;
    if (!check(to_path(instance, "/interaction_profiles/oculus/touch_controller",
                       &profile.interactionProfile), "profile path") ||
        !check(suggest(instance, &profile), "suggest")) return 0;
    XrSessionActionSetsAttachInfo ai;
    memset(&ai, 0, sizeof ai);
    ai.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    ai.countActionSets = 1;
    ai.actionSets = &s_set;
    if (!check(attach(session, &ai), "attach")) return 0;
    SDL_Log("DUEL input ready (oculus touch: sticks+triggers+menubtn)");
    return 1;
}

int DuelInput_Poll(DuelInput *out, float dt, XrTime display_time) {
    (void)display_time;
    memset(out, 0, sizeof(*out));
    out->thr_axis = 0.0f;
    XrActiveActionSet set;
    memset(&set, 0, sizeof set);
    set.actionSet = s_set;
    XrActionsSyncInfo si;
    memset(&si, 0, sizeof si);
    si.type = XR_TYPE_ACTIONS_SYNC_INFO;
    si.countActiveActionSets = 1;
    si.activeActionSets = &set;
    if (s_sync(s_session, &si) == XR_SESSION_NOT_FOCUSED) { s_armed = 0; return 0; }

    float lx = 0, ly = 0, rx = 0, ry = 0, rt = 0;
    int mb = 0;
    for (int i = 0; i < A_COUNT; i++) {
        XrActionStateGetInfo gi;
        memset(&gi, 0, sizeof gi);
        gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
        gi.action = s_actions[i];
        if (i < A_RTRIGGER) {
            XrActionStateVector2f st;
            memset(&st, 0, sizeof st);
            st.type = XR_TYPE_ACTION_STATE_VECTOR2F;
            if (s_getVec(s_session, &gi, &st) == XR_SUCCESS && st.isActive) {
                if (i == A_LSTICK) { lx = st.currentState.x; ly = st.currentState.y; }
                else { rx = st.currentState.x; ry = st.currentState.y; }
            }
        } else if (i == A_MENUBTN) {
            XrActionStateBoolean st;
            memset(&st, 0, sizeof st);
            st.type = XR_TYPE_ACTION_STATE_BOOLEAN;
            if (s_getBool(s_session, &gi, &st) == XR_SUCCESS && st.isActive)
                mb = st.currentState ? 1 : 0;
        } else {
            XrActionStateFloat st;
            memset(&st, 0, sizeof st);
            st.type = XR_TYPE_ACTION_STATE_FLOAT;
            if (s_getFloat(s_session, &gi, &st) == XR_SUCCESS && st.isActive) {
                if (i == A_RTRIGGER) rt = st.currentState;
            }
        }
    }
    out->menu = mb; /* M7: level; main edge-detects (never armed-gated) */
    /* Arming: require neutral sticks + released trigger (M8 semantics). */
    int neutral = fabsf(lx) < DUEL_DEADZONE && fabsf(ly) < DUEL_DEADZONE &&
                  fabsf(rx) < DUEL_DEADZONE && fabsf(ry) < DUEL_DEADZONE && rt < DUEL_TRIGGER_FIRE_OFF;
    if (neutral) s_armed = 1;
    if (!s_armed) return 1;
    /* P0.2f: XWAQuest m8integrated1 TouchConvert mapping, verbatim.
     * The P0.2e yaw/roll swap (user theory) failed its flight test; the
     * user ordered "copy the current XWAQuest project". Muscle memory wins:
     * yaw=lx, pitch=-ly, roll=rx, throttle lever on ry (up=accel). */
    out->yaw = fabsf(lx) > DUEL_DEADZONE ? lx : 0.0f;
    out->pitch = fabsf(ly) > DUEL_DEADZONE ? -ly : 0.0f;
    out->roll = fabsf(rx) > DUEL_DEADZONE ? rx : 0.0f;
    out->thr_axis = fabsf(ry) > DUEL_DEADZONE ? ry : 0.0f;
    out->raw_lx = lx; out->raw_ly = ly; out->raw_rx = rx; out->raw_ry = ry;
    /* Trigger hysteresis (M8 .45/.55). */
    if (rt > (s_fire ? DUEL_TRIGGER_FIRE_OFF : DUEL_TRIGGER_FIRE_ON)) s_fire = 1;
    else if (rt < DUEL_TRIGGER_FIRE_OFF) s_fire = 0;
    out->fire = s_fire;
    (void)s_throttle;
    (void)dt;
    return 1;
}

void DuelInput_Shutdown(void) {
    if (s_set && s_destroySet) {
        s_destroySet(s_set);
        s_set = XR_NULL_HANDLE;
    }
}
