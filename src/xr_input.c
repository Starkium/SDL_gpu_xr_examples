/*
 * xr_input.c - Thin convenience layer for XR controller input
 * 
 * EXPERIMENTAL - See xr_input.h for design principles.
 */

#include "xr_input.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Internal State
 * ============================================================================ */

struct XR_InputState {
    XrInstance instance;
    XrSession session;
    
    /* Action set and actions */
    XrActionSet actionSet;
    
    /* Boolean actions */
    XrAction buttonActions[XR_BUTTON_COUNT];
    
    /* Float actions */
    XrAction triggerAction;
    XrAction gripAction;
    
    /* Vector2 actions */
    XrAction thumbstickAction;
    XrAction trackpadAction;
    
    /* Pose actions */
    XrAction gripPoseAction;
    XrAction aimPoseAction;
    
    /* Haptic action */
    XrAction hapticAction;
    
    /* Subaction paths */
    XrPath handPaths[XR_HAND_COUNT];
    
    /* Controller state */
    XR_Controller controllers[XR_HAND_COUNT];
    
    /* Previous frame button state (for edge detection) */
    bool prevButtons[XR_HAND_COUNT][XR_BUTTON_COUNT];
    bool currButtons[XR_HAND_COUNT][XR_BUTTON_COUNT];
};

/* ============================================================================
 * Helpers
 * ============================================================================ */

static XrPath GetPath(XrInstance instance, const char *pathString) {
    XrPath path;
    if (xrStringToPath(instance, pathString, &path) != XR_SUCCESS) {
        return XR_NULL_PATH;
    }
    return path;
}

static XrAction CreateAction(
    XrActionSet actionSet,
    XrActionType type,
    const char *name,
    const char *localizedName,
    XrPath *subactionPaths,
    uint32_t subactionCount
) {
    XrActionCreateInfo createInfo = {XR_TYPE_ACTION_CREATE_INFO};
    createInfo.actionType = type;
    strncpy(createInfo.actionName, name, XR_MAX_ACTION_NAME_SIZE);
    strncpy(createInfo.localizedActionName, localizedName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    createInfo.countSubactionPaths = subactionCount;
    createInfo.subactionPaths = subactionPaths;
    
    XrAction action;
    if (xrCreateAction(actionSet, &createInfo, &action) != XR_SUCCESS) {
        return XR_NULL_HANDLE;
    }
    return action;
}

/* ============================================================================
 * Interaction Profile Bindings
 * ============================================================================ */

/*
 * ISSUE #1: Binding explosion
 * 
 * Every controller type needs its own bindings. This is where the complexity lives.
 * We could:
 * A) Support a curated list (Quest, Index, Vive, WMR)
 * B) Let app extend with custom profiles
 * C) Use a data-driven approach (JSON/config file)
 * 
 * For now: hardcode common controllers to see what patterns emerge.
 */

typedef struct {
    const char *profile;
    const char *trigger;
    const char *grip;
    const char *thumbstick;
    const char *trackpad;  /* May be NULL */
    const char *buttonA;
    const char *buttonB;
    const char *buttonMenu;
    const char *thumbstickClick;
    const char *gripPose;
    const char *aimPose;
    const char *haptic;
} ControllerProfile;

static const ControllerProfile g_profiles[] = {
    /* Oculus Touch (Quest, Rift) */
    {
        .profile = "/interaction_profiles/oculus/touch_controller",
        .trigger = "/input/trigger/value",
        .grip = "/input/squeeze/value",
        .thumbstick = "/input/thumbstick",
        .trackpad = NULL,
        .buttonA = "/input/a/click",       /* Right: A, Left: X */
        .buttonB = "/input/b/click",       /* Right: B, Left: Y */
        .buttonMenu = "/input/menu/click", /* Left only */
        .thumbstickClick = "/input/thumbstick/click",
        .gripPose = "/input/grip/pose",
        .aimPose = "/input/aim/pose",
        .haptic = "/output/haptic",
    },
    /* Valve Index */
    {
        .profile = "/interaction_profiles/valve/index_controller",
        .trigger = "/input/trigger/value",
        .grip = "/input/squeeze/value",  /* Actually force-sensitive */
        .thumbstick = "/input/thumbstick",
        .trackpad = "/input/trackpad",
        .buttonA = "/input/a/click",
        .buttonB = "/input/b/click",
        .buttonMenu = "/input/system/click",
        .thumbstickClick = "/input/thumbstick/click",
        .gripPose = "/input/grip/pose",
        .aimPose = "/input/aim/pose",
        .haptic = "/output/haptic",
    },
    /* HTC Vive Wand */
    {
        .profile = "/interaction_profiles/htc/vive_controller",
        .trigger = "/input/trigger/value",
        .grip = "/input/squeeze/click",  /* Note: click, not value! */
        .thumbstick = NULL,              /* Vive has trackpad, not stick */
        .trackpad = "/input/trackpad",
        .buttonA = "/input/trackpad/click",  /* Mapped to trackpad */
        .buttonB = NULL,
        .buttonMenu = "/input/menu/click",
        .thumbstickClick = NULL,
        .gripPose = "/input/grip/pose",
        .aimPose = "/input/aim/pose",
        .haptic = "/output/haptic",
    },
    /* Simple Controller (fallback) */
    {
        .profile = "/interaction_profiles/khr/simple_controller",
        .trigger = "/input/select/click",  /* Simple has select, not trigger */
        .grip = NULL,
        .thumbstick = NULL,
        .trackpad = NULL,
        .buttonA = "/input/select/click",
        .buttonB = NULL,
        .buttonMenu = "/input/menu/click",
        .thumbstickClick = NULL,
        .gripPose = "/input/grip/pose",
        .aimPose = "/input/aim/pose",
        .haptic = "/output/haptic",
    },
};
static const int g_profileCount = sizeof(g_profiles) / sizeof(g_profiles[0]);

/*
 * ISSUE #2: Binding path construction
 * 
 * OpenXR paths are: /user/hand/left/input/trigger/value
 * Profile tables have: /input/trigger/value
 * 
 * Need to concatenate per-hand. String manipulation in C is tedious.
 */

static bool SuggestBindingsForProfile(
    XR_InputState *state,
    const ControllerProfile *profile
) {
    XrPath profilePath = GetPath(state->instance, profile->profile);
    if (profilePath == XR_NULL_PATH) {
        return false;  /* Profile not supported by runtime */
    }
    
    /* Build binding list dynamically */
    XrActionSuggestedBinding bindings[64];
    int bindingCount = 0;
    
    const char *handPrefixes[2] = {"/user/hand/left", "/user/hand/right"};
    
    for (int hand = 0; hand < 2; hand++) {
        char pathBuf[256];
        
        /* Trigger */
        if (profile->trigger) {
            snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->trigger);
            bindings[bindingCount].action = state->triggerAction;
            bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
            bindingCount++;
        }
        
        /* Grip */
        if (profile->grip) {
            snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->grip);
            bindings[bindingCount].action = state->gripAction;
            bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
            bindingCount++;
        }
        
        /* Thumbstick */
        if (profile->thumbstick) {
            snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->thumbstick);
            bindings[bindingCount].action = state->thumbstickAction;
            bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
            bindingCount++;
        }
        
        /* Trackpad */
        if (profile->trackpad) {
            snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->trackpad);
            bindings[bindingCount].action = state->trackpadAction;
            bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
            bindingCount++;
        }
        
        /* Button A */
        if (profile->buttonA) {
            snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->buttonA);
            bindings[bindingCount].action = state->buttonActions[XR_BUTTON_A];
            bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
            bindingCount++;
        }
        
        /* Button B */
        if (profile->buttonB) {
            snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->buttonB);
            bindings[bindingCount].action = state->buttonActions[XR_BUTTON_B];
            bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
            bindingCount++;
        }
        
        /* Menu button - typically left hand only on some controllers */
        if (profile->buttonMenu) {
            snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->buttonMenu);
            bindings[bindingCount].action = state->buttonActions[XR_BUTTON_MENU];
            bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
            bindingCount++;
        }
        
        /* Thumbstick click */
        if (profile->thumbstickClick) {
            snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->thumbstickClick);
            bindings[bindingCount].action = state->buttonActions[XR_BUTTON_THUMBSTICK];
            bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
            bindingCount++;
        }
        
        /* Grip pose */
        if (profile->gripPose) {
            snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->gripPose);
            bindings[bindingCount].action = state->gripPoseAction;
            bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
            bindingCount++;
        }
        
        /* Aim pose */
        if (profile->aimPose) {
            snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->aimPose);
            bindings[bindingCount].action = state->aimPoseAction;
            bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
            bindingCount++;
        }
        
        /* Haptic */
        if (profile->haptic) {
            snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->haptic);
            bindings[bindingCount].action = state->hapticAction;
            bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
            bindingCount++;
        }
    }
    
    XrInteractionProfileSuggestedBinding suggestedBindings = {
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING
    };
    suggestedBindings.interactionProfile = profilePath;
    suggestedBindings.suggestedBindings = bindings;
    suggestedBindings.countSuggestedBindings = bindingCount;
    
    XrResult result = xrSuggestInteractionProfileBindings(state->instance, &suggestedBindings);
    
    /* 
     * ISSUE #3: Partial failure
     * 
     * xrSuggestInteractionProfileBindings can fail if any path is invalid.
     * Different runtimes support different profiles. Need graceful fallback.
     */
    return result == XR_SUCCESS;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

XR_InputState *XR_Input_Create(XrInstance instance, XrSession session) {
    XR_InputState *state = calloc(1, sizeof(XR_InputState));
    if (!state) return NULL;
    
    state->instance = instance;
    state->session = session;
    
    /* Get hand paths */
    state->handPaths[XR_HAND_LEFT] = GetPath(instance, "/user/hand/left");
    state->handPaths[XR_HAND_RIGHT] = GetPath(instance, "/user/hand/right");
    
    /* Create action set */
    XrActionSetCreateInfo actionSetInfo = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(actionSetInfo.actionSetName, "xr_input_default");
    strcpy(actionSetInfo.localizedActionSetName, "Default Input");
    actionSetInfo.priority = 0;
    
    if (xrCreateActionSet(instance, &actionSetInfo, &state->actionSet) != XR_SUCCESS) {
        free(state);
        return NULL;
    }
    
    /* Create actions */
    
    /* Boolean button actions */
    const char *buttonNames[XR_BUTTON_COUNT] = {
        "button_a", "button_b", "button_x", "button_y",
        "button_menu", "button_thumbstick", "button_trigger",
        "button_grip", "button_thumbrest"
    };
    const char *buttonLocalized[XR_BUTTON_COUNT] = {
        "Button A", "Button B", "Button X", "Button Y",
        "Menu", "Thumbstick Click", "Trigger Click",
        "Grip Click", "Thumb Rest"
    };
    
    for (int i = 0; i < XR_BUTTON_COUNT; i++) {
        state->buttonActions[i] = CreateAction(
            state->actionSet,
            XR_ACTION_TYPE_BOOLEAN_INPUT,
            buttonNames[i],
            buttonLocalized[i],
            state->handPaths,
            2
        );
    }
    
    /* Float actions */
    state->triggerAction = CreateAction(
        state->actionSet,
        XR_ACTION_TYPE_FLOAT_INPUT,
        "trigger",
        "Trigger",
        state->handPaths,
        2
    );
    
    state->gripAction = CreateAction(
        state->actionSet,
        XR_ACTION_TYPE_FLOAT_INPUT,
        "grip",
        "Grip",
        state->handPaths,
        2
    );
    
    /* Vector2 actions */
    state->thumbstickAction = CreateAction(
        state->actionSet,
        XR_ACTION_TYPE_VECTOR2F_INPUT,
        "thumbstick",
        "Thumbstick",
        state->handPaths,
        2
    );
    
    state->trackpadAction = CreateAction(
        state->actionSet,
        XR_ACTION_TYPE_VECTOR2F_INPUT,
        "trackpad",
        "Trackpad",
        state->handPaths,
        2
    );
    
    /* Pose actions */
    state->gripPoseAction = CreateAction(
        state->actionSet,
        XR_ACTION_TYPE_POSE_INPUT,
        "grip_pose",
        "Grip Pose",
        state->handPaths,
        2
    );
    
    state->aimPoseAction = CreateAction(
        state->actionSet,
        XR_ACTION_TYPE_POSE_INPUT,
        "aim_pose",
        "Aim Pose",
        state->handPaths,
        2
    );
    
    /* Haptic action */
    state->hapticAction = CreateAction(
        state->actionSet,
        XR_ACTION_TYPE_VIBRATION_OUTPUT,
        "haptic",
        "Haptic Feedback",
        state->handPaths,
        2
    );
    
    /* Suggest bindings for all known profiles */
    int successCount = 0;
    for (int i = 0; i < g_profileCount; i++) {
        if (SuggestBindingsForProfile(state, &g_profiles[i])) {
            successCount++;
        }
    }
    
    /*
     * ISSUE #4: What if no profiles succeed?
     * 
     * On an unknown runtime/controller, all bindings might fail.
     * The simple_controller fallback should work, but...
     */
    if (successCount == 0) {
        fprintf(stderr, "XR_Input: Warning - no interaction profiles bound\n");
    }
    
    /* Create hand spaces */
    for (int hand = 0; hand < XR_HAND_COUNT; hand++) {
        XrActionSpaceCreateInfo spaceInfo = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = state->gripPoseAction;
        spaceInfo.subactionPath = state->handPaths[hand];
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;
        
        if (xrCreateActionSpace(session, &spaceInfo, &state->controllers[hand].gripSpace) != XR_SUCCESS) {
            state->controllers[hand].gripSpace = XR_NULL_HANDLE;
        }
        
        spaceInfo.action = state->aimPoseAction;
        if (xrCreateActionSpace(session, &spaceInfo, &state->controllers[hand].aimSpace) != XR_SUCCESS) {
            state->controllers[hand].aimSpace = XR_NULL_HANDLE;
        }
        
        state->controllers[hand].hand = (XR_Hand)hand;
        state->controllers[hand]._state = state;
    }
    
    return state;
}

void XR_Input_Destroy(XR_InputState *state) {
    if (!state) return;
    
    for (int hand = 0; hand < XR_HAND_COUNT; hand++) {
        if (state->controllers[hand].gripSpace != XR_NULL_HANDLE) {
            xrDestroySpace(state->controllers[hand].gripSpace);
        }
        if (state->controllers[hand].aimSpace != XR_NULL_HANDLE) {
            xrDestroySpace(state->controllers[hand].aimSpace);
        }
    }
    
    /* Actions are destroyed when action set is destroyed */
    if (state->actionSet != XR_NULL_HANDLE) {
        xrDestroyActionSet(state->actionSet);
    }
    
    free(state);
}

XrActionSet XR_Input_GetActionSet(XR_InputState *state) {
    return state ? state->actionSet : XR_NULL_HANDLE;
}

/* ============================================================================
 * Per-Frame Update
 * ============================================================================ */

bool XR_Input_Sync(XR_InputState *state) {
    if (!state) return false;
    
    /* Save previous button state for edge detection */
    memcpy(state->prevButtons, state->currButtons, sizeof(state->prevButtons));
    
    /* Sync actions */
    XrActiveActionSet activeSet = {0};
    activeSet.actionSet = state->actionSet;
    activeSet.subactionPath = XR_NULL_PATH;
    
    XrActionsSyncInfo syncInfo = {XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;
    
    XrResult result = xrSyncActions(state->session, &syncInfo);
    if (result != XR_SUCCESS) {
        /*
         * ISSUE #5: Session state
         * 
         * xrSyncActions can fail if session isn't focused.
         * Need to handle XR_SESSION_NOT_FOCUSED gracefully.
         */
        return false;
    }
    
    /* Update controller active state and cache button values */
    for (int hand = 0; hand < XR_HAND_COUNT; hand++) {
        /* Check if grip pose action is active (indicates controller is tracked) */
        XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = state->gripPoseAction;
        getInfo.subactionPath = state->handPaths[hand];
        
        XrActionStatePose poseState = {XR_TYPE_ACTION_STATE_POSE};
        xrGetActionStatePose(state->session, &getInfo, &poseState);
        state->controllers[hand].active = poseState.isActive;
        
        /* Cache button states */
        for (int btn = 0; btn < XR_BUTTON_COUNT; btn++) {
            if (state->buttonActions[btn] == XR_NULL_HANDLE) {
                state->currButtons[hand][btn] = false;
                continue;
            }
            
            getInfo.action = state->buttonActions[btn];
            XrActionStateBoolean boolState = {XR_TYPE_ACTION_STATE_BOOLEAN};
            if (xrGetActionStateBoolean(state->session, &getInfo, &boolState) == XR_SUCCESS) {
                state->currButtons[hand][btn] = boolState.isActive && boolState.currentState;
            } else {
                state->currButtons[hand][btn] = false;
            }
        }
    }
    
    return true;
}

XR_Controller *XR_Input_GetController(XR_InputState *state, XR_Hand hand) {
    if (!state || hand < 0 || hand >= XR_HAND_COUNT) return NULL;
    return &state->controllers[hand];
}

/* ============================================================================
 * Input Queries
 * ============================================================================ */

bool XR_Input_GetButton(XR_Controller *controller, XR_Button button) {
    if (!controller || !controller->_state || button >= XR_BUTTON_COUNT) return false;
    return controller->_state->currButtons[controller->hand][button];
}

bool XR_Input_GetButtonDown(XR_Controller *controller, XR_Button button) {
    if (!controller || !controller->_state || button >= XR_BUTTON_COUNT) return false;
    XR_InputState *state = controller->_state;
    int hand = controller->hand;
    return state->currButtons[hand][button] && !state->prevButtons[hand][button];
}

bool XR_Input_GetButtonUp(XR_Controller *controller, XR_Button button) {
    if (!controller || !controller->_state || button >= XR_BUTTON_COUNT) return false;
    XR_InputState *state = controller->_state;
    int hand = controller->hand;
    return !state->currButtons[hand][button] && state->prevButtons[hand][button];
}

float XR_Input_GetAxis(XR_Controller *controller, XR_Axis axis) {
    if (!controller || !controller->_state) return 0.0f;
    XR_InputState *state = controller->_state;
    
    XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.subactionPath = state->handPaths[controller->hand];
    
    switch (axis) {
        case XR_AXIS_TRIGGER: {
            getInfo.action = state->triggerAction;
            XrActionStateFloat floatState = {XR_TYPE_ACTION_STATE_FLOAT};
            if (xrGetActionStateFloat(state->session, &getInfo, &floatState) == XR_SUCCESS && floatState.isActive) {
                return floatState.currentState;
            }
            return 0.0f;
        }
        
        case XR_AXIS_GRIP: {
            getInfo.action = state->gripAction;
            XrActionStateFloat floatState = {XR_TYPE_ACTION_STATE_FLOAT};
            if (xrGetActionStateFloat(state->session, &getInfo, &floatState) == XR_SUCCESS && floatState.isActive) {
                return floatState.currentState;
            }
            return 0.0f;
        }
        
        case XR_AXIS_THUMBSTICK_X:
        case XR_AXIS_THUMBSTICK_Y: {
            getInfo.action = state->thumbstickAction;
            XrActionStateVector2f vec2State = {XR_TYPE_ACTION_STATE_VECTOR2F};
            if (xrGetActionStateVector2f(state->session, &getInfo, &vec2State) == XR_SUCCESS && vec2State.isActive) {
                return (axis == XR_AXIS_THUMBSTICK_X) ? vec2State.currentState.x : vec2State.currentState.y;
            }
            return 0.0f;
        }
        
        case XR_AXIS_TRACKPAD_X:
        case XR_AXIS_TRACKPAD_Y: {
            getInfo.action = state->trackpadAction;
            XrActionStateVector2f vec2State = {XR_TYPE_ACTION_STATE_VECTOR2F};
            if (xrGetActionStateVector2f(state->session, &getInfo, &vec2State) == XR_SUCCESS && vec2State.isActive) {
                return (axis == XR_AXIS_TRACKPAD_X) ? vec2State.currentState.x : vec2State.currentState.y;
            }
            return 0.0f;
        }
        
        default:
            return 0.0f;
    }
}

void XR_Input_GetThumbstick(XR_Controller *controller, float *x, float *y) {
    if (x) *x = XR_Input_GetAxis(controller, XR_AXIS_THUMBSTICK_X);
    if (y) *y = XR_Input_GetAxis(controller, XR_AXIS_THUMBSTICK_Y);
}

/* ============================================================================
 * Haptics
 * ============================================================================ */

bool XR_Input_Haptic(XR_Controller *controller, const XR_HapticParams *params) {
    if (!controller || !controller->_state || !params) return false;
    XR_InputState *state = controller->_state;
    
    XrHapticVibration vibration = {XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = params->amplitude;
    vibration.duration = (params->duration_seconds > 0) 
        ? (XrDuration)(params->duration_seconds * 1e9)  /* Convert to nanoseconds */
        : XR_MIN_HAPTIC_DURATION;
    vibration.frequency = (params->frequency > 0) ? params->frequency : XR_FREQUENCY_UNSPECIFIED;
    
    XrHapticActionInfo hapticInfo = {XR_TYPE_HAPTIC_ACTION_INFO};
    hapticInfo.action = state->hapticAction;
    hapticInfo.subactionPath = state->handPaths[controller->hand];
    
    return xrApplyHapticFeedback(state->session, &hapticInfo, (XrHapticBaseHeader*)&vibration) == XR_SUCCESS;
}

bool XR_Input_HapticPulse(XR_Controller *controller, float amplitude, float duration_ms) {
    XR_HapticParams params = {
        .amplitude = amplitude,
        .duration_seconds = duration_ms / 1000.0f,
        .frequency = 0
    };
    return XR_Input_Haptic(controller, &params);
}

bool XR_Input_HapticStop(XR_Controller *controller) {
    if (!controller || !controller->_state) return false;
    XR_InputState *state = controller->_state;
    
    XrHapticActionInfo hapticInfo = {XR_TYPE_HAPTIC_ACTION_INFO};
    hapticInfo.action = state->hapticAction;
    hapticInfo.subactionPath = state->handPaths[controller->hand];
    
    return xrStopHapticFeedback(state->session, &hapticInfo) == XR_SUCCESS;
}

/* ============================================================================
 * Poses
 * ============================================================================ */

XrSpace XR_Input_GetSpace(XR_Controller *controller, XR_PoseType pose) {
    if (!controller) return XR_NULL_HANDLE;
    
    switch (pose) {
        case XR_POSE_GRIP:
            return controller->gripSpace;
        case XR_POSE_AIM:
            return controller->aimSpace;
        case XR_POSE_PALM:
            /* Palm pose requires extension, fall back to grip */
            return controller->gripSpace;
        default:
            return XR_NULL_HANDLE;
    }
}

bool XR_Input_LocatePose(
    XR_Controller *controller,
    XR_PoseType poseType,
    XrSpace baseSpace,
    XrTime time,
    XrPosef *outPose,
    XrSpaceVelocity *outVelocity
) {
    if (!controller || !outPose) return false;
    
    XrSpace space = XR_Input_GetSpace(controller, poseType);
    if (space == XR_NULL_HANDLE) return false;
    
    XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
    if (outVelocity) {
        outVelocity->type = XR_TYPE_SPACE_VELOCITY;
        location.next = outVelocity;
    }
    
    XrResult result = xrLocateSpace(space, baseSpace, time, &location);
    if (result != XR_SUCCESS) return false;
    
    /* Check if pose is valid */
    const XrSpaceLocationFlags requiredFlags = 
        XR_SPACE_LOCATION_POSITION_VALID_BIT | 
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    
    if ((location.locationFlags & requiredFlags) != requiredFlags) {
        return false;
    }
    
    *outPose = location.pose;
    return true;
}
