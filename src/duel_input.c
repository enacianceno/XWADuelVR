/* XWADuelVR input implementation. */
#include "duel_input.h"

#include <SDL3/SDL_openxr.h>
#include <string.h>

#include "duel_xr.h"
#include "flight_config.h"

/* M35: A/B confirm/back (XWAQuestVR parity) + per-hand aim poses
 * for the menu ray. Aim degrades silently (ray off, sticks alive). */
enum {
    A_LSTICK = 0, A_RSTICK, A_RTRIGGER, A_LTRIGGER, A_MENUBTN,
    A_ABTN, A_BBTN, A_AIM_L, A_AIM_R, A_COUNT
};

static XrInstance s_instance = XR_NULL_HANDLE;
static XrSession s_session = XR_NULL_HANDLE;
static XrActionSet s_set = XR_NULL_HANDLE;
static XrAction s_actions[A_COUNT];
static XrSpace s_aim_space[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
static int s_aim_ok = 0;
static PFN_xrSyncActions s_sync = NULL;
static PFN_xrGetActionStateFloat s_getFloat = NULL;
static PFN_xrGetActionStateVector2f s_getVec = NULL;
static PFN_xrGetActionStateBoolean s_getBool = NULL;
static PFN_xrDestroyActionSet s_destroySet = NULL;
static PFN_xrCreateActionSpace s_createSpace = NULL;
static PFN_xrLocateSpace s_locateSpace = NULL;
static PFN_xrDestroySpace s_destroySpace = NULL;

static float s_throttle = 0.5f;
static int s_fire_r = 0, s_fire_l = 0;
static int s_armed = 0;
static int s_hand_left = 0; /* M36: 0 right-handed, 1 left-handed */

/* M36 handedness (main publishes menu.hand_left before each poll). */
void DuelInput_SetLeftHanded(int left) {
    s_hand_left = left ? 1 : 0;
}

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
    LOAD(s_createSpace, xrCreateActionSpace);
    LOAD(s_locateSpace, xrLocateSpace);
    LOAD(s_destroySpace, xrDestroySpace);
#undef LOAD

    XrActionSetCreateInfo sci;
    memset(&sci, 0, sizeof sci);
    sci.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    SDL_strlcpy(sci.actionSetName, "duel_flight", sizeof sci.actionSetName);
    SDL_strlcpy(sci.localizedActionSetName, "Duel flight", sizeof sci.localizedActionSetName);
    if (!check(create_set(instance, &sci, &s_set), "xrCreateActionSet")) return 0;

    static const char *names[A_COUNT] = { "lstick", "rstick", "rtrigger",
        "ltrigger", "menubtn", "abtn", "bbtn", "aim_l", "aim_r" };
    static const char *paths[A_COUNT] = {
        "/user/hand/left/input/thumbstick",
        "/user/hand/right/input/thumbstick",
        "/user/hand/right/input/trigger/value",
        "/user/hand/left/input/trigger/value",
        "/user/hand/left/input/menu/click",
        "/user/hand/right/input/a/click",
        "/user/hand/right/input/b/click",
        "/user/hand/left/input/aim/pose",
        "/user/hand/right/input/aim/pose",
    };
    XrActionSuggestedBinding bindings[A_COUNT];
    memset(bindings, 0, sizeof bindings);
    for (int i = 0; i < A_COUNT; i++) {
        XrActionCreateInfo ci;
        memset(&ci, 0, sizeof ci);
        ci.type = XR_TYPE_ACTION_CREATE_INFO;
        ci.actionType = i < A_RTRIGGER ? XR_ACTION_TYPE_VECTOR2F_INPUT
            : (i == A_MENUBTN || i == A_ABTN || i == A_BBTN) ? XR_ACTION_TYPE_BOOLEAN_INPUT
            : (i == A_AIM_L || i == A_AIM_R) ? XR_ACTION_TYPE_POSE_INPUT
            : XR_ACTION_TYPE_FLOAT_INPUT;
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
    /* Aim spaces (soft: controllers optional, ray degrades to off). */
    {
        int k;
        s_aim_ok = 1;
        for (k = 0; k < 2; k++) {
            XrActionSpaceCreateInfo sci2;
            memset(&sci2, 0, sizeof sci2);
            sci2.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
            sci2.action = s_actions[k == 0 ? A_AIM_L : A_AIM_R];
            sci2.subactionPath = XR_NULL_PATH;
            sci2.poseInActionSpace.orientation.w = 1.0f;
            if (s_createSpace(s_session, &sci2,
                              &s_aim_space[k]) != XR_SUCCESS) {
                SDL_Log("DUEL input: aim space %d failed (ray off)", k);
                s_aim_space[k] = XR_NULL_HANDLE;
                s_aim_ok = 0;
            }
        }
    }
    SDL_Log("DUEL input ready (oculus touch: sticks+triggers+menubtn+A/B+aim)");
    return 1;
}

int DuelInput_Poll(DuelInput *out, float dt, XrTime display_time) {
    memset(out, 0, sizeof(*out));
    out->thr_axis = 0.0f;
    /* Aim poses (XR LOCAL space; valid only when tracked this poll). */
    if (s_aim_ok && display_time > 0) {
        XrSpace base = DuelXr_Space();
        int k;
        for (k = 0; k < 2 && base != XR_NULL_HANDLE; k++) {
            XrSpaceLocation loc;
            memset(&loc, 0, sizeof loc);
            loc.type = XR_TYPE_SPACE_LOCATION;
            if (s_aim_space[k] != XR_NULL_HANDLE &&
                s_locateSpace(s_aim_space[k], base, display_time,
                              &loc) == XR_SUCCESS &&
                (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                (loc.locationFlags &
                 XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                out->aim_valid[k] = 1;
                out->aim_pos[k].x = loc.pose.position.x;
                out->aim_pos[k].y = loc.pose.position.y;
                out->aim_pos[k].z = loc.pose.position.z;
                out->aim_quat[k].x = loc.pose.orientation.x;
                out->aim_quat[k].y = loc.pose.orientation.y;
                out->aim_quat[k].z = loc.pose.orientation.z;
                out->aim_quat[k].w = loc.pose.orientation.w;
            }
        }
    }
    XrActiveActionSet set;
    memset(&set, 0, sizeof set);
    set.actionSet = s_set;
    XrActionsSyncInfo si;
    memset(&si, 0, sizeof si);
    si.type = XR_TYPE_ACTIONS_SYNC_INFO;
    si.countActiveActionSets = 1;
    si.activeActionSets = &set;
    if (s_sync(s_session, &si) == XR_SESSION_NOT_FOCUSED) { s_armed = 0; return 0; }

    float lx = 0, ly = 0, rx = 0, ry = 0, rt = 0, lt = 0;
    int mb = 0, ba = 0, bb = 0;
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
        } else if (i == A_MENUBTN || i == A_ABTN || i == A_BBTN) {
            XrActionStateBoolean st;
            memset(&st, 0, sizeof st);
            st.type = XR_TYPE_ACTION_STATE_BOOLEAN;
            if (s_getBool(s_session, &gi, &st) == XR_SUCCESS && st.isActive) {
                int v = st.currentState ? 1 : 0;
                if (i == A_MENUBTN) mb = v;
                else if (i == A_ABTN) ba = v;
                else bb = v;
            }
        } else if (i == A_AIM_L || i == A_AIM_R) {
            continue; /* pose actions: located above, no polled state */
        } else {
            XrActionStateFloat st;
            memset(&st, 0, sizeof st);
            st.type = XR_TYPE_ACTION_STATE_FLOAT;
            if (s_getFloat(s_session, &gi, &st) == XR_SUCCESS && st.isActive) {
                if (i == A_RTRIGGER) rt = st.currentState;
                else lt = st.currentState;
            }
        }
    }
    out->menu = mb; /* M7: level; main edge-detects (never armed-gated) */
    out->btn_a = ba;  /* M35: levels; main edge-detects */
    out->btn_b = bb;
    /* Arming: require neutral sticks + released trigger (M8 semantics). */
    int neutral = fabsf(lx) < DUEL_DEADZONE && fabsf(ly) < DUEL_DEADZONE &&
                  fabsf(rx) < DUEL_DEADZONE && fabsf(ry) < DUEL_DEADZONE &&
                  rt < DUEL_TRIGGER_FIRE_OFF && lt < DUEL_TRIGGER_FIRE_OFF;
    if (neutral) s_armed = 1;
    if (!s_armed) return 1;
    /* M36 handedness (user order: sticks were reversed; the flying hand
     * matches the flying hand-lever). DIESTRA (default): right stick =
     * yaw/pitch, left = roll/throttle. ZURDA mirrors (legacy map). */
    {
        float fx = s_hand_left ? lx : rx, fy = s_hand_left ? ly : ry;
        float gx = s_hand_left ? rx : lx, gy = s_hand_left ? ry : ly;
        out->yaw = fabsf(fx) > DUEL_DEADZONE ? fx : 0.0f;
        out->pitch = fabsf(fy) > DUEL_DEADZONE ? -fy : 0.0f;
        out->roll = fabsf(gx) > DUEL_DEADZONE ? gx : 0.0f;
        out->thr_axis = fabsf(gy) > DUEL_DEADZONE ? gy : 0.0f;
    }
    out->raw_lx = lx; out->raw_ly = ly; out->raw_rx = rx; out->raw_ry = ry;
    /* Either trigger fires (M8 .45/.55 hysteresis each): the flying
     * hand shoots in both handedness modes, either ray clicks. */
    if (rt > (s_fire_r ? DUEL_TRIGGER_FIRE_OFF : DUEL_TRIGGER_FIRE_ON))
        s_fire_r = 1;
    else if (rt < DUEL_TRIGGER_FIRE_OFF) s_fire_r = 0;
    if (lt > (s_fire_l ? DUEL_TRIGGER_FIRE_OFF : DUEL_TRIGGER_FIRE_ON))
        s_fire_l = 1;
    else if (lt < DUEL_TRIGGER_FIRE_OFF) s_fire_l = 0;
    out->fire = (s_fire_r || s_fire_l) ? 1 : 0;
    (void)s_throttle;
    (void)dt;
    return 1;
}

void DuelInput_Shutdown(void) {
    int k;
    for (k = 0; k < 2; k++) {
        if (s_aim_space[k] != XR_NULL_HANDLE && s_destroySpace)
            s_destroySpace(s_aim_space[k]);
        s_aim_space[k] = XR_NULL_HANDLE;
    }
    s_aim_ok = 0;
    if (s_set && s_destroySet) {
        s_destroySet(s_set);
        s_set = XR_NULL_HANDLE;
    }
}
