# XR Controller Input Implementation Guide

This document outlines approaches to implementing XR controller support with SDL3 GPU + OpenXR, and compares with other major engines/frameworks.

---

## Table of Contents

1. [Overview](#overview)
2. [SDL + OpenXR Approach](#sdl--openxr-approach)
3. [Comparison with Other Engines](#comparison-with-other-engines)
4. [Implementation Details](#implementation-details)
5. [Controller Mesh Rendering](#controller-mesh-rendering)

---

## Overview

XR input differs fundamentally from traditional gamepad input:

| Aspect | Traditional Gamepad | XR Controller |
|--------|---------------------|---------------|
| **Tracking** | None | 6DOF position + orientation |
| **Input Model** | Raw device polling | Action-based abstraction |
| **Haptics** | Simple rumble | Localized, frequency-controlled |
| **Count** | 1-4 controllers | 2 hands + trackers |
| **Poses** | N/A | Grip, Aim, Palm poses |

---

## SDL + OpenXR Approach

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Game Logic  │  │ Input State │  │ Controller Renderer │  │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬──────────┘  │
│         │                │                     │              │
├─────────┴────────────────┴─────────────────────┴─────────────┤
│                    SDL3 GPU Layer                            │
│  ┌─────────────────┐  ┌─────────────────┐                   │
│  │ SDL_GPUDevice   │  │ XR Swapchains   │                   │
│  └────────┬────────┘  └────────┬────────┘                   │
├───────────┴────────────────────┴─────────────────────────────┤
│                    OpenXR Layer                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────────┐  │
│  │ Actions  │  │ Bindings │  │ Spaces   │  │ Swapchains  │  │
│  └──────────┘  └──────────┘  └──────────┘  └─────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

#### 1. Action System Setup

```c
// Define abstract actions (device-agnostic)
XrActionSet actionSet;
XrAction grabAction;        // Boolean - grip button
XrAction triggerAction;     // Float - trigger value  
XrAction thumbstickAction;  // Vector2 - thumbstick
XrAction poseAction;        // Pose - controller position/orientation
XrAction hapticAction;      // Output - vibration

// Per-hand tracking data
typedef struct {
    XrPath subactionPath;       // /user/hand/left or /user/hand/right
    XrSpace space;              // Tracking space for this controller
    XrPosef pose;               // Current pose
    XrSpaceVelocity velocity;   // Linear/angular velocity
    bool isActive;              // Is controller tracked?
    
    // Render data
    SDL_GPUBuffer *meshVertexBuffer;
    SDL_GPUBuffer *meshIndexBuffer;
    XrPosef renderOffset;       // Grip-to-render adjustment
} ControllerHand;
```

#### 2. Interaction Profile Bindings

```c
// Suggest bindings for different controller types
void SuggestBindings(XrInstance instance) {
    // Quest/Touch controllers
    XrPath touchProfile;
    xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller", &touchProfile);
    
    XrActionSuggestedBinding bindings[] = {
        {poseAction,    GetPath("/user/hand/left/input/grip/pose")},
        {poseAction,    GetPath("/user/hand/right/input/grip/pose")},
        {triggerAction, GetPath("/user/hand/left/input/trigger/value")},
        {triggerAction, GetPath("/user/hand/right/input/trigger/value")},
        {grabAction,    GetPath("/user/hand/left/input/squeeze/value")},
        {grabAction,    GetPath("/user/hand/right/input/squeeze/value")},
        {hapticAction,  GetPath("/user/hand/left/output/haptic")},
        {hapticAction,  GetPath("/user/hand/right/output/haptic")},
    };
    
    // Also add: Valve Index, Vive, Simple Controller fallback...
}
```

#### 3. Frame Loop Integration

```c
void XRFrame() {
    // 1. Sync and poll input
    XrActiveActionSet activeSet = {actionSet, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo = {XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;
    xrSyncActions(session, &syncInfo);
    
    // 2. Get controller poses
    for (int hand = 0; hand < 2; hand++) {
        XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
        xrLocateSpace(hands[hand].space, referenceSpace, predictedTime, &location);
        hands[hand].pose = location.pose;
        hands[hand].isActive = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
    }
    
    // 3. Get button states
    XrActionStateFloat triggerState = {XR_TYPE_ACTION_STATE_FLOAT};
    XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = triggerAction;
    getInfo.subactionPath = hands[0].subactionPath;
    xrGetActionStateFloat(session, &getInfo, &triggerState);
    
    // 4. Render controllers
    RenderControllers(hands);
}
```

---

## Comparison with Other Engines

### Khronos hello_xr (Reference Implementation)

**Approach:** Direct OpenXR API usage, C++

| Aspect | Khronos hello_xr | SDL + OpenXR |
|--------|------------------|--------------|
| **Abstraction** | None - raw OpenXR | Thin SDL layer over OpenXR |
| **Action Setup** | Manual C++ | Manual C |
| **Bindings** | Code-defined | Code-defined |
| **Poses** | XrSpace per hand | XrSpace per hand |
| **Rendering** | Plugin-based (D3D/Vulkan/GL) | SDL_GPU unified |

**Key Difference:** hello_xr is a reference, not production-ready. SDL approach adds the benefit of unified GPU abstraction.

```cpp
// Khronos hello_xr pattern
m_input.actionSet = CreateActionSet(m_instance, "gameplay", "Gameplay");
m_input.grabAction = CreateAction(m_input.actionSet, XR_ACTION_TYPE_FLOAT_INPUT, "grab", "Grab");
// ... very similar to our approach
```

---

### Unity XR Input System

**Approach:** Hardware-abstracted `InputFeatureUsage` + device characteristics

| Aspect | Unity | SDL + OpenXR |
|--------|-------|--------------|
| **Abstraction** | High - CommonUsages | Low - Direct OpenXR |
| **Device Access** | `InputDevice` objects | XrAction per feature |
| **Cross-Platform** | Automatic via providers | Manual profile bindings |
| **Poses** | `TrackedPoseDriver` component | Manual XrSpace queries |
| **Editor Integration** | Full visual setup | Code-only |

**Unity Pattern:**
```csharp
// Unity abstracts away OpenXR entirely
var device = InputDevices.GetDeviceAtXRNode(XRNode.LeftHand);
device.TryGetFeatureValue(CommonUsages.trigger, out float triggerValue);
device.TryGetFeatureValue(CommonUsages.devicePosition, out Vector3 position);

// Haptics
device.SendHapticImpulse(0, 0.5f, 0.1f);
```

**Key Differences:**
- Unity hides OpenXR completely - you use `CommonUsages` not action bindings
- Automatic controller model loading via `XR Interaction Toolkit`
- `TrackedPoseDriver` auto-positions GameObjects
- No manual interaction profile setup

---

### Godot XR Action Map System

**Approach:** Visual action map editor + OpenXR bindings

| Aspect | Godot | SDL + OpenXR |
|--------|-------|--------------|
| **Abstraction** | Medium - Action Map UI | Low - Direct OpenXR |
| **Binding Setup** | Visual editor | Code-defined |
| **Action Sets** | First-class (enable/disable) | Manual sync |
| **Poses** | `XRController3D` node | Manual XrSpace |
| **Profiles** | Visual per-controller | Code per-controller |

**Godot Pattern:**
```gdscript
# Godot exposes actions via signals on XRController3D
func _on_left_controller_button_pressed(name):
    if name == "trigger_click":
        shoot()

# Or poll directly
var trigger_value = $LeftController.get_float("trigger")
var controller_position = $LeftController.global_position
```

**Key Differences:**
- Godot provides a visual Action Map editor (similar to OpenXR spec concepts)
- Actions bound to nodes (`XRController3D`) that emit signals
- Built-in pose types: aim, grip, palm (selectable per node)
- Binding modifiers (thresholds, dpad emulation) configurable in UI
- Recommends only binding controllers you've actually tested

**Godot's Advice:**
> "Limit your action map to interaction profiles for devices you have actually tested. The XR runtimes are designed with this in mind - they can perform a better job of rebinding than a developer making educated guesses."

---

### Unreal Engine OpenXR

**Approach:** Blueprint/C++ + Enhanced Input System integration

| Aspect | Unreal | SDL + OpenXR |
|--------|--------|--------------|
| **Abstraction** | Very High | Low |
| **Setup** | Project Settings + Blueprints | Code |
| **Action Mapping** | Enhanced Input contexts | XrActionSet |
| **Hand Tracking** | `MotionControllerComponent` | Manual |
| **Haptics** | Blueprint nodes | Manual XR calls |

**Unreal Pattern:**
```cpp
// C++ approach
UMotionControllerComponent* LeftHand;
FVector Position = LeftHand->GetComponentLocation();
FRotator Rotation = LeftHand->GetComponentRotation();

// Or via Enhanced Input
void AMyPawn::SetupPlayerInputComponent(UInputComponent* Input) {
    EnhancedInput->BindAction(GrabAction, ETriggerEvent::Started, this, &AMyPawn::OnGrab);
}
```

**Key Differences:**
- Deep integration with Unreal's Enhanced Input System
- `MotionControllerComponent` handles all tracking automatically
- Controller models via OpenXR extensions or custom meshes
- Blueprint visual scripting for non-programmers
- Automatic platform abstraction (SteamVR, Oculus, WMR)

---

## Summary Comparison Table

| Feature | SDL+OpenXR | Khronos | Unity | Godot | Unreal |
|---------|------------|---------|-------|-------|--------|
| **Abstraction Level** | Low | None | High | Medium | Very High |
| **Setup Complexity** | Medium | High | Low | Low | Low |
| **Visual Editor** | ❌ | ❌ | ✅ | ✅ | ✅ |
| **Controller Models** | Manual | Manual | Auto | Auto | Auto |
| **Cross-Platform** | Manual | Manual | Auto | Auto | Auto |
| **Performance Control** | Full | Full | Limited | Medium | Limited |
| **Learning Curve** | Steep | Steepest | Gentle | Moderate | Moderate |

---

## Implementation Details

### Controller Pose Types

OpenXR defines several pose types per controller:

```
┌─────────────────────────────────────┐
│           Controller                │
│                                     │
│    ┌─────┐  ← AIM POSE             │
│    │     │    (laser pointer)       │
│    └──┬──┘                          │
│       │                             │
│    ┌──┴──┐  ← GRIP POSE            │
│    │     │    (where hand grips)    │
│    │     │                          │
│    └─────┘                          │
│                                     │
│    ─────── ← PALM POSE             │
│              (center of palm)       │
└─────────────────────────────────────┘
```

- **Grip Pose**: Where the hand physically grips - good for held objects
- **Aim Pose**: Points forward - good for laser pointers, guns
- **Palm Pose**: Center of palm - good for UI interaction, hand visualization

### Render Offset Compensation

Controllers report "grip" pose but visual models may need adjustment:

```c
// Quest Touch: grip is inside controller body
XrPosef touchRenderOffset = {
    .orientation = {0, 0, 0, 1},
    .position = {0, 0, 0.05f}  // 5cm forward
};

// Index: grip at handle center
XrPosef indexRenderOffset = {
    .orientation = QuatFromEuler(15, 0, 0), // 15° pitch
    .position = {0, -0.02f, 0.03f}
};

mat4x4 GetControllerRenderMatrix(ControllerHand *hand) {
    mat4x4 grip = XrPoseToMatrix(hand->pose);
    mat4x4 offset = XrPoseToMatrix(hand->renderOffset);
    mat4x4 result;
    mat4x4_mul(result, grip, offset);
    return result;
}
```

---

## Controller Mesh Rendering

### Options for Controller Visuals

1. **`XR_MSFT_controller_model` Extension**
   - Runtime provides official models
   - Automatic updates for new controllers
   - Not universally supported

2. **Community Models (OpenXR-Inventory)**
   - glTF models for common controllers
   - MIT licensed
   - Manual updates needed

3. **Procedural Fallback**
   - Simple capsule/box shapes
   - Always works
   - Not visually appealing

4. **Hand Tracking Visualization**
   - Skeleton-based rendering
   - Uses `XR_EXT_hand_tracking`
   - More natural but requires extension

### Recommended Approach

```c
bool LoadControllerMesh(ControllerHand *hand, XrSession session) {
    // Try runtime-provided model first
    if (MSFT_controller_model_supported) {
        return LoadRuntimeControllerModel(hand, session);
    }
    
    // Fall back to bundled glTF
    if (LoadGLTFModel(hand, "assets/controllers/quest_touch.gltf")) {
        return true;
    }
    
    // Ultimate fallback: procedural capsule
    return CreateProceduralController(hand);
}
```

---

## SDL Input Bridge (Optional)

If you want XR controllers to appear as SDL gamepads:

```c
// Create virtual joysticks for XR controllers
SDL_JoystickID leftJoystick = SDL_AttachVirtualJoystick(
    SDL_JOYSTICK_TYPE_GAMEPAD,
    6,   // axes: trigger, grip, thumbstick x/y, etc.
    12,  // buttons: A, B, X, Y, menu, etc.
    0    // hats
);

// Each frame, update from XR state
void BridgeXRToSDL(ControllerHand *hand, SDL_JoystickID joystick) {
    SDL_SetJoystickVirtualAxis(joystick, 0, (Sint16)(hand->trigger * 32767));
    SDL_SetJoystickVirtualButton(joystick, 0, hand->grabbing);
    // Position/rotation would need custom handling
}
```

**Limitations:**
- No pose data through SDL joystick API
- Loses XR-specific features (haptic localization, etc.)
- Useful mainly for 2D UI/menu navigation

---

## Recommendations

1. **Start Simple**: Use grip pose + trigger + grab actions
2. **Add Profiles Incrementally**: Start with Touch controllers, add others as you test
3. **Use Aim Pose for Pointing**: Better than grip for UI interaction
4. **Always Provide Haptic Feedback**: Users expect it for grabs/triggers
5. **Test Controller Visibility**: Some runtimes let users hide controllers
6. **Handle Loss of Tracking**: Controllers can go out of range

---

## SDL + OpenXR Input: The Design Question

*This section frames the problem space for discussion with other developers.*

### The Core Tension

**SDL's philosophy:** "Provide low-level cross-platform access, don't dictate architecture."

**Engine approach:** "Abstract everything, hide the platform."

XR input sits awkwardly between:
- Has gamepad-like elements (buttons, triggers, thumbsticks, haptics)
- Has spatial elements unique to XR (poses, tracking spaces, prediction)
- OpenXR already provides cross-platform abstraction (its whole point)

**The question: What value can SDL add on top of OpenXR?**

### Two Categories of XR Controller Data

#### Category 1: Traditional Input (SDL's Wheelhouse)

| Data | XR Equivalent | SDL Can Add Value? |
|------|---------------|-------------------|
| Buttons (A/B/X/Y) | `xrGetActionStateBoolean` | ✅ Mapping, events |
| Triggers/Grip | `xrGetActionStateFloat` | ✅ Axis abstraction |
| Thumbstick | `xrGetActionStateVector2f` | ✅ Axis abstraction |
| Gyro/Accel | Extension or derived | ✅ Sensor API |
| Haptics | `xrApplyHapticFeedback` | ✅ Per-hand rumble |
| Hot-plug | Session state | ✅ Event system |

These are the same problems SDL solves for gamepads. The patterns exist.

#### Category 2: Spatial Data (XR-Specific)

| Data | Why It's Different |
|------|-------------------|
| Position | Requires tracking space, predicted display time |
| Orientation | Reference frame dependent |
| Velocity | Physics integration, runtime-predicted |
| Tracking state | Confidence levels, loss handling |
| Hand/eye tracking | Skeleton data, gaze vectors |

These don't map to gamepad concepts. They're inherently tied to the XR frame loop.

### How Engines Separate These Concerns

**Unity:**
- Input: `InputDevice` + `CommonUsages` (buttons, axes)
- Spatial: `TrackedPoseDriver` component (separate system)
- Different subsystems, different update semantics

**Godot:**
- Input: Signals (`button_pressed`) and methods (`get_float`)
- Spatial: `global_transform` property on same node
- Unified access point, but clearly different data types

**Unreal:**
- Input: `EnhancedInputComponent` for actions
- Spatial: `MotionControllerComponent` for tracking
- Composed at actor level, separate responsibilities

**Khronos hello_xr:**
- Both via raw OpenXR
- No abstraction, full flexibility, maximum boilerplate

### Design Options for SDL

#### Option A: SDL Stays Out

*"OpenXR already abstracts XR input. Don't duplicate it."*

```c
// Developer uses OpenXR directly
xrSyncActions(session, &syncInfo);
xrGetActionStateFloat(session, &getInfo, &triggerState);
xrLocateSpace(handSpace, refSpace, time, &location);
```

**Pros:** No new API surface, full OpenXR access
**Cons:** Boilerplate for common patterns, steeper learning curve

#### Option B: Thin Convenience Layer

*"Wrap common patterns, don't replace OpenXR."*

```c
// Hypothetical - convenience for common cases
SDL_XR_Controller controllers[2];
SDL_XR_GetControllers(session, controllers);

float trigger = SDL_XR_GetTrigger(controllers[LEFT]);
bool aButton = SDL_XR_GetButton(controllers[LEFT], SDL_XR_BUTTON_A);
SDL_XR_Rumble(controllers[LEFT], 0.5f, 100);

// Spatial data stays in OpenXR (where it belongs)
xrLocateSpace(controllers[LEFT].gripSpace, refSpace, time, &pose);
```

**Pros:** Familiar SDL style, reduces boilerplate, spatial stays in XR domain
**Cons:** New API to maintain, thin enough to matter?

#### Option C: Full Abstraction (Engine-Style)

*"Hide OpenXR entirely."*

```c
// Everything through SDL
SDL_XR_Hand *left = SDL_XR_GetHand(SDL_XR_HAND_LEFT);
SDL_XR_Pose pose = SDL_XR_GetPose(left, SDL_XR_POSE_GRIP);
float trigger = SDL_XR_GetAxis(left, SDL_XR_AXIS_TRIGGER);
```

**Pros:** Maximum convenience
**Cons:** **Goes against SDL's nature** - SDL exposes platforms, doesn't hide them

#### Option D: Best Practices in Examples

*"Patterns in SDL_gpu_xr_examples, not in SDL itself."*

```c
// Example helper code, not official API
#include "xr_input_helpers.h"

XRInputState input;
XRInput_Update(&input, session, actionSet);
if (input.left.trigger > 0.5f) { ... }
```

**Pros:** No API commitment, shows patterns, developers customize freely
**Cons:** Not standardized, every project reinvents slightly

### The SDL Identity Question

SDL has historically been:
- **Thin** — close to platform APIs
- **Explicit** — you know what's happening
- **Portable** — same code, different platforms
- **Stable** — APIs don't churn

OpenXR already provides portability. So the question becomes:

> What convenience can SDL add without becoming an engine?

### Discussion Points for the Community

1. **Overlap:** OpenXR's action system is already an abstraction. Is wrapping an abstraction useful or just indirection?

2. **Scope:** Should any SDL XR input support be in core, a separate library (like SDL_mixer), or just example code?

3. **Mapping:** SDL's gamepad DB works because gamepads are similar. XR controllers vary more—does the same pattern apply?

4. **Boundaries:** Where's the line between "SDL convenience" and "becoming an engine"?

5. **Extensibility:** How do we handle XR features that don't map to input (hand tracking, eye gaze, body tracking)?

6. **Existing patterns:** SDL already handles per-controller haptics, sensors, hot-plug. Can these naturally extend to XR, or does XR need different primitives?

### Hand Tracking: A Bigger Question

Hand tracking deserves special attention because it's fundamentally different from controller input:

| Aspect | Controllers | Hand Tracking |
|--------|-------------|---------------|
| **Data model** | Discrete buttons + axes | 26-joint skeleton per hand |
| **Update rate** | ~90 Hz | ~30-90 Hz (varies by runtime) |
| **Confidence** | Binary (tracked or not) | Per-joint confidence values |
| **Gestures** | Hardware buttons | Software recognition |
| **Haptics** | Built-in vibration | None (or external devices) |

**Key questions for hand tracking:**

1. **Abstraction level:** Should SDL provide raw joint data, or higher-level gestures (pinch, grab, point)?

2. **Gesture recognition:** OpenXR doesn't define standard gestures. Should SDL? Or leave to apps?

3. **Hybrid input:** Many apps support both controllers AND hands. How to handle seamless switching?

4. **Extension dependency:** Hand tracking requires `XR_EXT_hand_tracking`. Not all runtimes support it.

5. **Skeleton format:** OpenXR defines 26 joints. Is this the right abstraction, or should SDL normalize across potential future hand APIs?

6. **Performance:** Full skeleton data is ~500 bytes per hand at 90Hz. Should SDL provide filtering/downsampling options?

**Potential approaches:**

```c
// Option A: Raw joint access (thin)
XR_HandJoint joints[XR_HAND_JOINT_COUNT];
XR_Input_GetHandJoints(leftHand, joints);
// App does gesture recognition

// Option B: Gesture helpers (medium)
if (XR_Input_GetGesture(leftHand, XR_GESTURE_PINCH) > 0.8f) {
    // Pinch detected
}

// Option C: Unified input (thick)
// Same API whether controller or hand
float grabStrength = XR_Input_GetGrab(leftHand);  // Works for grip button OR hand grab
```

This is even more complex than controller input—a strong argument for **keeping hand tracking as direct OpenXR** rather than wrapping it.

### Current State

PR #14837 focuses on **rendering** (GPU + OpenXR swapchains). Input is explicitly via direct OpenXR for now.

This document captures the design space for future discussion.

---

## Option B Experiment: Thin Convenience Layer

We prototyped Option B in `src/xr_input.h` and `src/xr_input.c`. Here's what we learned.

### What It Looks Like to Use

```c
// Init (after xrCreateSession)
XR_InputState *input = XR_Input_Create(instance, session);

// Attach to session
XrActionSet actionSets[] = { XR_Input_GetActionSet(input) };
XrSessionActionSetsAttachInfo attachInfo = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
attachInfo.countActionSets = 1;
attachInfo.actionSets = actionSets;
xrAttachSessionActionSets(session, &attachInfo);

// Each frame
XR_Input_Sync(input);

XR_Controller *left = XR_Input_GetController(input, XR_HAND_LEFT);
XR_Controller *right = XR_Input_GetController(input, XR_HAND_RIGHT);

// Familiar SDL-style input queries
if (XR_Input_GetButtonDown(right, XR_BUTTON_A)) {
    // A just pressed this frame
}

float trigger = XR_Input_GetAxis(right, XR_AXIS_TRIGGER);
if (trigger > 0.8f) {
    XR_Input_HapticPulse(right, 0.5f, 50);  // 50ms pulse
}

// Poses - intentionally stays in OpenXR types
XrPosef pose;
if (XR_Input_LocatePose(left, XR_POSE_GRIP, stageSpace, predictedTime, &pose, NULL)) {
    // Use pose for rendering
}
```

### Issues Discovered

#### Issue 1: Binding Explosion

Every controller type needs its own interaction profile bindings:

| Controller | Profile Path |
|------------|--------------|
| Quest Touch | `/interaction_profiles/oculus/touch_controller` |
| Valve Index | `/interaction_profiles/valve/index_controller` |
| HTC Vive | `/interaction_profiles/htc/vive_controller` |
| WMR | `/interaction_profiles/microsoft/motion_controller` |
| Pico | `/interaction_profiles/bytedance/pico_neo3_controller` |
| Simple (fallback) | `/interaction_profiles/khr/simple_controller` |

We hardcoded 4 profiles (~150 lines of binding tables). The full list is much longer.

**Question:** Should bindings be data-driven (JSON/config file) instead of hardcoded? Who maintains them?

#### Issue 2: Path String Construction

OpenXR paths are verbose. Building `/user/hand/left/input/trigger/value` from parts requires string manipulation—tedious and error-prone in C.

```c
// This happens a lot:
snprintf(pathBuf, sizeof(pathBuf), "%s%s", handPrefixes[hand], profile->trigger);
bindings[bindingCount].binding = GetPath(state->instance, pathBuf);
```

**Question:** Is there a cleaner pattern? Macros? Code generation?

#### Issue 3: Partial Failure

`xrSuggestInteractionProfileBindings` fails if *any* binding path is invalid. Different runtimes support different profiles.

```c
// If Quest runtime doesn't know Vive profile, this fails entirely
XrResult result = xrSuggestInteractionProfileBindings(instance, &suggestedBindings);
```

**Question:** Need to call separately per profile and handle failures gracefully. Is silent fallback okay, or should apps know what bound?

#### Issue 4: Unknown Controllers

What happens with a new controller the library doesn't know? The `simple_controller` fallback has only:
- Select (maps to trigger)
- Menu button
- Grip/aim poses

No thumbstick, no A/B buttons, no grip axis. Apps break on new hardware.

**Question:** Is "degrade gracefully" acceptable, or must apps always work fully?

#### Issue 5: Session State

`xrSyncActions` fails when session isn't focused:

```c
XrResult result = xrSyncActions(session, &syncInfo);
// Returns XR_SESSION_NOT_FOCUSED when user is in system menu
```

This isn't an error—it's expected. Need to distinguish "not focused" from actual failures.

**Question:** Should the API expose session focus state? Or just return zeros when unfocused?

### Code Size Reality Check

The "thin" layer is ~600 lines:
- Header: 170 lines  
- Implementation: 430 lines
- Just for basic buttons, axes, haptics, poses

**Compiles clean:** 0 warnings, 0 errors (MSVC, Release/Debug)

Adding more features (hand tracking, eye gaze, grip force) would grow this significantly.

**Question:** Is 600 lines "thin"? Is it worth it vs teaching developers raw OpenXR?

### Open Questions From This Experiment

1. **Is this thin enough?** 600 lines for basic input—worth it vs raw OpenXR?

2. **Binding maintenance:** Who maintains the profile database? SDL team? Community contributions?

3. **Extension support:** No grip force, finger curl, hand tracking, eye tracking. Add them? Or keep minimal?

4. **Where does this live?** SDL core? `SDL_xr` extension library? Example code only?

5. **Versioning:** What happens when OpenXR adds new profiles? Library update required?

6. **Customization:** Apps may want custom actions. Does this layer help or get in the way?

7. **Multiple action sets:** Real apps often have gameplay + menu + spectator action sets. This only creates one.

### Comparison: Convenience vs Direct OpenXR

| Task | Direct OpenXR | This Layer |
|------|---------------|------------|
| Get trigger value | ~15 lines | 1 line |
| Set up actions | ~100 lines | 1 line (init) |
| Suggest bindings | ~50 lines per profile | Automatic |
| Button edge detection | Manual tracking | Built-in |
| Haptic pulse | ~10 lines | 1 line |
| Handle new controller | You decide | Falls back (limited) |
| Custom action set | Full control | Not supported |

### Verdict So Far

The layer reduces boilerplate for the common case but:
- Hides what's happening (less "SDL-like")
- Binding maintenance is a real burden
- Doesn't handle advanced scenarios

Might be better as **example code that apps copy and modify** rather than an official API.

---

## References

- [OpenXR Specification - Input](https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#input)
- [Khronos hello_xr](https://github.com/KhronosGroup/OpenXR-SDK-Source/tree/main/src/tests/hello_xr)
- [Godot XR Action Map](https://docs.godotengine.org/en/stable/tutorials/xr/xr_action_map.html)
- [Unity XR Input](https://docs.unity3d.com/Manual/xr_input.html)
- [OpenXR-Inventory Controller Models](https://github.com/KhronosGroup/OpenXR-Inventory)
