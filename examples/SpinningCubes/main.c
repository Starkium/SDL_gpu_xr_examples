/*
 * Quest VR Test - OpenXR/SDL GPU test with Spinning Cubes for Meta Quest
 * 
 * This VR application renders multiple spinning colored cubes to validate
 * the full Android/Quest OpenXR rendering pipeline with SDL's GPU API.
 */

/* Include OpenXR headers FIRST, before SDL_openxr.h */
#include <openxr/openxr.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_openxr.h>
#include <SDL3/SDL_vulkan.h>

#include <math.h>

#define XR_ERR_LOG(result, msg) \
    do { \
        if (XR_FAILED(result)) { \
            SDL_Log("OpenXR Error: %s (result=%d)", msg, (int)result); \
            return 1; \
        } \
    } while(0)

/* ========================================================================
 * Math Types and Functions for 3D rendering
 * ======================================================================== */

typedef struct { float x, y, z; } Vec3;
typedef struct { float m[16]; } Mat4;

typedef struct {
    float x, y, z;
    Uint8 r, g, b, a;
} PositionColorVertex;

static Mat4 Mat4_Identity(void) {
    return (Mat4){{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }};
}

static Mat4 Mat4_Multiply(Mat4 a, Mat4 b) {
    Mat4 result = {{0}};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                result.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
            }
        }
    }
    return result;
}

static Mat4 Mat4_Translation(float x, float y, float z) {
    return (Mat4){{ 1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1 }};
}

static Mat4 Mat4_Scale(float s) {
    return (Mat4){{ s,0,0,0, 0,s,0,0, 0,0,s,0, 0,0,0,1 }};
}

static Mat4 Mat4_RotationY(float rad) {
    float c = SDL_cosf(rad), s = SDL_sinf(rad);
    return (Mat4){{ c,0,-s,0, 0,1,0,0, s,0,c,0, 0,0,0,1 }};
}

static Mat4 Mat4_RotationX(float rad) {
    float c = SDL_cosf(rad), s = SDL_sinf(rad);
    return (Mat4){{ 1,0,0,0, 0,c,s,0, 0,-s,c,0, 0,0,0,1 }};
}

/* Convert XrPosef to view matrix (inverted transform) */
static Mat4 Mat4_FromXrPose(XrPosef pose) {
    float x = pose.orientation.x, y = pose.orientation.y;
    float z = pose.orientation.z, w = pose.orientation.w;
    
    /* Quaternion to rotation matrix columns */
    Vec3 right = { 1-2*(y*y+z*z), 2*(x*y+w*z), 2*(x*z-w*y) };
    Vec3 up = { 2*(x*y-w*z), 1-2*(x*x+z*z), 2*(y*z+w*x) };
    Vec3 fwd = { 2*(x*z+w*y), 2*(y*z-w*x), 1-2*(x*x+y*y) };
    Vec3 pos = { pose.position.x, pose.position.y, pose.position.z };
    
    /* Inverted transform for view matrix */
    float dr = -(right.x*pos.x + right.y*pos.y + right.z*pos.z);
    float du = -(up.x*pos.x + up.y*pos.y + up.z*pos.z);
    float df = -(fwd.x*pos.x + fwd.y*pos.y + fwd.z*pos.z);
    
    return (Mat4){{ right.x,up.x,fwd.x,0, right.y,up.y,fwd.y,0, right.z,up.z,fwd.z,0, dr,du,df,1 }};
}

/* Create asymmetric projection matrix from XR FOV */
static Mat4 Mat4_Projection(XrFovf fov, float nearZ, float farZ) {
    float tL = SDL_tanf(fov.angleLeft), tR = SDL_tanf(fov.angleRight);
    float tU = SDL_tanf(fov.angleUp), tD = SDL_tanf(fov.angleDown);
    float w = tR - tL, h = tU - tD;
    
    return (Mat4){{
        2/w, 0, 0, 0,
        0, 2/h, 0, 0,
        (tR+tL)/w, (tU+tD)/h, -farZ/(farZ-nearZ), -1,
        0, 0, -(farZ*nearZ)/(farZ-nearZ), 0
    }};
}

/* ========================================================================
 * OpenXR Function Pointers (loaded dynamically)
 * ======================================================================== */

static PFN_xrGetInstanceProcAddr pfn_xrGetInstanceProcAddr = NULL;
static PFN_xrEnumerateViewConfigurationViews pfn_xrEnumerateViewConfigurationViews = NULL;
static PFN_xrEnumerateSwapchainImages pfn_xrEnumerateSwapchainImages = NULL;
static PFN_xrCreateReferenceSpace pfn_xrCreateReferenceSpace = NULL;
static PFN_xrDestroySpace pfn_xrDestroySpace = NULL;
static PFN_xrDestroySession pfn_xrDestroySession = NULL;
static PFN_xrPollEvent pfn_xrPollEvent = NULL;
static PFN_xrBeginSession pfn_xrBeginSession = NULL;
static PFN_xrEndSession pfn_xrEndSession = NULL;
static PFN_xrWaitFrame pfn_xrWaitFrame = NULL;
static PFN_xrBeginFrame pfn_xrBeginFrame = NULL;
static PFN_xrEndFrame pfn_xrEndFrame = NULL;
static PFN_xrLocateViews pfn_xrLocateViews = NULL;
static PFN_xrAcquireSwapchainImage pfn_xrAcquireSwapchainImage = NULL;
static PFN_xrWaitSwapchainImage pfn_xrWaitSwapchainImage = NULL;
static PFN_xrReleaseSwapchainImage pfn_xrReleaseSwapchainImage = NULL;

/* ========================================================================
 * Global State
 * ======================================================================== */

/* OpenXR state */
static XrInstance xrInstance = XR_NULL_HANDLE;
static XrSystemId xrSystemId = XR_NULL_SYSTEM_ID;
static XrSession xrSession = XR_NULL_HANDLE;
static XrSpace xrLocalSpace = XR_NULL_HANDLE;
static bool xrSessionRunning = false;
static bool xrShouldQuit = false;

/* Swapchain state */
typedef struct {
    XrSwapchain swapchain;
    SDL_GPUTexture **images;
    XrExtent2Di size;
    SDL_GPUTextureFormat format;
    uint32_t imageCount;
} VRSwapchain;

static VRSwapchain *vrSwapchains = NULL;
static XrView *xrViews = NULL;
static uint32_t viewCount = 0;

/* SDL GPU state */
static SDL_GPUDevice *gpuDevice = NULL;
static SDL_GPUGraphicsPipeline *pipeline = NULL;
static SDL_GPUBuffer *vertexBuffer = NULL;
static SDL_GPUBuffer *indexBuffer = NULL;

/* Animation time */
static float animTime = 0.0f;

/* Cube scene configuration */
#define NUM_CUBES 5
static Vec3 cubePositions[NUM_CUBES] = {
    { 0.0f, 0.0f, -2.0f },     /* Center, in front */
    { -1.2f, 0.4f, -2.5f },    /* Upper left */
    { 1.2f, 0.3f, -2.5f },     /* Upper right */
    { -0.6f, -0.4f, -1.8f },   /* Lower left close */
    { 0.6f, -0.3f, -1.8f },    /* Lower right close */
};
static float cubeScales[NUM_CUBES] = { 1.0f, 0.6f, 0.6f, 0.5f, 0.5f };
static float cubeSpeeds[NUM_CUBES] = { 1.0f, 1.5f, -1.2f, 2.0f, -0.8f };

/* ========================================================================
 * Shader and Pipeline Creation
 * ======================================================================== */

static SDL_GPUShader* LoadShader(const char* shaderName, SDL_GPUShaderStage stage, Uint32 samplerCount, Uint32 uniformBufferCount)
{
    char path[256];
    SDL_snprintf(path, sizeof(path), "Shaders/Compiled/SPIRV/%s.spv", shaderName);
    
    size_t codeSize;
    void* code = SDL_LoadFile(path, &codeSize);
    if (!code) {
        SDL_Log("Failed to load shader %s: %s", path, SDL_GetError());
        return NULL;
    }
    
    SDL_GPUShaderCreateInfo shaderInfo = {
        .code = (const Uint8*)code,
        .code_size = codeSize,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = stage,
        .num_samplers = samplerCount,
        .num_uniform_buffers = uniformBufferCount
    };
    
    SDL_GPUShader* shader = SDL_CreateGPUShader(gpuDevice, &shaderInfo);
    SDL_free(code);
    
    if (!shader) {
        SDL_Log("Failed to create shader %s: %s", shaderName, SDL_GetError());
    } else {
        SDL_Log("Loaded shader: %s", shaderName);
    }
    
    return shader;
}

static int CreatePipeline(SDL_GPUTextureFormat colorFormat)
{
    SDL_GPUShader *vertShader = LoadShader("PositionColorTransform.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader *fragShader = LoadShader("SolidColor.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    
    if (!vertShader || !fragShader) {
        if (vertShader) SDL_ReleaseGPUShader(gpuDevice, vertShader);
        if (fragShader) SDL_ReleaseGPUShader(gpuDevice, fragShader);
        return 1;
    }
    
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
        .vertex_shader = vertShader,
        .fragment_shader = fragShader,
        .target_info = {
            .num_color_targets = 1,
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]){{
                .format = colorFormat
            }}
        },
        .depth_stencil_state = {
            .enable_depth_test = false,
            .enable_depth_write = false
        },
        .rasterizer_state = {
            .cull_mode = SDL_GPU_CULLMODE_BACK,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .fill_mode = SDL_GPU_FILLMODE_FILL
        },
        .vertex_input_state = {
            .num_vertex_buffers = 1,
            .vertex_buffer_descriptions = (SDL_GPUVertexBufferDescription[]){{
                .slot = 0,
                .pitch = sizeof(PositionColorVertex),
                .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX
            }},
            .num_vertex_attributes = 2,
            .vertex_attributes = (SDL_GPUVertexAttribute[]){{
                .location = 0,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                .offset = 0
            }, {
                .location = 1,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
                .offset = sizeof(float) * 3
            }}
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST
    };
    
    pipeline = SDL_CreateGPUGraphicsPipeline(gpuDevice, &pipelineInfo);
    
    SDL_ReleaseGPUShader(gpuDevice, vertShader);
    SDL_ReleaseGPUShader(gpuDevice, fragShader);
    
    if (!pipeline) {
        SDL_Log("Failed to create pipeline: %s", SDL_GetError());
        return 1;
    }
    
    SDL_Log("Created graphics pipeline for format %d", colorFormat);
    return 0;
}

static int CreateCubeBuffers(void)
{
    /* Cube vertices - 0.25m half-size, each face a different color */
    float s = 0.25f;
    PositionColorVertex vertices[24] = {
        /* Front face (red) */
        {-s,-s,-s, 255,0,0,255}, {s,-s,-s, 255,0,0,255}, {s,s,-s, 255,0,0,255}, {-s,s,-s, 255,0,0,255},
        /* Back face (green) */
        {s,-s,s, 0,255,0,255}, {-s,-s,s, 0,255,0,255}, {-s,s,s, 0,255,0,255}, {s,s,s, 0,255,0,255},
        /* Left face (blue) */
        {-s,-s,s, 0,0,255,255}, {-s,-s,-s, 0,0,255,255}, {-s,s,-s, 0,0,255,255}, {-s,s,s, 0,0,255,255},
        /* Right face (yellow) */
        {s,-s,-s, 255,255,0,255}, {s,-s,s, 255,255,0,255}, {s,s,s, 255,255,0,255}, {s,s,-s, 255,255,0,255},
        /* Top face (magenta) */
        {-s,s,-s, 255,0,255,255}, {s,s,-s, 255,0,255,255}, {s,s,s, 255,0,255,255}, {-s,s,s, 255,0,255,255},
        /* Bottom face (cyan) */
        {-s,-s,s, 0,255,255,255}, {s,-s,s, 0,255,255,255}, {s,-s,-s, 0,255,255,255}, {-s,-s,-s, 0,255,255,255}
    };
    
    Uint16 indices[36] = {
        0,1,2, 0,2,3,       /* Front */
        4,5,6, 4,6,7,       /* Back */
        8,9,10, 8,10,11,    /* Left */
        12,13,14, 12,14,15, /* Right */
        16,17,18, 16,18,19, /* Top */
        20,21,22, 20,22,23  /* Bottom */
    };
    
    SDL_GPUBufferCreateInfo vertexBufInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(vertices)
    };
    vertexBuffer = SDL_CreateGPUBuffer(gpuDevice, &vertexBufInfo);
    
    SDL_GPUBufferCreateInfo indexBufInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = sizeof(indices)
    };
    indexBuffer = SDL_CreateGPUBuffer(gpuDevice, &indexBufInfo);
    
    if (!vertexBuffer || !indexBuffer) {
        SDL_Log("Failed to create buffers: %s", SDL_GetError());
        return 1;
    }
    
    /* Create transfer buffer and upload data */
    SDL_GPUTransferBufferCreateInfo transferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(vertices) + sizeof(indices)
    };
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpuDevice, &transferInfo);
    
    void *data = SDL_MapGPUTransferBuffer(gpuDevice, transfer, false);
    SDL_memcpy(data, vertices, sizeof(vertices));
    SDL_memcpy((Uint8*)data + sizeof(vertices), indices, sizeof(indices));
    SDL_UnmapGPUTransferBuffer(gpuDevice, transfer);
    
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpuDevice);
    SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmd);
    
    SDL_GPUTransferBufferLocation srcVertex = { .transfer_buffer = transfer, .offset = 0 };
    SDL_GPUBufferRegion dstVertex = { .buffer = vertexBuffer, .offset = 0, .size = sizeof(vertices) };
    SDL_UploadToGPUBuffer(copyPass, &srcVertex, &dstVertex, false);
    
    SDL_GPUTransferBufferLocation srcIndex = { .transfer_buffer = transfer, .offset = sizeof(vertices) };
    SDL_GPUBufferRegion dstIndex = { .buffer = indexBuffer, .offset = 0, .size = sizeof(indices) };
    SDL_UploadToGPUBuffer(copyPass, &srcIndex, &dstIndex, false);
    
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(gpuDevice, transfer);
    
    SDL_Log("Created cube vertex (%zu bytes) and index (%zu bytes) buffers", sizeof(vertices), sizeof(indices));
    return 0;
}

/* ========================================================================
 * OpenXR Function Loading
 * ======================================================================== */

/* Load OpenXR function pointers after instance is created */
static int LoadXRFunctions(void)
{
    pfn_xrGetInstanceProcAddr = SDL_OpenXR_GetXrGetInstanceProcAddr();
    if (!pfn_xrGetInstanceProcAddr) {
        SDL_Log("Failed to get xrGetInstanceProcAddr");
        return 1;
    }

#define XR_LOAD(fn) \
    if (XR_FAILED(pfn_xrGetInstanceProcAddr(xrInstance, #fn, (PFN_xrVoidFunction*)&pfn_##fn))) { \
        SDL_Log("Failed to load " #fn); \
        return 1; \
    }
    
    XR_LOAD(xrEnumerateViewConfigurationViews);
    XR_LOAD(xrEnumerateSwapchainImages);
    XR_LOAD(xrCreateReferenceSpace);
    XR_LOAD(xrDestroySpace);
    XR_LOAD(xrDestroySession);
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
    
#undef XR_LOAD
    
    SDL_Log("Loaded all XR functions successfully");
    return 0;
}

static int InitXRSession(void)
{
    XrResult result;
    
    /* Create session */
    XrSessionCreateInfo sessionCreateInfo = { XR_TYPE_SESSION_CREATE_INFO };
    result = SDL_CreateGPUXRSession(gpuDevice, &sessionCreateInfo, &xrSession);
    XR_ERR_LOG(result, "Failed to create XR session");
    
    SDL_Log("Created OpenXR session: %p", (void*)xrSession);
    
    /* Create reference space */
    XrReferenceSpaceCreateInfo spaceCreateInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f; /* Identity quaternion */
    
    result = pfn_xrCreateReferenceSpace(xrSession, &spaceCreateInfo, &xrLocalSpace);
    XR_ERR_LOG(result, "Failed to create reference space");
    
    return 0;
}

static int CreateSwapchains(void)
{
    XrResult result;
    
    /* Get view configuration */
    result = pfn_xrEnumerateViewConfigurationViews(
        xrInstance, xrSystemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        0, &viewCount, NULL);
    XR_ERR_LOG(result, "Failed to enumerate view config views (count)");
    
    SDL_Log("View count: %u", viewCount);
    
    XrViewConfigurationView *viewConfigs = SDL_calloc(viewCount, sizeof(XrViewConfigurationView));
    for (uint32_t i = 0; i < viewCount; i++) {
        viewConfigs[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    }
    
    result = pfn_xrEnumerateViewConfigurationViews(
        xrInstance, xrSystemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        viewCount, &viewCount, viewConfigs);
    XR_ERR_LOG(result, "Failed to enumerate view config views");
    
    /* Allocate swapchains and views */
    vrSwapchains = SDL_calloc(viewCount, sizeof(VRSwapchain));
    xrViews = SDL_calloc(viewCount, sizeof(XrView));
    
    for (uint32_t i = 0; i < viewCount; i++) {
        xrViews[i].type = XR_TYPE_VIEW;
        xrViews[i].pose.orientation.w = 1.0f;
        
        SDL_Log("View %u: recommended %ux%u", i,
                viewConfigs[i].recommendedImageRectWidth,
                viewConfigs[i].recommendedImageRectHeight);
        
        /* Create swapchain using OpenXR's XrSwapchainCreateInfo */
        XrSwapchainCreateInfo swapchainCreateInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        swapchainCreateInfo.format = 0; /* Let SDL pick the format */
        swapchainCreateInfo.sampleCount = 1;
        swapchainCreateInfo.width = viewConfigs[i].recommendedImageRectWidth;
        swapchainCreateInfo.height = viewConfigs[i].recommendedImageRectHeight;
        swapchainCreateInfo.faceCount = 1;
        swapchainCreateInfo.arraySize = 1;
        swapchainCreateInfo.mipCount = 1;
        
        result = SDL_CreateGPUXRSwapchain(
            gpuDevice,
            xrSession,
            &swapchainCreateInfo,
            &vrSwapchains[i].format,
            &vrSwapchains[i].swapchain,
            &vrSwapchains[i].images);
        
        if (XR_FAILED(result)) {
            SDL_Log("Failed to create swapchain %u", i);
            SDL_free(viewConfigs);
            return 1;
        }
        
        /* Get image count by enumerating swapchain images */
        result = pfn_xrEnumerateSwapchainImages(vrSwapchains[i].swapchain, 0, &vrSwapchains[i].imageCount, NULL);
        if (XR_FAILED(result)) {
            vrSwapchains[i].imageCount = 3; /* Assume 3 if we can't query */
        }
        
        vrSwapchains[i].size.width = (int32_t)swapchainCreateInfo.width;
        vrSwapchains[i].size.height = (int32_t)swapchainCreateInfo.height;
        
        SDL_Log("Created swapchain %u: %dx%d, %u images",
                i, vrSwapchains[i].size.width, vrSwapchains[i].size.height,
                vrSwapchains[i].imageCount);
    }
    
    SDL_free(viewConfigs);
    
    /* Create the pipeline using the swapchain format */
    if (viewCount > 0 && pipeline == NULL) {
        if (CreatePipeline(vrSwapchains[0].format) != 0) {
            return 1;
        }
        if (CreateCubeBuffers() != 0) {
            return 1;
        }
    }
    
    return 0;
}

static void HandleXREvents(void)
{
    XrEventDataBuffer eventBuffer = { XR_TYPE_EVENT_DATA_BUFFER };
    
    while (pfn_xrPollEvent(xrInstance, &eventBuffer) == XR_SUCCESS) {
        switch (eventBuffer.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                XrEventDataSessionStateChanged *stateEvent = 
                    (XrEventDataSessionStateChanged*)&eventBuffer;
                
                SDL_Log("Session state changed: %d", stateEvent->state);
                
                switch (stateEvent->state) {
                    case XR_SESSION_STATE_READY: {
                        XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
                        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        
                        XrResult result = pfn_xrBeginSession(xrSession, &beginInfo);
                        if (XR_SUCCEEDED(result)) {
                            SDL_Log("XR Session begun!");
                            xrSessionRunning = true;
                            
                            /* Create swapchains now that session is ready */
                            if (CreateSwapchains() != 0) {
                                SDL_Log("Failed to create swapchains");
                                xrShouldQuit = true;
                            }
                        }
                        break;
                    }
                    case XR_SESSION_STATE_STOPPING:
                        pfn_xrEndSession(xrSession);
                        xrSessionRunning = false;
                        break;
                    case XR_SESSION_STATE_EXITING:
                    case XR_SESSION_STATE_LOSS_PENDING:
                        xrShouldQuit = true;
                        break;
                    default:
                        break;
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                xrShouldQuit = true;
                break;
            default:
                break;
        }
        
        eventBuffer.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
}

static void RenderFrame(void)
{
    if (!xrSessionRunning) return;
    
    XrFrameState frameState = { XR_TYPE_FRAME_STATE };
    XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
    
    XrResult result = pfn_xrWaitFrame(xrSession, &waitInfo, &frameState);
    if (XR_FAILED(result)) return;
    
    XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
    result = pfn_xrBeginFrame(xrSession, &beginInfo);
    if (XR_FAILED(result)) return;
    
    XrCompositionLayerProjectionView *projViews = NULL;
    XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    uint32_t layerCount = 0;
    const XrCompositionLayerBaseHeader *layers[1] = {0};
    
    if (frameState.shouldRender && viewCount > 0 && vrSwapchains != NULL) {
        /* Update animation time - ~90fps for Quest */
        animTime += 0.011f;
        
        /* Locate views */
        XrViewState viewState = { XR_TYPE_VIEW_STATE };
        XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = frameState.predictedDisplayTime;
        locateInfo.space = xrLocalSpace;
        
        uint32_t viewCountOutput;
        result = pfn_xrLocateViews(xrSession, &locateInfo, &viewState, viewCount, &viewCountOutput, xrViews);
        if (XR_FAILED(result)) {
            SDL_Log("xrLocateViews failed");
            goto endFrame;
        }
        
        projViews = SDL_calloc(viewCount, sizeof(XrCompositionLayerProjectionView));
        
        SDL_GPUCommandBuffer *cmdBuf = SDL_AcquireGPUCommandBuffer(gpuDevice);
        
        for (uint32_t i = 0; i < viewCount; i++) {
            VRSwapchain *swapchain = &vrSwapchains[i];
            
            /* Acquire swapchain image */
            uint32_t imageIndex;
            XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            result = pfn_xrAcquireSwapchainImage(swapchain->swapchain, &acquireInfo, &imageIndex);
            if (XR_FAILED(result)) continue;
            
            XrSwapchainImageWaitInfo waitImageInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            waitImageInfo.timeout = XR_INFINITE_DURATION;
            result = pfn_xrWaitSwapchainImage(swapchain->swapchain, &waitImageInfo);
            if (XR_FAILED(result)) {
                XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                pfn_xrReleaseSwapchainImage(swapchain->swapchain, &releaseInfo);
                continue;
            }
            
            /* Render the scene */
            SDL_GPUTexture *targetTexture = swapchain->images[imageIndex];
            
            /* Build view and projection matrices from XR pose/fov */
            Mat4 viewMatrix = Mat4_FromXrPose(xrViews[i].pose);
            Mat4 projMatrix = Mat4_Projection(xrViews[i].fov, 0.05f, 100.0f);
            
            SDL_GPUColorTargetInfo colorTarget = {0};
            colorTarget.texture = targetTexture;
            colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
            colorTarget.store_op = SDL_GPU_STOREOP_STORE;
            /* Dark blue background */
            colorTarget.clear_color.r = 0.05f;
            colorTarget.clear_color.g = 0.05f;
            colorTarget.clear_color.b = 0.15f;
            colorTarget.clear_color.a = 1.0f;
            
            SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(cmdBuf, &colorTarget, 1, NULL);
            
            if (pipeline && vertexBuffer && indexBuffer) {
                SDL_BindGPUGraphicsPipeline(renderPass, pipeline);
                
                SDL_GPUViewport viewport = {0, 0, (float)swapchain->size.width, (float)swapchain->size.height, 0, 1};
                SDL_SetGPUViewport(renderPass, &viewport);
                
                SDL_Rect scissor = {0, 0, swapchain->size.width, swapchain->size.height};
                SDL_SetGPUScissor(renderPass, &scissor);
                
                SDL_GPUBufferBinding vertexBinding = {vertexBuffer, 0};
                SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBinding, 1);
                
                SDL_GPUBufferBinding indexBinding = {indexBuffer, 0};
                SDL_BindGPUIndexBuffer(renderPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                
                /* Draw each cube */
                for (int cubeIdx = 0; cubeIdx < NUM_CUBES; cubeIdx++) {
                    float rot = animTime * cubeSpeeds[cubeIdx];
                    Vec3 pos = cubePositions[cubeIdx];
                    
                    /* Build model matrix: scale -> rotateY -> rotateX -> translate */
                    Mat4 scale = Mat4_Scale(cubeScales[cubeIdx]);
                    Mat4 rotY = Mat4_RotationY(rot);
                    Mat4 rotX = Mat4_RotationX(rot * 0.7f);
                    Mat4 trans = Mat4_Translation(pos.x, pos.y, pos.z);
                    
                    Mat4 model = Mat4_Multiply(Mat4_Multiply(Mat4_Multiply(scale, rotY), rotX), trans);
                    Mat4 mv = Mat4_Multiply(model, viewMatrix);
                    Mat4 mvp = Mat4_Multiply(mv, projMatrix);
                    
                    SDL_PushGPUVertexUniformData(cmdBuf, 0, &mvp, sizeof(mvp));
                    SDL_DrawGPUIndexedPrimitives(renderPass, 36, 1, 0, 0, 0);
                }
            }
            
            SDL_EndGPURenderPass(renderPass);
            
            /* Release swapchain image */
            XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            pfn_xrReleaseSwapchainImage(swapchain->swapchain, &releaseInfo);
            
            /* Set up projection view */
            projViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projViews[i].pose = xrViews[i].pose;
            projViews[i].fov = xrViews[i].fov;
            projViews[i].subImage.swapchain = swapchain->swapchain;
            projViews[i].subImage.imageRect.offset.x = 0;
            projViews[i].subImage.imageRect.offset.y = 0;
            projViews[i].subImage.imageRect.extent = swapchain->size;
            projViews[i].subImage.imageArrayIndex = 0;
        }
        
        SDL_SubmitGPUCommandBuffer(cmdBuf);
        
        layer.space = xrLocalSpace;
        layer.viewCount = viewCount;
        layer.views = projViews;
        layers[0] = (XrCompositionLayerBaseHeader*)&layer;
        layerCount = 1;
    }
    
endFrame:;
    XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = layerCount;
    endInfo.layers = layers;
    
    pfn_xrEndFrame(xrSession, &endInfo);
    
    if (projViews) SDL_free(projViews);
}

static void Cleanup(void)
{
    SDL_Log("Cleaning up...");
    
    /* Release GPU resources first */
    if (pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(gpuDevice, pipeline);
        pipeline = NULL;
    }
    if (vertexBuffer) {
        SDL_ReleaseGPUBuffer(gpuDevice, vertexBuffer);
        vertexBuffer = NULL;
    }
    if (indexBuffer) {
        SDL_ReleaseGPUBuffer(gpuDevice, indexBuffer);
        indexBuffer = NULL;
    }
    
    if (vrSwapchains) {
        for (uint32_t i = 0; i < viewCount; i++) {
            if (vrSwapchains[i].swapchain) {
                SDL_DestroyGPUXRSwapchain(gpuDevice, vrSwapchains[i].swapchain, vrSwapchains[i].images);
            }
        }
        SDL_free(vrSwapchains);
    }
    
    if (xrViews) SDL_free(xrViews);
    
    if (xrLocalSpace && pfn_xrDestroySpace) pfn_xrDestroySpace(xrLocalSpace);
    if (xrSession && pfn_xrDestroySession) pfn_xrDestroySession(xrSession);
    
    if (gpuDevice) SDL_DestroyGPUDevice(gpuDevice);
    
    /* Note: xrInstance is managed by SDL */
    
    SDL_Quit();
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    SDL_Log("Quest VR Spinning Cubes Test starting...");
    
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    
    SDL_Log("SDL initialized");
    
    /* Create GPU device with OpenXR enabled */
    SDL_Log("Creating GPU device with OpenXR enabled...");
    
    /* Create GPU device WITHOUT OpenXR to test basic Vulkan */
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true);
    /* Re-enable XR */
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_ENABLE_BOOLEAN, true);
    SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_INSTANCE_POINTER, &xrInstance);
    SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_SYSTEM_ID_POINTER, &xrSystemId);
    SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_APPLICATION_NAME_STRING, "Quest VR Test");
    SDL_SetNumberProperty(props, SDL_PROP_GPU_DEVICE_CREATE_XR_APPLICATION_VERSION_NUMBER, 1);
    
    gpuDevice = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    
    if (!gpuDevice) {
        SDL_Log("Failed to create GPU device: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    
    SDL_Log("GPU device created, XR instance: %p, systemId: %llu", 
            (void*)(uintptr_t)xrInstance, (unsigned long long)xrSystemId);
    
    /* Load OpenXR function pointers */
    if (LoadXRFunctions() != 0) {
        SDL_Log("Failed to load XR functions");
        Cleanup();
        return 1;
    }
    
    /* Initialize XR session */
    if (InitXRSession() != 0) {
        SDL_Log("Failed to init XR session");
        Cleanup();
        return 1;
    }
    
    SDL_Log("Entering main loop...");
    
    /* Main loop */
    while (!xrShouldQuit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                xrShouldQuit = true;
            }
        }
        
        HandleXREvents();
        RenderFrame();
    }
    
    Cleanup();
    SDL_Log("Quest VR Test finished");
    return 0;
}
