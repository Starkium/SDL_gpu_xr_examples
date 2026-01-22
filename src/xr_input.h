/*
 * xr_input.h - Thin convenience layer for XR controller input
 * 
 * EXPERIMENTAL - Exploring what SDL-style XR input convenience could look like.
 * 
 * Design principles:
 * - Wrap common OpenXR input patterns, don't replace OpenXR
 * - Spatial data (poses) stays in OpenXR domain
 * - Familiar SDL naming conventions
 * - Minimal boilerplate for the 80% case
 */

#ifndef XR_INPUT_H
#define XR_INPUT_H

#include <openxr/openxr.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Types
 * ============================================================================ */

typedef enum XR_Hand {
    XR_HAND_LEFT = 0,
    XR_HAND_RIGHT = 1,
    XR_HAND_COUNT = 2
} XR_Hand;

typedef enum XR_Button {
    XR_BUTTON_A,              /* Primary button (A on Quest, A on Index) */
    XR_BUTTON_B,              /* Secondary button */
    XR_BUTTON_X,              /* Tertiary (left controller) */
    XR_BUTTON_Y,              /* Quaternary (left controller) */
    XR_BUTTON_MENU,           /* Menu/System button */
    XR_BUTTON_THUMBSTICK,     /* Thumbstick click */
    XR_BUTTON_TRIGGER,        /* Trigger fully pressed (digital) */
    XR_BUTTON_GRIP,           /* Grip fully pressed (digital) */
    XR_BUTTON_THUMBREST,      /* Thumb on rest (capacitive) */
    XR_BUTTON_COUNT
} XR_Button;

typedef enum XR_Axis {
    XR_AXIS_TRIGGER,          /* Trigger pull [0, 1] */
    XR_AXIS_GRIP,             /* Grip squeeze [0, 1] */
    XR_AXIS_THUMBSTICK_X,     /* Thumbstick horizontal [-1, 1] */
    XR_AXIS_THUMBSTICK_Y,     /* Thumbstick vertical [-1, 1] */
    XR_AXIS_TRACKPAD_X,       /* Trackpad X (Vive) [-1, 1] */
    XR_AXIS_TRACKPAD_Y,       /* Trackpad Y (Vive) [-1, 1] */
    XR_AXIS_COUNT
} XR_Axis;

typedef enum XR_PoseType {
    XR_POSE_GRIP,             /* Where the hand grips - good for held objects */
    XR_POSE_AIM,              /* Points forward - good for laser pointers */
    XR_POSE_PALM,             /* Center of palm - good for UI interaction */
    XR_POSE_COUNT
} XR_PoseType;

/* Forward declaration - internal state */
typedef struct XR_InputState XR_InputState;

/* Controller handle */
typedef struct XR_Controller {
    XR_Hand hand;
    bool active;                  /* Is controller tracked/connected? */
    
    /* OpenXR spaces for pose queries (app uses these directly) */
    XrSpace gripSpace;
    XrSpace aimSpace;
    
    /* Internal - don't access directly */
    XR_InputState *_state;
} XR_Controller;

/* Haptic parameters */
typedef struct XR_HapticParams {
    float amplitude;              /* [0, 1] */
    float duration_seconds;       /* Duration in seconds, 0 = minimum */
    float frequency;              /* Hz, 0 = runtime default */
} XR_HapticParams;

/* ============================================================================
 * Initialization
 * ============================================================================ */

/*
 * Create the XR input system.
 * 
 * This sets up:
 * - An OpenXR action set with common actions
 * - Suggested bindings for popular controllers
 * - Hand spaces for pose queries
 * 
 * Call this after xrCreateSession, before xrAttachSessionActionSets.
 * 
 * Returns NULL on failure.
 */
XR_InputState *XR_Input_Create(XrInstance instance, XrSession session);

/*
 * Destroy the XR input system.
 */
void XR_Input_Destroy(XR_InputState *state);

/*
 * Get the action set to attach to the session.
 * 
 * App must call xrAttachSessionActionSets with this (and any other action sets).
 */
XrActionSet XR_Input_GetActionSet(XR_InputState *state);

/* ============================================================================
 * Per-Frame Update
 * ============================================================================ */

/*
 * Sync input state. Call once per frame before querying.
 * 
 * This calls xrSyncActions internally.
 * Returns false if sync failed.
 */
bool XR_Input_Sync(XR_InputState *state);

/*
 * Get controller state for a hand.
 * 
 * The returned pointer is valid until the next XR_Input_Sync or XR_Input_Destroy.
 */
XR_Controller *XR_Input_GetController(XR_InputState *state, XR_Hand hand);

/* ============================================================================
 * Input Queries
 * ============================================================================ */

/*
 * Get button state.
 * Returns true if pressed.
 */
bool XR_Input_GetButton(XR_Controller *controller, XR_Button button);

/*
 * Get button pressed this frame (edge trigger).
 */
bool XR_Input_GetButtonDown(XR_Controller *controller, XR_Button button);

/*
 * Get button released this frame (edge trigger).
 */
bool XR_Input_GetButtonUp(XR_Controller *controller, XR_Button button);

/*
 * Get axis value.
 * Triggers/grip: [0, 1]
 * Thumbsticks: [-1, 1]
 */
float XR_Input_GetAxis(XR_Controller *controller, XR_Axis axis);

/*
 * Get thumbstick as vector (convenience).
 */
void XR_Input_GetThumbstick(XR_Controller *controller, float *x, float *y);

/* ============================================================================
 * Haptics
 * ============================================================================ */

/*
 * Trigger haptic feedback.
 */
bool XR_Input_Haptic(XR_Controller *controller, const XR_HapticParams *params);

/*
 * Simple haptic pulse (convenience).
 */
bool XR_Input_HapticPulse(XR_Controller *controller, float amplitude, float duration_ms);

/*
 * Stop haptic feedback.
 */
bool XR_Input_HapticStop(XR_Controller *controller);

/* ============================================================================
 * Poses - Thin wrapper, but stays in OpenXR types
 * ============================================================================ */

/*
 * Get the XrSpace for a pose type.
 * 
 * App then uses xrLocateSpace() with their reference space and predicted time.
 * This keeps pose queries in OpenXR's timing model where they belong.
 */
XrSpace XR_Input_GetSpace(XR_Controller *controller, XR_PoseType pose);

/*
 * Convenience: Locate a pose (wraps xrLocateSpace).
 * 
 * Returns false if tracking lost or call failed.
 * Pose is only valid if return is true.
 */
bool XR_Input_LocatePose(
    XR_Controller *controller,
    XR_PoseType poseType,
    XrSpace baseSpace,
    XrTime time,
    XrPosef *outPose,
    XrSpaceVelocity *outVelocity  /* Can be NULL */
);

#ifdef __cplusplus
}
#endif

#endif /* XR_INPUT_H */
