#include "renderer.h"
#ifdef DOOM_FRAME_GENERATION
#include "frame_generation.h"
#include "presentation.h"
#endif
#ifdef DOOM_DLSS5
#include "dlss5.h"
#endif

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#define STB_EASY_FONT_IMPLEMENTATION
#include <stb_easy_font.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <math.h>
#include <float.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_HUD_VERTICES = 65536 };

static const uint32_t vertex_code[] = {
#include "vulkan.vert.inc"
};
static const uint32_t fragment_code[] = {
#include "vulkan.frag.inc"
};
static const uint32_t ray_fragment_code[] = {
#include "vulkan-ray.frag.inc"
};
static const uint32_t hud_vertex_code[] = {
#include "hud.vert.inc"
};
static const uint32_t hud_fragment_code[] = {
#include "hud.frag.inc"
};

typedef struct {
    float horizontal;
    float vertical;
    float depth;
    unsigned char color[4];
} EasyFontVertex;

static_assert(sizeof(EasyFontVertex) == 16, "stb_easy_font vertices must be 16 bytes");

typedef struct {
    VkBuffer handle;
    VkDeviceMemory memory;
    VkDeviceSize size;
    void *mapped;
} GpuBuffer;

typedef struct {
    VkImage handle;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat format;
} GpuImage;

typedef struct {
    float position[3];
    float previous_position[3];
    float texcoord[2];
    float material[2];
    float color[4];
} GpuVertex;

typedef struct {
    uint64_t vertices, materials;
    uint32_t info[4];
    float positions[4][4], colors[4][4];
    float sun_direction[4], sun_color[4];
} RayUniform;
static_assert(sizeof(RayUniform) == 192, "ray uniform layout must match GLSL");
static_assert(offsetof(GpuVertex, texcoord) == 6 * sizeof(float), "ray UV layout must match GLSL");

typedef struct {
    bool enabled, built, geometry_dirty;
    uint32_t triangles;
    GpuBuffer vertices, materials, uniform, instances, scratch, bottom_storage, top_storage;
    VkAccelerationStructureKHR bottom, top;
    VkDeviceAddress scratch_address;
    VkAccelerationStructureGeometryKHR geometry;
    VkAccelerationStructureGeometryKHR instance_geometry;
    VkDescriptorSetLayout layout;
    VkDescriptorPool pool;
    VkDescriptorSet descriptors;
    Position *light_positions;
    float *light_areas;
    PFN_vkCreateAccelerationStructureKHR create;
    PFN_vkDestroyAccelerationStructureKHR destroy;
    PFN_vkGetAccelerationStructureBuildSizesKHR sizes;
    PFN_vkGetAccelerationStructureDeviceAddressKHR address;
    PFN_vkCmdBuildAccelerationStructuresKHR build;
} RayLighting;

typedef struct {
    float current_matrix[16];
    float previous_matrix[16];
    float camera_position[4];
    float extent[4];
    float jitter[4];
} FrameUniform;

typedef struct {
    char *text;
    int horizontal;
    int vertical;
} QueuedText;

struct Renderer {
    GLFWwindow *window;
    bool ray_lighting;
    RayLighting *lighting;
    bool frame_generation;
    bool no_vsync;
#ifdef DOOM_FRAME_GENERATION
    DoomFrameGeneration *fg;
    Presentation *presentation;
    VkSemaphore fg_image_available;
    GpuBuffer fg_disable;
    GpuImage hudless_target;
    GpuImage interpolated_target;
    double fg_last_render_time, fg_frame_seconds;
    bool last_frame_generated;
#endif
    bool dlss_enabled;
    unsigned neural_style;
    unsigned neural_passes;
    unsigned upscale_mode, jitter_index;
    float jitter[2], previous_jitter[2];
#ifdef DOOM_DLSS5
    DoomDlss5 *dlss;
#endif
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceMemoryProperties memory_properties;
    VkDevice device;
    uint32_t graphics_family;
    uint32_t present_family;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available;
    VkFence frame_fence;
    VkQueryPool profile_pool;
    uint64_t profile_mask;
    float profile_period;
    unsigned profile_frames;
    bool profile_pending;
    double profile_last_time, profile_wall_ms, profile_stage_ms[4];
    atomic_uint validation_errors;
    VkSwapchainKHR swapchain;
    bool swapchain_dirty;
    VkFormat swapchain_format;
    VkExtent2D extent, work_extent;
    VkImage *swapchain_images;
    VkSemaphore *present_semaphores;
    bool *swapchain_initialized;
    uint32_t swapchain_image_count;
    VkSampler sampler;
    GpuImage *textures;
    GpuImage palette;
    size_t texture_count;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet *descriptor_sets;
    GpuBuffer uniform_buffer;
    VkPipelineLayout pipeline_layout;
    VkPipeline scene_pipeline;
    VkPipeline sky_pipeline;
    VkPipeline hud_pipeline;
    GpuImage color_target;
    GpuImage motion_target;
    GpuImage depth_target;
    GpuImage output_target;
    GpuImage display_target;
    GpuBuffer color_readback;
    GpuBuffer motion_readback;
    GpuBuffer vertex_buffer;
    GpuVertex *vertices;
    size_t vertex_capacity;
    size_t scene_vertex_count;
    size_t sprite_offset;
    size_t sky_offset;
    size_t hud_offset;
    size_t *batch_offsets;
    uint32_t *base_assets;
    Position *previous_scene_positions;
    Position *previous_sprite_positions;
    RenderCamera previous_camera;
    bool history_valid;
    bool targets_initialized;
    bool has_frame;
    QueuedText *texts;
    size_t text_count;
    size_t text_capacity;
    void *font_buffer;
    size_t font_buffer_size;
    size_t font_quads_per_character;
};

static const float pi_value = 3.14159265358979323846f;

static void require_vk(VkResult result, const char *operation) {
    if (result != VK_SUCCESS) fail("Vulkan %s failed (%d)", operation, (int)result);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT *data, void *user_data) {
    (void)types;
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) && user_data)
        atomic_fetch_add_explicit(&((Renderer *)user_data)->validation_errors, 1, memory_order_relaxed);
    fprintf(stderr, "Vulkan %s: %s\n", severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : "warning",
            data->pMessage ? data->pMessage : "validation message");
    return VK_FALSE;
}

static bool has_instance_layer(const char *name) {
    uint32_t count = 0;
    require_vk(vkEnumerateInstanceLayerProperties(&count, NULL), "layer enumeration");
    VkLayerProperties *properties = allocate(count, sizeof(*properties));
    require_vk(vkEnumerateInstanceLayerProperties(&count, properties), "layer enumeration");
    bool found = false;
    for (uint32_t index = 0; index < count; ++index)
        if (strcmp(properties[index].layerName, name) == 0) found = true;
    free(properties);
    return found;
}

static bool has_instance_extension(const char *name) {
    uint32_t count = 0;
    require_vk(vkEnumerateInstanceExtensionProperties(NULL, &count, NULL), "extension enumeration");
    VkExtensionProperties *properties = allocate(count, sizeof(*properties));
    require_vk(vkEnumerateInstanceExtensionProperties(NULL, &count, properties), "extension enumeration");
    bool found = false;
    for (uint32_t index = 0; index < count; ++index)
        if (strcmp(properties[index].extensionName, name) == 0) found = true;
    free(properties);
    return found;
}

static bool has_device_extension(VkPhysicalDevice device, const char *name) {
    uint32_t count = 0;
    require_vk(vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL), "device extension enumeration");
    VkExtensionProperties *properties = allocate(count, sizeof(*properties));
    require_vk(vkEnumerateDeviceExtensionProperties(device, NULL, &count, properties), "device extension enumeration");
    bool found = false;
    for (uint32_t index = 0; index < count; ++index)
        if (strcmp(properties[index].extensionName, name) == 0) found = true;
    free(properties);
    return found;
}

#ifdef DOOM_DLSS5
static VkExtensionProperties *dlss_extensions(Renderer *renderer, bool device, uint32_t *count) {
    char error[512];
    bool success = device ?
        doom_dlss5_get_device_extensions(L".", renderer->instance, renderer->physical_device,
                                        NULL, 0, count, error, sizeof(error)) :
        doom_dlss5_get_instance_extensions(L".", NULL, 0, count, error, sizeof(error));
    if (!success) fail("DLSS 5 extension query: %s", error);
    VkExtensionProperties *properties = allocate(*count, sizeof(*properties));
    success = device ?
        doom_dlss5_get_device_extensions(L".", renderer->instance, renderer->physical_device,
                                        properties, *count, count, error, sizeof(error)) :
        doom_dlss5_get_instance_extensions(L".", properties, *count, count, error, sizeof(error));
    if (!success) fail("DLSS 5 extension query: %s", error);
    return properties;
}

static VkExtensionProperties *append_dlss_extensions(Renderer *renderer, bool device,
                                                     const char ***names, uint32_t *count) {
    uint32_t extra_count = 0;
    VkExtensionProperties *properties = dlss_extensions(renderer, device, &extra_count);
    const char **combined = allocate((size_t)*count + extra_count, sizeof(*combined));
    memcpy(combined, *names, (size_t)*count * sizeof(*combined));
    free(*names);
    *names = combined;
    for (uint32_t extra = 0; extra < extra_count; ++extra) {
        const char *name = properties[extra].extensionName;
        bool present = false;
        for (uint32_t existing = 0; existing < *count; ++existing)
            if (!strcmp(name, combined[existing])) { present = true; break; }
        if (present) continue;
        bool supported = device ? has_device_extension(renderer->physical_device, name) : has_instance_extension(name);
        if (!supported) fail("DLSS 5 requires unavailable Vulkan extension %s", name);
        combined[(*count)++] = name;
    }
    return properties;
}
#endif

static void create_instance(Renderer *renderer, bool validation) {
    uint32_t loader_version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enumerate_version =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (enumerate_version) require_vk(enumerate_version(&loader_version), "loader version query");
    if (loader_version < VK_API_VERSION_1_3) fail("Vulkan 1.3 loader is required");

    uint32_t glfw_extension_count = 0;
    const char **glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if (!glfw_extensions || !glfw_extension_count) fail("GLFW did not provide Vulkan surface extensions");
    bool debug_utils = has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (validation && !has_instance_layer("VK_LAYER_KHRONOS_validation"))
        fail("Vulkan validation was requested but VK_LAYER_KHRONOS_validation is unavailable");
    if (validation && !debug_utils) fail("Vulkan validation needs VK_EXT_debug_utils");

    const char **extensions = allocate((size_t)glfw_extension_count + (validation ? 1u : 0u), sizeof(*extensions));
    for (uint32_t index = 0; index < glfw_extension_count; ++index) extensions[index] = glfw_extensions[index];
    if (validation) extensions[glfw_extension_count] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    uint32_t extension_count = glfw_extension_count + (validation ? 1u : 0u);
#ifdef DOOM_DLSS5
    VkExtensionProperties *dlss_properties = renderer->dlss_enabled ?
        append_dlss_extensions(renderer, false, &extensions, &extension_count) : NULL;
#endif
    const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Doom Vulkan",
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName = "doom-raylib",
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3
    };
    VkDebugUtilsMessengerCreateInfoEXT debug_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData = renderer
    };
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = validation ? &debug_info : NULL,
        .pApplicationInfo = &application,
        .enabledLayerCount = validation ? 1u : 0u,
        .ppEnabledLayerNames = validation ? layers : NULL,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions
    };
    require_vk(vkCreateInstance(&create_info, NULL, &renderer->instance), "instance creation");
    free(extensions);
#ifdef DOOM_DLSS5
    free(dlss_properties);
#endif
    if (validation) {
        PFN_vkCreateDebugUtilsMessengerEXT create_messenger =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(renderer->instance, "vkCreateDebugUtilsMessengerEXT");
        if (!create_messenger) fail("Cannot load Vulkan debug messenger entry point");
        require_vk(create_messenger(renderer->instance, &debug_info, NULL, &renderer->debug_messenger),
                   "debug messenger creation");
    }
    require_vk(glfwCreateWindowSurface(renderer->instance, renderer->window, NULL, &renderer->surface),
               "GLFW surface creation");
}

static bool format_supports(VkPhysicalDevice device, VkFormat format, VkFormatFeatureFlags required) {
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(device, format, &properties);
    return (properties.optimalTilingFeatures & required) == required;
}

static bool device_surface_format(VkPhysicalDevice device, VkSurfaceKHR surface) {
    uint32_t count = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, NULL) != VK_SUCCESS || !count) return false;
    VkSurfaceFormatKHR *formats = allocate(count, sizeof(*formats));
    VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats);
    bool supported = result == VK_SUCCESS;
    bool blit_target = false;
    for (uint32_t index = 0; supported && index < count; ++index) {
        if (formats[index].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;
        if (formats[index].format == VK_FORMAT_UNDEFINED) {
            blit_target = true;
            break;
        }
        if (formats[index].format != VK_FORMAT_B8G8R8A8_UNORM &&
            formats[index].format != VK_FORMAT_R8G8B8A8_UNORM) continue;
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(device, formats[index].format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) blit_target = true;
    }
    free(formats);
    return supported && blit_target;
}

static bool choose_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface,
                                  uint32_t *graphics_family, uint32_t *present_family) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    VkQueueFamilyProperties *families = allocate(count, sizeof(*families));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families);
    bool have_graphics = false, have_present = false;
    for (uint32_t index = 0; index < count; ++index) {
        VkBool32 can_present = VK_FALSE;
        require_vk(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &can_present), "surface support query");
        if (!have_graphics && (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            *graphics_family = index;
            have_graphics = true;
        }
        if (!have_present && can_present) {
            *present_family = index;
            have_present = true;
        }
    }
    free(families);
    return have_graphics && have_present;
}

static bool physical_device_suitable(VkPhysicalDevice device, VkSurfaceKHR surface,
                                     uint32_t *graphics_family, uint32_t *present_family) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3 ||
        !has_device_extension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
        !choose_queue_families(device, surface, graphics_family, present_family) ||
        !device_surface_format(device, surface)) return false;
    VkPhysicalDeviceVulkan13Features features13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features13};
    vkGetPhysicalDeviceFeatures2(device, &features);
    if (!features13.dynamicRendering || !features13.synchronization2 ||
        !features13.shaderDemoteToHelperInvocation || !features.features.independentBlend) return false;
    VkSurfaceCapabilitiesKHR capabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities) != VK_SUCCESS ||
        !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return false;
    VkFormatFeatureFlags color_features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                         VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    VkFormatFeatureFlags motion_features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    return format_supports(device, VK_FORMAT_R8G8B8A8_UNORM, color_features) &&
           format_supports(device, VK_FORMAT_R16G16_SFLOAT, motion_features) &&
           format_supports(device, VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

static bool ray_device_supported(VkPhysicalDevice device) {
    if (!has_device_extension(device, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) ||
        !has_device_extension(device, VK_KHR_RAY_QUERY_EXTENSION_NAME) ||
        !has_device_extension(device, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)) return false;
    VkPhysicalDeviceRayQueryFeaturesKHR query = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, .pNext = &query};
    VkPhysicalDeviceVulkan12Features features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &acceleration};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features12};
    vkGetPhysicalDeviceFeatures2(device, &features);
    return query.rayQuery && acceleration.accelerationStructure && features.features.shaderInt64 &&
        features12.bufferDeviceAddress && features12.runtimeDescriptorArray &&
        features12.shaderSampledImageArrayNonUniformIndexing;
}

static void select_physical_device(Renderer *renderer) {
    uint32_t count = 0;
    require_vk(vkEnumeratePhysicalDevices(renderer->instance, &count, NULL), "physical device enumeration");
    if (!count) fail("No Vulkan physical device is available");
    VkPhysicalDevice *devices = allocate(count, sizeof(*devices));
    require_vk(vkEnumeratePhysicalDevices(renderer->instance, &count, devices), "physical device enumeration");
    int best_score = -1;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t graphics_family = 0, present_family = 0;
        if (!physical_device_suitable(devices[index], renderer->surface, &graphics_family, &present_family)) continue;
        if (renderer->ray_lighting && !ray_device_supported(devices[index])) continue;
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[index], &properties);
        if ((renderer->dlss_enabled || renderer->frame_generation) && properties.vendorID != 0x10de) continue;
        int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
        if (score > best_score) {
            renderer->physical_device = devices[index];
            renderer->graphics_family = graphics_family;
            renderer->present_family = present_family;
            best_score = score;
        }
    }
    free(devices);
    if (renderer->physical_device == VK_NULL_HANDLE)
        fail(renderer->ray_lighting ? "No Vulkan device supports the requested ray lighting features" :
             renderer->dlss_enabled ? "No Vulkan device supports DLSS 5 neural rendering" :
                                     "No Vulkan 1.3 device supports this renderer");
    VkPhysicalDeviceProperties selected;
    vkGetPhysicalDeviceProperties(renderer->physical_device, &selected);
    printf("Vulkan GPU: %s\n", selected.deviceName);
    vkGetPhysicalDeviceMemoryProperties(renderer->physical_device, &renderer->memory_properties);
}

static void create_device(Renderer *renderer) {
    VkPhysicalDeviceVulkan12Features features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    if (renderer->dlss_enabled || renderer->ray_lighting) {
        VkPhysicalDeviceFeatures2 supported = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                               .pNext = &features12};
        vkGetPhysicalDeviceFeatures2(renderer->physical_device, &supported);
        if (!features12.bufferDeviceAddress) fail("DLSS 5 requires Vulkan buffer device addresses");
        features12 = (VkPhysicalDeviceVulkan12Features){
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .bufferDeviceAddress = VK_TRUE};
    }
    VkPhysicalDeviceRayQueryFeaturesKHR query = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
                                                .rayQuery = VK_TRUE};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &query, .accelerationStructure = VK_TRUE};
    if (renderer->ray_lighting) {
        features12.pNext = &acceleration;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    }
    float queue_priorities[] = {1.0f, 1.0f};
    bool split_queues = renderer->frame_generation && renderer->graphics_family == renderer->present_family;
    if (split_queues) {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &count, NULL);
        VkQueueFamilyProperties *families = allocate(count, sizeof(*families));
        vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &count, families);
        bool available = families[renderer->graphics_family].queueCount >= 2;
        free(families);
        if (!available) fail("Frame generation needs separate render and presentation queues");
    }
    VkDeviceQueueCreateInfo queues[2] = {
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueFamilyIndex = renderer->graphics_family, .queueCount = split_queues ? 2u : 1u, .pQueuePriorities = queue_priorities},
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueFamilyIndex = renderer->present_family, .queueCount = 1, .pQueuePriorities = queue_priorities}
    };
    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = renderer->dlss_enabled || renderer->ray_lighting ? &features12 : NULL,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE,
        .shaderDemoteToHelperInvocation = VK_TRUE
    };
    VkPhysicalDeviceFeatures features = {.independentBlend = VK_TRUE, .shaderInt64 = renderer->ray_lighting};
    uint32_t extension_count = renderer->ray_lighting ? 4u : 1u;
    const char **extensions = allocate(extension_count, sizeof(*extensions));
    extensions[0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (renderer->ray_lighting) {
        extensions[1] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        extensions[2] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
        extensions[3] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
    }
#ifdef DOOM_DLSS5
    VkExtensionProperties *dlss_properties = renderer->dlss_enabled ?
        append_dlss_extensions(renderer, true, &extensions, &extension_count) : NULL;
#endif
    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features13,
        .queueCreateInfoCount = renderer->graphics_family == renderer->present_family ? 1u : 2u,
        .pQueueCreateInfos = queues,
        .pEnabledFeatures = &features,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions
    };
    require_vk(vkCreateDevice(renderer->physical_device, &create_info, NULL, &renderer->device), "device creation");
    free(extensions);
#ifdef DOOM_DLSS5
    free(dlss_properties);
#endif
    vkGetDeviceQueue(renderer->device, renderer->graphics_family, 0, &renderer->graphics_queue);
    vkGetDeviceQueue(renderer->device, renderer->present_family, split_queues ? 1u : 0u, &renderer->present_queue);
    if (renderer->frame_generation && renderer->graphics_queue == renderer->present_queue)
        fail("Frame generation needs distinct render and presentation queue handles");
}

static uint32_t find_memory_type(Renderer *renderer, uint32_t type_bits, VkMemoryPropertyFlags flags) {
    for (uint32_t index = 0; index < renderer->memory_properties.memoryTypeCount; ++index)
        if ((type_bits & (1u << index)) &&
            (renderer->memory_properties.memoryTypes[index].propertyFlags & flags) == flags) return index;
    fail("No Vulkan memory type matches flags 0x%x", flags);
}

static GpuBuffer create_buffer(Renderer *renderer, VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags memory_flags, bool map) {
    GpuBuffer buffer = {.size = size};
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    require_vk(vkCreateBuffer(renderer->device, &create_info, NULL, &buffer.handle), "buffer creation");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(renderer->device, buffer.handle, &requirements);
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = find_memory_type(renderer, requirements.memoryTypeBits, memory_flags)
    };
    VkMemoryAllocateFlagsInfo flags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
                                       .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) allocation.pNext = &flags;
    require_vk(vkAllocateMemory(renderer->device, &allocation, NULL, &buffer.memory), "buffer memory allocation");
    require_vk(vkBindBufferMemory(renderer->device, buffer.handle, buffer.memory, 0), "buffer memory binding");
    if (map) require_vk(vkMapMemory(renderer->device, buffer.memory, 0, size, 0, &buffer.mapped), "buffer mapping");
    return buffer;
}

static void destroy_buffer(Renderer *renderer, GpuBuffer *buffer) {
    if (buffer->mapped) vkUnmapMemory(renderer->device, buffer->memory);
    if (buffer->handle) vkDestroyBuffer(renderer->device, buffer->handle, NULL);
    if (buffer->memory) vkFreeMemory(renderer->device, buffer->memory, NULL);
    *buffer = (GpuBuffer){0};
}

static GpuImage create_image(Renderer *renderer, uint32_t width, uint32_t height, VkFormat format,
                             VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    GpuImage image = {.format = format};
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    require_vk(vkCreateImage(renderer->device, &image_info, NULL, &image.handle), "image creation");
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(renderer->device, image.handle, &requirements);
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = find_memory_type(renderer, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    require_vk(vkAllocateMemory(renderer->device, &allocation, NULL, &image.memory), "image memory allocation");
    require_vk(vkBindImageMemory(renderer->device, image.handle, image.memory, 0), "image memory binding");
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.handle,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {.aspectMask = aspect, .baseMipLevel = 0, .levelCount = 1,
                             .baseArrayLayer = 0, .layerCount = 1}
    };
    require_vk(vkCreateImageView(renderer->device, &view_info, NULL, &image.view), "image view creation");
    return image;
}

static void destroy_image(Renderer *renderer, GpuImage *image) {
    if (image->view) vkDestroyImageView(renderer->device, image->view, NULL);
    if (image->handle) vkDestroyImage(renderer->device, image->handle, NULL);
    if (image->memory) vkFreeMemory(renderer->device, image->memory, NULL);
    *image = (GpuImage){0};
}

static void image_barrier(VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags aspect,
                          VkImageLayout old_layout, VkImageLayout new_layout,
                          VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
                          VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access) {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = source_stage,
        .srcAccessMask = source_access,
        .dstStageMask = destination_stage,
        .dstAccessMask = destination_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = aspect, .baseMipLevel = 0, .levelCount = 1,
                             .baseArrayLayer = 0, .layerCount = 1}
    };
    VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency);
}

static VkCommandBuffer begin_immediate(Renderer *renderer) {
    VkCommandBufferAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = renderer->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer command_buffer;
    require_vk(vkAllocateCommandBuffers(renderer->device, &allocation, &command_buffer), "temporary command allocation");
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    require_vk(vkBeginCommandBuffer(command_buffer, &begin), "temporary command begin");
    return command_buffer;
}

static void end_immediate(Renderer *renderer, VkCommandBuffer command_buffer) {
    require_vk(vkEndCommandBuffer(command_buffer), "temporary command end");
    VkCommandBufferSubmitInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = command_buffer
    };
    VkSubmitInfo2 submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &command_info
    };
    require_vk(vkQueueSubmit2(renderer->graphics_queue, 1, &submit, VK_NULL_HANDLE), "temporary queue submit");
    require_vk(vkQueueWaitIdle(renderer->graphics_queue), "temporary queue wait");
    vkFreeCommandBuffers(renderer->device, renderer->command_pool, 1, &command_buffer);
}

static void create_sampler(Renderer *renderer) {
    VkSamplerCreateInfo sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0,
        .anisotropyEnable = VK_FALSE,
        .compareEnable = VK_FALSE,
        .minLod = 0,
        .maxLod = 0,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };
    require_vk(vkCreateSampler(renderer->device, &sampler, NULL, &renderer->sampler), "sampler creation");
}

static void upload_textures(Renderer *renderer, const Scene *scene) {
    renderer->texture_count = scene->asset_count;
    renderer->textures = allocate(scene->asset_count, sizeof(*renderer->textures));
    VkDeviceSize total_size = sizeof(scene->lut);
    for (size_t index = 0; index < scene->asset_count; ++index) {
        const Asset *asset = &scene->assets[index];
        VkDeviceSize pixel_bytes = (VkDeviceSize)asset->width * asset->height * 4;
        if (total_size > UINT64_MAX - pixel_bytes) fail("Texture staging size overflow");
        total_size += pixel_bytes;
    }
    GpuBuffer staging = create_buffer(renderer, total_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    VkDeviceSize *offsets = allocate(scene->asset_count, sizeof(*offsets));
    memcpy(staging.mapped, scene->lut, sizeof(scene->lut));
    VkDeviceSize offset = sizeof(scene->lut);
    for (size_t index = 0; index < scene->asset_count; ++index) {
        const Asset *asset = &scene->assets[index];
        VkDeviceSize pixel_bytes = (VkDeviceSize)asset->width * asset->height * 4;
        offsets[index] = offset;
        memcpy((uint8_t *)staging.mapped + offset, asset->pixels, (size_t)pixel_bytes);
        renderer->textures[index] = create_image(renderer, asset->width, asset->height, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        offset += pixel_bytes;
    }
    renderer->palette = create_image(renderer, 256, 32, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    VkCommandBuffer command_buffer = begin_immediate(renderer);
    image_barrier(command_buffer, renderer->palette.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy palette_copy = {
        .bufferOffset = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0,
                             .baseArrayLayer = 0, .layerCount = 1},
        .imageExtent = {256, 32, 1}
    };
    vkCmdCopyBufferToImage(command_buffer, staging.handle, renderer->palette.handle,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &palette_copy);
    image_barrier(command_buffer, renderer->palette.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    for (size_t index = 0; index < scene->asset_count; ++index) {
        GpuImage *image = &renderer->textures[index];
        const Asset *asset = &scene->assets[index];
        image_barrier(command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy copy = {
            .bufferOffset = offsets[index],
            .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0,
                                 .baseArrayLayer = 0, .layerCount = 1},
            .imageExtent = {asset->width, asset->height, 1}
        };
        vkCmdCopyBufferToImage(command_buffer, staging.handle, image->handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        image_barrier(command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    end_immediate(renderer, command_buffer);
    free(offsets);
    destroy_buffer(renderer, &staging);
}

static VkFormat choose_swapchain_format(Renderer *renderer, const VkSurfaceFormatKHR *formats, uint32_t count,
                                        VkColorSpaceKHR *color_space) {
    const VkFormat preferred[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    for (size_t choice = 0; choice < sizeof(preferred) / sizeof(preferred[0]); ++choice) {
        for (uint32_t index = 0; index < count; ++index) {
            if (formats[index].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;
            VkFormat format = formats[index].format == VK_FORMAT_UNDEFINED ? preferred[choice] : formats[index].format;
            if (format != preferred[choice]) continue;
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(renderer->physical_device, format, &properties);
            if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) {
                *color_space = formats[index].colorSpace;
                return format;
            }
        }
    }
    fail("No nonlinear UNORM swapchain format preserves Doom palette colors");
}

static void destroy_swapchain(Renderer *renderer) {
    for (uint32_t index = 0; index < renderer->swapchain_image_count; ++index)
        if (renderer->present_semaphores && renderer->present_semaphores[index])
            vkDestroySemaphore(renderer->device, renderer->present_semaphores[index], NULL);
    free(renderer->swapchain_images);
    free(renderer->present_semaphores);
    free(renderer->swapchain_initialized);
    renderer->swapchain_images = NULL;
    renderer->present_semaphores = NULL;
    renderer->swapchain_initialized = NULL;
    renderer->swapchain_image_count = 0;
    if (renderer->swapchain) vkDestroySwapchainKHR(renderer->device, renderer->swapchain, NULL);
    renderer->swapchain = VK_NULL_HANDLE;
}

static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported) {
    const VkCompositeAlphaFlagBitsKHR choices[] = {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    for (size_t index = 0; index < sizeof(choices) / sizeof(choices[0]); ++index)
        if (supported & (VkCompositeAlphaFlagsKHR)choices[index]) return choices[index];
    fail("No supported swapchain composite alpha mode");
}

static bool create_swapchain(Renderer *renderer) {
    VkSurfaceCapabilitiesKHR capabilities;
    require_vk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer->physical_device, renderer->surface, &capabilities),
               "surface capabilities query");
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        fail("The Vulkan surface does not support swapchain blit targets");
    uint32_t format_count = 0;
    require_vk(vkGetPhysicalDeviceSurfaceFormatsKHR(renderer->physical_device, renderer->surface, &format_count, NULL),
               "surface format query");
    VkSurfaceFormatKHR *formats = allocate(format_count, sizeof(*formats));
    require_vk(vkGetPhysicalDeviceSurfaceFormatsKHR(renderer->physical_device, renderer->surface, &format_count, formats),
               "surface format query");
    VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkFormat format = choose_swapchain_format(renderer, formats, format_count, &color_space);
    free(formats);

    int framebuffer_width = 0, framebuffer_height = 0;
    glfwGetFramebufferSize(renderer->window, &framebuffer_width, &framebuffer_height);
    if (framebuffer_width <= 0 || framebuffer_height <= 0 || glfwWindowShouldClose(renderer->window)) return false;
    VkExtent2D extent;
    if (capabilities.currentExtent.width != UINT32_MAX) extent = capabilities.currentExtent;
    else {
        extent.width = (uint32_t)framebuffer_width;
        extent.height = (uint32_t)framebuffer_height;
        if (extent.width < capabilities.minImageExtent.width) extent.width = capabilities.minImageExtent.width;
        if (extent.width > capabilities.maxImageExtent.width) extent.width = capabilities.maxImageExtent.width;
        if (extent.height < capabilities.minImageExtent.height) extent.height = capabilities.minImageExtent.height;
        if (extent.height > capabilities.maxImageExtent.height) extent.height = capabilities.maxImageExtent.height;
    }
    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && image_count > capabilities.maxImageCount) image_count = capabilities.maxImageCount;
    uint32_t families[] = {renderer->graphics_family, renderer->present_family};
    bool immediate_present = renderer->frame_generation || renderer->no_vsync;
    if (immediate_present) {
        uint32_t mode_count = 0;
        require_vk(vkGetPhysicalDeviceSurfacePresentModesKHR(renderer->physical_device, renderer->surface,
            &mode_count, NULL), "present mode count");
        VkPresentModeKHR *modes = allocate(mode_count, sizeof(*modes));
        require_vk(vkGetPhysicalDeviceSurfacePresentModesKHR(renderer->physical_device, renderer->surface,
            &mode_count, modes), "present mode query");
        bool immediate = false;
        for (uint32_t index = 0; index < mode_count; ++index)
            immediate |= modes[index] == VK_PRESENT_MODE_IMMEDIATE_KHR;
        free(modes);
        if (!immediate) fail("Immediate presentation is not supported by this surface");
    }
    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = renderer->surface,
        .minImageCount = image_count,
        .imageFormat = format,
        .imageColorSpace = color_space,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = renderer->graphics_family == renderer->present_family ? VK_SHARING_MODE_EXCLUSIVE :
                            VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = renderer->graphics_family == renderer->present_family ? 0u : 2u,
        .pQueueFamilyIndices = renderer->graphics_family == renderer->present_family ? NULL : families,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = choose_composite_alpha(capabilities.supportedCompositeAlpha),
        .presentMode = immediate_present ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = renderer->swapchain
    };
    VkSwapchainKHR next_swapchain;
    require_vk(vkCreateSwapchainKHR(renderer->device, &create_info, NULL, &next_swapchain), "swapchain creation");
    uint32_t next_count = 0;
    require_vk(vkGetSwapchainImagesKHR(renderer->device, next_swapchain, &next_count, NULL), "swapchain image query");
    VkImage *next_images = allocate(next_count, sizeof(*next_images));
    VkSemaphore *next_semaphores = allocate(next_count, sizeof(*next_semaphores));
    bool *next_initialized = allocate(next_count, sizeof(*next_initialized));
    require_vk(vkGetSwapchainImagesKHR(renderer->device, next_swapchain, &next_count, next_images), "swapchain image query");
    for (uint32_t index = 0; index < next_count; ++index) {
        VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        require_vk(vkCreateSemaphore(renderer->device, &semaphore_info, NULL, &next_semaphores[index]),
                   "present semaphore creation");
    }
    destroy_swapchain(renderer);
    renderer->swapchain = next_swapchain;
    renderer->swapchain_format = format;
    renderer->extent = extent;
    renderer->swapchain_images = next_images;
    renderer->present_semaphores = next_semaphores;
    renderer->swapchain_initialized = next_initialized;
    renderer->swapchain_image_count = next_count;
    return true;
}

static void destroy_targets(Renderer *renderer) {
#ifdef DOOM_FRAME_GENERATION
    destroy_image(renderer, &renderer->hudless_target);
    destroy_image(renderer, &renderer->interpolated_target);
#endif
    destroy_image(renderer, &renderer->color_target);
    destroy_image(renderer, &renderer->motion_target);
    destroy_image(renderer, &renderer->depth_target);
    destroy_image(renderer, &renderer->output_target);
    destroy_image(renderer, &renderer->display_target);
    destroy_buffer(renderer, &renderer->color_readback);
    destroy_buffer(renderer, &renderer->motion_readback);
    renderer->targets_initialized = false;
    renderer->has_frame = false;
}

static void create_targets(Renderer *renderer) {
    renderer->work_extent = renderer->extent;
#ifdef DOOM_DLSS5
    if (renderer->upscale_mode) {
        char error[512];
        if (!doom_dlss5_render_size(renderer->dlss, (DoomDlss5SRMode)renderer->upscale_mode, renderer->extent,
                                    &renderer->work_extent, error, sizeof(error)))
            fail("Super Resolution render size: %s", error);
        printf("Super Resolution: %ux%u -> %ux%u\n", renderer->work_extent.width, renderer->work_extent.height,
               renderer->extent.width, renderer->extent.height);
    }
#endif
    uint32_t width = renderer->work_extent.width, height = renderer->work_extent.height;
    VkFormatProperties color_properties, motion_properties, depth_properties;
    vkGetPhysicalDeviceFormatProperties(renderer->physical_device, VK_FORMAT_R8G8B8A8_UNORM, &color_properties);
    vkGetPhysicalDeviceFormatProperties(renderer->physical_device, VK_FORMAT_R16G16_SFLOAT, &motion_properties);
    vkGetPhysicalDeviceFormatProperties(renderer->physical_device, VK_FORMAT_D32_SFLOAT, &depth_properties);
    if (renderer->dlss_enabled &&
        (!(color_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
         !(color_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
         !(motion_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
         !(depth_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)))
        fail("DLSS 5 requires sampled color, motion, depth, and a storage output");
    VkImageUsageFlags color_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageUsageFlags motion_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImageUsageFlags depth_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (color_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
        color_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (color_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
        color_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (motion_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
        motion_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (motion_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
        motion_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (depth_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
        depth_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageUsageFlags output_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (color_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
        output_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (color_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
        output_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    renderer->color_target = create_image(renderer, width, height, VK_FORMAT_R8G8B8A8_UNORM,
        color_usage, VK_IMAGE_ASPECT_COLOR_BIT);
    renderer->motion_target = create_image(renderer, width, height, VK_FORMAT_R16G16_SFLOAT,
        motion_usage, VK_IMAGE_ASPECT_COLOR_BIT);
    renderer->depth_target = create_image(renderer, width, height, VK_FORMAT_D32_SFLOAT, depth_usage,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    renderer->output_target = create_image(renderer, width, height, VK_FORMAT_R8G8B8A8_UNORM,
        output_usage, VK_IMAGE_ASPECT_COLOR_BIT);
    renderer->display_target = create_image(renderer, renderer->extent.width, renderer->extent.height,
        VK_FORMAT_R8G8B8A8_UNORM, output_usage, VK_IMAGE_ASPECT_COLOR_BIT);
#ifdef DOOM_FRAME_GENERATION
    if (renderer->fg) {
        renderer->hudless_target = create_image(renderer, renderer->extent.width, renderer->extent.height,
            VK_FORMAT_R8G8B8A8_UNORM, output_usage, VK_IMAGE_ASPECT_COLOR_BIT);
        renderer->interpolated_target = create_image(renderer, renderer->extent.width, renderer->extent.height,
            VK_FORMAT_R8G8B8A8_UNORM, output_usage, VK_IMAGE_ASPECT_COLOR_BIT);
        VkCommandBuffer command = begin_immediate(renderer);
        char error[512];
        if (!doom_frame_generation_resize(renderer->fg, command, renderer->extent,
                VK_FORMAT_R8G8B8A8_UNORM, error, sizeof(error)))
            fail("Frame generation resize: %s", error);
        end_immediate(renderer, command);
    }
#endif
    VkDeviceSize pixels = (VkDeviceSize)width * height;
    VkDeviceSize display_pixels = (VkDeviceSize)renderer->extent.width * renderer->extent.height;
    renderer->color_readback = create_buffer(renderer, display_pixels * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    renderer->motion_readback = create_buffer(renderer, pixels * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
}

static void create_shader(Renderer *renderer, const uint32_t *code, size_t size, VkShaderModule *module) {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code
    };
    require_vk(vkCreateShaderModule(renderer->device, &create_info, NULL, module), "shader module creation");
}

static VkPipeline create_graphics_pipeline(Renderer *renderer, VkShaderModule vertex_shader,
    VkShaderModule fragment_shader, bool depth_test, bool depth_write, bool hud) {
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vertex_shader, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragment_shader, .pName = "main"}
    };
    VkVertexInputBindingDescription binding = {
        .binding = 0, .stride = sizeof(GpuVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription attributes[5] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(GpuVertex, position)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(GpuVertex, previous_position)},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(GpuVertex, texcoord)},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(GpuVertex, material)},
        {.location = 4, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(GpuVertex, color)}
    };
    VkVertexInputAttributeDescription hud_attributes[] = {attributes[0], attributes[4]};
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = hud ? 2u : 4u,
        .pVertexAttributeDescriptions = hud ? hud_attributes : attributes
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };
    VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_test,
        .depthWriteEnable = depth_write,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE
    };
    VkPipelineColorBlendAttachmentState blend_attachments[2] = {
        {.blendEnable = hud, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
         .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD,
         .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
         .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT},
        {.blendEnable = VK_FALSE, .colorWriteMask = hud ? 0u :
             VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT}
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = hud ? 1u : 2u,
        .pAttachments = blend_attachments
    };
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states
    };
    VkFormat formats[] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16_SFLOAT};
    VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = hud ? 1u : 2u,
        .pColorAttachmentFormats = formats,
        .depthAttachmentFormat = hud ? VK_FORMAT_UNDEFINED : VK_FORMAT_D32_SFLOAT
    };
    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = renderer->pipeline_layout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0
    };
    VkPipeline pipeline;
    require_vk(vkCreateGraphicsPipelines(renderer->device, VK_NULL_HANDLE, 1, &create_info, NULL, &pipeline),
               "graphics pipeline creation");
    return pipeline;
}

static void create_pipelines(Renderer *renderer) {
    VkDescriptorSetLayoutBinding bindings[3] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT}
    };
    VkDescriptorSetLayoutCreateInfo descriptor_layout = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3,
        .pBindings = bindings
    };
    require_vk(vkCreateDescriptorSetLayout(renderer->device, &descriptor_layout, NULL,
                                            &renderer->descriptor_layout), "descriptor layout creation");
    VkDescriptorSetLayout layouts[] = {renderer->descriptor_layout,
        renderer->lighting ? renderer->lighting->layout : VK_NULL_HANDLE};
    VkPipelineLayoutCreateInfo pipeline_layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = renderer->lighting ? 2u : 1u,
        .pSetLayouts = layouts
    };
    require_vk(vkCreatePipelineLayout(renderer->device, &pipeline_layout, NULL, &renderer->pipeline_layout),
               "pipeline layout creation");
    VkShaderModule vertex_shader, fragment_shader, hud_vertex, hud_fragment;
    create_shader(renderer, vertex_code, sizeof(vertex_code), &vertex_shader);
    create_shader(renderer, renderer->lighting ? ray_fragment_code : fragment_code,
        renderer->lighting ? sizeof(ray_fragment_code) : sizeof(fragment_code), &fragment_shader);
    renderer->scene_pipeline = create_graphics_pipeline(renderer, vertex_shader, fragment_shader, true, true, false);
    renderer->sky_pipeline = create_graphics_pipeline(renderer, vertex_shader, fragment_shader, false, false, false);
    create_shader(renderer, hud_vertex_code, sizeof(hud_vertex_code), &hud_vertex);
    create_shader(renderer, hud_fragment_code, sizeof(hud_fragment_code), &hud_fragment);
    renderer->hud_pipeline = create_graphics_pipeline(renderer, hud_vertex, hud_fragment, false, false, true);
    vkDestroyShaderModule(renderer->device, hud_vertex, NULL);
    vkDestroyShaderModule(renderer->device, hud_fragment, NULL);
    vkDestroyShaderModule(renderer->device, vertex_shader, NULL);
    vkDestroyShaderModule(renderer->device, fragment_shader, NULL);
}

static void create_descriptors(Renderer *renderer) {
    uint32_t count = (uint32_t)renderer->texture_count;
    VkDescriptorPoolSize sizes[2] = {
        {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = count * 2},
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = count}
    };
    VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = count,
        .poolSizeCount = 2,
        .pPoolSizes = sizes
    };
    require_vk(vkCreateDescriptorPool(renderer->device, &pool, NULL, &renderer->descriptor_pool),
               "descriptor pool creation");
    VkDescriptorSetLayout *layouts = allocate(count, sizeof(*layouts));
    renderer->descriptor_sets = allocate(count, sizeof(*renderer->descriptor_sets));
    for (uint32_t index = 0; index < count; ++index) layouts[index] = renderer->descriptor_layout;
    VkDescriptorSetAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = renderer->descriptor_pool,
        .descriptorSetCount = count,
        .pSetLayouts = layouts
    };
    require_vk(vkAllocateDescriptorSets(renderer->device, &allocation, renderer->descriptor_sets),
               "descriptor set allocation");
    free(layouts);
    for (uint32_t index = 0; index < count; ++index) {
        VkDescriptorImageInfo texture = {.sampler = renderer->sampler,
            .imageView = renderer->textures[index].view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo palette = {.sampler = renderer->sampler,
            .imageView = renderer->palette.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo uniform = {.buffer = renderer->uniform_buffer.handle,
            .offset = 0, .range = sizeof(FrameUniform)};
        VkWriteDescriptorSet writes[3] = {
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = renderer->descriptor_sets[index],
             .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .pImageInfo = &texture},
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = renderer->descriptor_sets[index],
             .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .pImageInfo = &palette},
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = renderer->descriptor_sets[index],
             .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
             .pBufferInfo = &uniform}
        };
        vkUpdateDescriptorSets(renderer->device, 3, writes, 0, NULL);
    }
}

static Position vector_subtract(Position first, Position second) {
    return (Position){first.x - second.x, first.y - second.y, first.z - second.z};
}

static Position vector_cross(Position first, Position second) {
    return (Position){first.y * second.z - first.z * second.y,
                      first.z * second.x - first.x * second.z,
                      first.x * second.y - first.y * second.x};
}

static float vector_dot(Position first, Position second) {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

static Position vector_normalize(Position value) {
    float length = sqrtf(vector_dot(value, value));
    if (length < 1e-8f) return (Position){1, 0, 0};
    return (Position){value.x / length, value.y / length, value.z / length};
}

static void make_view_projection(const RenderCamera *camera, float aspect, float matrix[16]) {
    Position position = {camera->position.x, camera->position.y, camera->position.z};
    Position target = {camera->target.x, camera->target.y, camera->target.z};
    Position forward = vector_normalize(vector_subtract(target, position));
    Position right = vector_normalize(vector_cross(forward, (Position){0, 1, 0}));
    Position up = vector_cross(right, forward);
    float view[16] = {
        right.x, up.x, -forward.x, 0,
        right.y, up.y, -forward.y, 0,
        right.z, up.z, -forward.z, 0,
        -vector_dot(right, position), -vector_dot(up, position), vector_dot(forward, position), 1
    };
    float fovy = camera->fovy;
    if (!isfinite(fovy) || fovy < 1.0f || fovy > 179.0f) fovy = 70.0f;
    float focal = 1.0f / tanf(fovy * pi_value / 360.0f);
    float near_plane = 0.02f, far_plane = 10000.0f;
    float projection[16] = {
        focal / aspect, 0, 0, 0,
        0, -focal, 0, 0,
        0, 0, far_plane / (near_plane - far_plane), -1,
        0, 0, (far_plane * near_plane) / (near_plane - far_plane), 0
    };
    memset(matrix, 0, sizeof(float) * 16);
    for (unsigned column = 0; column < 4; ++column)
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned inner = 0; inner < 4; ++inner)
                matrix[column * 4 + row] += projection[inner * 4 + row] * view[column * 4 + inner];
}

static unsigned sector_shade(const SceneSector *sector) {
    return (255u - sector->light) * 31u / 255u;
}

static void fill_vertex(GpuVertex *vertex, Position position, Position previous, float texture_u, float texture_v,
                        unsigned shade, float kind) {
    vertex->position[0] = position.x;
    vertex->position[1] = position.y;
    vertex->position[2] = position.z;
    vertex->previous_position[0] = previous.x;
    vertex->previous_position[1] = previous.y;
    vertex->previous_position[2] = previous.z;
    vertex->texcoord[0] = texture_u;
    vertex->texcoord[1] = texture_v;
    vertex->material[0] = (float)shade;
    vertex->material[1] = kind;
    vertex->color[0] = 1;
    vertex->color[1] = 1;
    vertex->color[2] = 1;
    vertex->color[3] = 1;
}

static void update_switch_assets(Scene *scene, const Specials *specials, const uint32_t *base_assets) {
    for (size_t index = 0; index < scene->wall_count; ++index) {
        const WallSurface *wall = &scene->walls[index];
        if (wall->switch_asset == UINT32_MAX) continue;
        bool active = !scene->lines[wall->line].special ||
                     (specials && specials->switch_ticks && specials->switch_ticks[wall->line]);
        scene->batches[wall->batch].asset = active ? wall->switch_asset : base_assets[wall->batch];
    }
}

static void build_scene_vertices(Renderer *renderer, Scene *scene, const Specials *specials) {
    update_switch_assets(scene, specials, renderer->base_assets);
    for (size_t batch_index = 0; batch_index < scene->batch_count; ++batch_index) {
        Batch *batch = &scene->batches[batch_index];
        size_t offset = renderer->batch_offsets[batch_index];
        unsigned shade = batch->shade;
        float plane_height = 0;
        bool plane = batch->plane == SURFACE_FLOOR || batch->plane == SURFACE_CEILING;
        if (plane) {
            const SceneSector *sector = &scene->sectors[batch->owner];
            shade = sector_shade(sector);
            plane_height = batch->plane == SURFACE_FLOOR ? sector->floor : sector->ceiling;
        }
        for (size_t vertex_index = 0; vertex_index < batch->count; ++vertex_index) {
            const SceneVertex *source = &batch->vertices[vertex_index];
            Position position = {source->x, plane ? plane_height : source->y, source->z};
            Position previous = position;
            if (renderer->history_valid) previous = renderer->previous_scene_positions[offset + vertex_index];
            fill_vertex(&renderer->vertices[offset + vertex_index], position, previous, source->u, source->v,
                        shade, batch->sky ? 1.0f : 0.0f);
        }
    }
    for (size_t wall_index = 0; wall_index < scene->wall_count; ++wall_index) {
        const WallSurface *wall = &scene->walls[wall_index];
        const CollisionLine *line = &scene->lines[wall->line];
        uint32_t sector_index = wall->side ? line->back : line->front;
        if (sector_index == UINT32_MAX) continue;
        SceneVertex wall_points[6];
        wall_vertices(scene, wall, wall_points);
        size_t batch_offset = renderer->batch_offsets[wall->batch] + wall->first;
        unsigned shade = sector_shade(&scene->sectors[sector_index]);
        for (size_t corner = 0; corner < 6; ++corner) {
            const SceneVertex *point = &wall_points[corner];
            size_t vertex_index = batch_offset + corner;
            Position position = {point->x, point->y, point->z};
            Position previous = position;
            if (renderer->history_valid) previous = renderer->previous_scene_positions[vertex_index];
            fill_vertex(&renderer->vertices[vertex_index], position, previous, point->u, point->v,
                        shade, scene->batches[wall->batch].sky ? 1.0f : 0.0f);
        }
    }
    for (size_t index = 0; index < renderer->scene_vertex_count; ++index) {
        GpuVertex *vertex = &renderer->vertices[index];
        Position prior = renderer->previous_scene_positions[index];
        if (renderer->lighting && (prior.x != vertex->position[0] || prior.y != vertex->position[1] || prior.z != vertex->position[2]))
            renderer->lighting->geometry_dirty = true;
        renderer->previous_scene_positions[index] =
            (Position){vertex->position[0], vertex->position[1], vertex->position[2]};
    }
}

static void build_sprites(Renderer *renderer, const Scene *scene, const RenderCamera *camera) {
    float sprite_yaw = atan2f(-(camera->target.z - camera->position.z), camera->target.x - camera->position.x) - pi_value * 0.5f;
    float current_cosine = cosf(sprite_yaw), current_sine = sinf(sprite_yaw);
    for (size_t sprite_index = 0; sprite_index < scene->sprite_count; ++sprite_index) {
        const Sprite *sprite = &scene->sprites[sprite_index];
        const Asset *asset = &scene->assets[sprite->asset];
        float left = -(float)asset->left / 32.0f;
        float right = ((float)asset->width - (float)asset->left) / 32.0f;
        float top = (float)asset->top / 32.0f;
        float bottom = ((float)asset->top - (float)asset->height) / 32.0f;
        const float local_x[6] = {left, right, right, left, right, left};
        const float local_y[6] = {bottom, bottom, top, bottom, top, top};
        const float tex_u[6] = {0, 1, 1, 0, 1, 0};
        const float tex_v[6] = {1, 1, 0, 1, 0, 0};
        unsigned shade = sprite->fullbright ? 0 : sector_shade(&scene->sectors[sprite->sector]);
        size_t first = renderer->sprite_offset + sprite_index * 6;
        for (size_t corner = 0; corner < 6; ++corner) {
            Position current = {
                sprite->position.x + current_cosine * local_x[corner],
                sprite->position.y + local_y[corner],
                sprite->position.z - current_sine * local_x[corner]
            };
            Position previous = current;
            if (renderer->history_valid) previous = renderer->previous_sprite_positions[sprite_index * 6 + corner];
            fill_vertex(&renderer->vertices[first + corner], current, previous, tex_u[corner], tex_v[corner],
                        shade, sprite->fullbright ? 2.0f : 0.0f);
        }
        for (size_t corner = 0; corner < 6; ++corner) {
            GpuVertex *vertex = &renderer->vertices[first + corner];
            renderer->previous_sprite_positions[sprite_index * 6 + corner] =
                (Position){vertex->position[0], vertex->position[1], vertex->position[2]};
        }
    }
}

static void build_sky(Renderer *renderer, const RenderCamera *camera) {
    const float radius = 4096.0f;
    const Position corners[8] = {
        {-radius, -radius, -radius}, {radius, -radius, -radius},
        {radius, radius, -radius}, {-radius, radius, -radius},
        {-radius, -radius, radius}, {radius, -radius, radius},
        {radius, radius, radius}, {-radius, radius, radius}
    };
    const unsigned faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 4, 7, 3},
        {1, 5, 6, 2}, {0, 1, 5, 4}, {3, 2, 6, 7}};
    const unsigned order[6] = {0, 1, 2, 0, 2, 3};
    Position old_camera = renderer->history_valid ? renderer->previous_camera.position : camera->position;
    for (size_t face = 0; face < 6; ++face) {
        for (size_t corner = 0; corner < 6; ++corner) {
            Position direction = corners[faces[face][order[corner]]];
            Position position = {camera->position.x + direction.x, camera->position.y + direction.y,
                                 camera->position.z + direction.z};
            Position previous = {old_camera.x + direction.x, old_camera.y + direction.y, old_camera.z + direction.z};
            fill_vertex(&renderer->vertices[renderer->sky_offset + face * 6 + corner], position, previous, 0, 0, 0, 1);
        }
    }
}

static float append_text(Renderer *renderer, const QueuedText *queued, size_t *hud_count) {
    const float scale = 5;
    size_t length = strlen(queued->text);
    if (length > (SIZE_MAX - 1) / 2) fail("HUD text is too long");
    char *wrapped = allocate(length * 2 + 1, 1);
    size_t written = 0;
    float line_width = 0;
    float available = fmaxf(1, ((float)renderer->extent.width - (float)queued->horizontal - 12) / scale);
    for (size_t index = 0; index < length; ++index) {
        char glyph[2] = {queued->text[index], '\0'};
        float width = (float)stb_easy_font_width(glyph);
        if (glyph[0] != '\n' && line_width > 0 && line_width + width > available) {
            wrapped[written++] = '\n';
            line_width = 0;
        }
        wrapped[written++] = glyph[0];
        line_width = glyph[0] == '\n' ? 0 : line_width + width;
    }
    wrapped[written] = '\0';
    float height = (float)stb_easy_font_height(wrapped) * scale;
    length = written;
    if (renderer->font_quads_per_character && length > (size_t)INT_MAX /
        (renderer->font_quads_per_character * 64)) fail("HUD text is too long");
    size_t output_size = length * renderer->font_quads_per_character * 64;
    if (output_size > (size_t)INT_MAX) fail("HUD text is too long");
    if (output_size > renderer->font_buffer_size) {
        size_t next_size = output_size > renderer->font_buffer_size * 2 ? output_size : renderer->font_buffer_size * 2;
        if (!next_size) next_size = 1024;
        void *next_buffer = realloc(renderer->font_buffer, next_size);
        if (!next_buffer) fail("Out of memory for HUD font geometry");
        renderer->font_buffer = next_buffer;
        renderer->font_buffer_size = next_size;
    }
    unsigned char white[4] = {255, 255, 255, 255};
    int quads = stb_easy_font_print(0, 0, wrapped, white,
                                   renderer->font_buffer, (int)output_size);
    free(wrapped);
    EasyFontVertex *font_vertices = renderer->font_buffer;
    static const unsigned order[6] = {0, 1, 2, 0, 2, 3};
    for (int quad = 0; quad < quads; ++quad) {
        for (unsigned triangle_vertex = 0; triangle_vertex < 6; ++triangle_vertex) {
            if (*hud_count >= MAX_HUD_VERTICES) fail("HUD vertex limit exceeded");
            const EasyFontVertex *source = &font_vertices[(size_t)quad * 4 + order[triangle_vertex]];
            GpuVertex *target = &renderer->vertices[renderer->hud_offset + *hud_count];
            Position position = {(float)queued->horizontal + source->horizontal * scale,
                                 (float)queued->vertical + source->vertical * scale, 0};
            fill_vertex(target, position, position, 0, 0, 0, 2);
            for (unsigned channel = 0; channel < 4; ++channel)
                target->color[channel] = (float)source->color[channel] / 255.0f;
            ++*hud_count;
        }
    }
    return height;
}

static void build_hud(Renderer *renderer, size_t *hud_count) {
    *hud_count = 0;
    int next_vertical = 0;
    for (size_t index = 0; index < renderer->text_count; ++index) {
        QueuedText text = renderer->texts[index];
        if (text.vertical < next_vertical) text.vertical = next_vertical;
        next_vertical = text.vertical + (int)append_text(renderer, &text, hud_count) + 8;
    }
}

static VkDeviceAddress buffer_address(Renderer *renderer, const GpuBuffer *buffer) {
    VkBufferDeviceAddressInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer->handle};
    return vkGetBufferDeviceAddress(renderer->device, &info);
}

static VkAccelerationStructureBuildGeometryInfoKHR ray_build_info(RayLighting *ray, bool top) {
    return (VkAccelerationStructureBuildGeometryInfoKHR){
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = top ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = ray->built ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .srcAccelerationStructure = ray->built ? (top ? ray->top : ray->bottom) : VK_NULL_HANDLE,
        .dstAccelerationStructure = top ? ray->top : ray->bottom,
        .geometryCount = 1, .pGeometries = top ? &ray->instance_geometry : &ray->geometry,
        .scratchData.deviceAddress = ray->scratch_address};
}

static void create_ray_storage(Renderer *renderer, VkAccelerationStructureTypeKHR type, VkDeviceSize size,
                               GpuBuffer *buffer, VkAccelerationStructureKHR *structure) {
    *buffer = create_buffer(renderer, size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
    VkAccelerationStructureCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = buffer->handle, .size = size, .type = type};
    require_vk(renderer->lighting->create(renderer->device, &info, NULL, structure), "ray structure creation");
}

static void create_ray_lighting(Renderer *renderer, const Scene *scene) {
    if (!renderer->ray_lighting) return;
    if (renderer->scene_vertex_count > UINT32_MAX || renderer->scene_vertex_count % 3)
        fail("Ray lighting requires a bounded triangle-list mesh");
    RayLighting *ray = renderer->lighting = allocate(1, sizeof(*ray));
    ray->enabled = true;
    ray->triangles = (uint32_t)(renderer->scene_vertex_count / 3);
    ray->create = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(renderer->device, "vkCreateAccelerationStructureKHR");
    ray->destroy = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(renderer->device, "vkDestroyAccelerationStructureKHR");
    ray->sizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(renderer->device, "vkGetAccelerationStructureBuildSizesKHR");
    ray->address = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(renderer->device, "vkGetAccelerationStructureDeviceAddressKHR");
    ray->build = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(renderer->device, "vkCmdBuildAccelerationStructuresKHR");
    if (!ray->create || !ray->destroy || !ray->sizes || !ray->address || !ray->build)
        fail("Ray lighting entry points are unavailable");
    VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &acceleration};
    vkGetPhysicalDeviceProperties2(renderer->physical_device, &properties);
    if (ray->triangles > acceleration.maxPrimitiveCount ||
        scene->asset_count + 2 > properties.properties.limits.maxPerStageDescriptorSamplers ||
        scene->asset_count + 2 > properties.properties.limits.maxPerStageDescriptorSampledImages ||
        scene->asset_count + 5 > properties.properties.limits.maxPerStageResources)
        fail("Map exceeds ray lighting device limits");
    VkDeviceSize vertex_bytes = renderer->scene_vertex_count * sizeof(GpuVertex);
    if (!vertex_bytes) vertex_bytes = sizeof(GpuVertex);
    VkDeviceSize material_bytes = (VkDeviceSize)(ray->triangles ? ray->triangles : 1) * sizeof(uint32_t);
    ray->vertices = create_buffer(renderer, vertex_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
    ray->materials = create_buffer(renderer, material_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
    ray->uniform = create_buffer(renderer, sizeof(RayUniform) + material_bytes,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    ray->instances = create_buffer(renderer, sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    ray->geometry = (VkAccelerationStructureGeometryKHR){
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry.triangles = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT, .vertexData.deviceAddress = buffer_address(renderer, &ray->vertices),
            .vertexStride = sizeof(GpuVertex), .maxVertex = renderer->scene_vertex_count ? (uint32_t)renderer->scene_vertex_count - 1 : 0,
            .indexType = VK_INDEX_TYPE_NONE_KHR}};
    ray->instance_geometry = (VkAccelerationStructureGeometryKHR){
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry.instances = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .data.deviceAddress = buffer_address(renderer, &ray->instances)}};
    VkAccelerationStructureBuildGeometryInfoKHR bottom = ray_build_info(ray, false), top = ray_build_info(ray, true);
    VkAccelerationStructureBuildSizesInfoKHR bottom_sizes = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    VkAccelerationStructureBuildSizesInfoKHR top_sizes = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    uint32_t one = 1;
    ray->sizes(renderer->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bottom, &ray->triangles, &bottom_sizes);
    ray->sizes(renderer->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &top, &one, &top_sizes);
    create_ray_storage(renderer, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, bottom_sizes.accelerationStructureSize,
                       &ray->bottom_storage, &ray->bottom);
    create_ray_storage(renderer, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, top_sizes.accelerationStructureSize,
                       &ray->top_storage, &ray->top);
    VkDeviceSize scratch_size = bottom_sizes.buildScratchSize;
    VkDeviceSize candidates[] = {bottom_sizes.updateScratchSize, top_sizes.buildScratchSize, top_sizes.updateScratchSize};
    for (unsigned index = 0; index < 3; ++index) if (candidates[index] > scratch_size) scratch_size = candidates[index];
    VkDeviceSize alignment = acceleration.minAccelerationStructureScratchOffsetAlignment;
    ray->scratch = create_buffer(renderer, scratch_size + alignment,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
    ray->scratch_address = (buffer_address(renderer, &ray->scratch) + alignment - 1) & ~(alignment - 1);
    VkAccelerationStructureDeviceAddressInfoKHR address = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, .accelerationStructure = ray->bottom};
    VkAccelerationStructureInstanceKHR instance = {.transform.matrix = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}},
        .mask = 255, .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
        .accelerationStructureReference = ray->address(renderer->device, &address)};
    memcpy(ray->instances.mapped, &instance, sizeof(instance));
    ray->light_positions = allocate(scene->sector_count, sizeof(*ray->light_positions));
    ray->light_areas = allocate(scene->sector_count, sizeof(*ray->light_areas));
    float *largest = allocate(scene->sector_count, sizeof(*largest));
    for (size_t index = 0; index < scene->batch_count; ++index) {
        const Batch *batch = &scene->batches[index];
        if (batch->plane != SURFACE_FLOOR || batch->owner >= scene->sector_count) continue;
        for (size_t vertex = 0; vertex + 2 < batch->count; vertex += 3) {
            const SceneVertex *first = &batch->vertices[vertex], *second = first + 1, *third = first + 2;
            float area = fabsf((second->x - first->x) * (third->z - first->z) -
                               (third->x - first->x) * (second->z - first->z)) * 0.5f;
            ray->light_areas[batch->owner] += area;
            if (area > largest[batch->owner]) {
                largest[batch->owner] = area;
                ray->light_positions[batch->owner] = (Position){(first->x + second->x + third->x) / 3,
                    0, (first->z + second->z + third->z) / 3};
            }
        }
    }
    free(largest);
    for (size_t index = 0; index < scene->batch_count; ++index) {
        const Batch *batch = &scene->batches[index];
        if (batch->plane == SURFACE_CEILING && batch->sky && batch->owner < scene->sector_count)
            ray->light_areas[batch->owner] = 0;
    }
    uint32_t textures = (uint32_t)scene->asset_count;
    VkDescriptorSetLayoutBinding bindings[] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = textures, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT}};
    VkDescriptorSetLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3, .pBindings = bindings};
    require_vk(vkCreateDescriptorSetLayout(renderer->device, &layout, NULL, &ray->layout), "ray descriptor layout");
    VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textures}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 3, .pPoolSizes = pool_sizes};
    require_vk(vkCreateDescriptorPool(renderer->device, &pool, NULL, &ray->pool), "ray descriptor pool");
    VkDescriptorSetAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = ray->pool, .descriptorSetCount = 1, .pSetLayouts = &ray->layout};
    require_vk(vkAllocateDescriptorSets(renderer->device, &allocation, &ray->descriptors), "ray descriptor set");
    VkDescriptorImageInfo *images = allocate(textures, sizeof(*images));
    for (uint32_t index = 0; index < textures; ++index)
        images[index] = (VkDescriptorImageInfo){renderer->sampler, renderer->textures[index].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSetAccelerationStructureKHR structure = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1, .pAccelerationStructures = &ray->top};
    VkDescriptorBufferInfo uniform = {ray->uniform.handle, 0, sizeof(RayUniform)};
    VkWriteDescriptorSet writes[] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = &structure, .dstSet = ray->descriptors,
         .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ray->descriptors, .dstBinding = 1,
         .descriptorCount = textures, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = images},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ray->descriptors, .dstBinding = 2,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &uniform}};
    vkUpdateDescriptorSets(renderer->device, 3, writes, 0, NULL);
    free(images);
    printf("Ray lighting: %u world triangles, scratch alignment %llu\n", ray->triangles, (unsigned long long)alignment);
}

static void ray_barrier(Renderer *renderer, VkPipelineStageFlags2 source, VkAccessFlags2 source_access,
                       VkPipelineStageFlags2 destination, VkAccessFlags2 destination_access) {
    VkMemoryBarrier2 barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = source, .srcAccessMask = source_access,
        .dstStageMask = destination, .dstAccessMask = destination_access};
    VkDependencyInfo dependency = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1, .pMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(renderer->command_buffer, &dependency);
}

static void update_ray_lighting(Renderer *renderer, const Scene *scene, const RenderCamera *camera) {
    RayLighting *ray = renderer->lighting;
    if (!ray) return;
    RayUniform uniform = {.vertices = buffer_address(renderer, &ray->vertices),
        .materials = buffer_address(renderer, &ray->materials),
        .info = {0, ray->enabled, sizeof(GpuVertex) / sizeof(float), (uint32_t)scene->asset_count},
        .sun_direction = {-0.4f, 0.85f, 0.3f, 0.8f}, .sun_color = {1, 0.97f, 0.9f, 0}};
    float distances[4] = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    for (size_t sector = 0; sector < scene->sector_count; ++sector) {
        const SceneSector *room = &scene->sectors[sector];
        float height = room->ceiling - room->floor;
        if (ray->light_areas[sector] < 4 || height < 0.5f || !room->light) continue;
        Position position = ray->light_positions[sector];
        position.y = room->ceiling - 0.15f;
        Position delta = vector_subtract(position, camera->position);
        float distance = vector_dot(delta, delta);
        unsigned slot = 0;
        while (slot < 4 && distance >= distances[slot]) ++slot;
        if (slot == 4) continue;
        for (unsigned index = 3; index > slot; --index) {
            distances[index] = distances[index - 1];
            memcpy(uniform.positions[index], uniform.positions[index - 1], sizeof(uniform.positions[index]));
            memcpy(uniform.colors[index], uniform.colors[index - 1], sizeof(uniform.colors[index]));
        }
        distances[slot] = distance;
        uniform.positions[slot][0] = position.x; uniform.positions[slot][1] = position.y; uniform.positions[slot][2] = position.z;
        uniform.positions[slot][3] = fminf(64, fmaxf(height * 2, sqrtf(ray->light_areas[sector]) * 2 + height));
        uniform.colors[slot][0] = 1; uniform.colors[slot][1] = 1; uniform.colors[slot][2] = 1;
        uniform.colors[slot][3] = 12.0f * fminf(1, (float)room->light / 255.0f);
        if (uniform.info[0] < 4) ++uniform.info[0];
    }
    memcpy(ray->uniform.mapped, &uniform, sizeof(uniform));
    if (!ray->enabled && ray->built) return;
    uint32_t *materials = (uint32_t *)((uint8_t *)ray->uniform.mapped + sizeof(RayUniform));
    if (!ray->triangles) materials[0] = UINT32_MAX;
    for (size_t batch_index = 0; batch_index < scene->batch_count; ++batch_index) {
        const Batch *batch = &scene->batches[batch_index];
        bool hidden = batch->sky != 0;
        if (batch->plane == SURFACE_FLOOR || batch->plane == SURFACE_CEILING)
            hidden |= scene->sectors[batch->owner].ceiling <= scene->sectors[batch->owner].floor;
        size_t first = renderer->batch_offsets[batch_index] / 3;
        for (size_t triangle = 0; triangle < batch->count / 3; ++triangle)
            materials[first + triangle] = hidden ? UINT32_MAX : batch->asset;
    }
    if (renderer->scene_vertex_count) {
        VkBufferCopy copy = {.size = renderer->scene_vertex_count * sizeof(GpuVertex)};
        vkCmdCopyBuffer(renderer->command_buffer, renderer->vertex_buffer.handle, ray->vertices.handle, 1, &copy);
    }
    VkBufferCopy material_copy = {.srcOffset = sizeof(RayUniform), .size = ray->materials.size};
    vkCmdCopyBuffer(renderer->command_buffer, ray->uniform.handle, ray->materials.handle, 1, &material_copy);
    ray_barrier(renderer, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT);
    if (!ray->built || ray->geometry_dirty) {
        VkAccelerationStructureBuildGeometryInfoKHR bottom = ray_build_info(ray, false), top = ray_build_info(ray, true);
        VkAccelerationStructureBuildRangeInfoKHR bottom_range = {.primitiveCount = ray->triangles}, top_range = {.primitiveCount = 1};
        const VkAccelerationStructureBuildRangeInfoKHR *range = &bottom_range;
        ray->build(renderer->command_buffer, 1, &bottom, &range);
        ray_barrier(renderer, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        range = &top_range;
        ray->build(renderer->command_buffer, 1, &top, &range);
        ray_barrier(renderer, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
        ray->built = true;
        ray->geometry_dirty = false;
    }
}

static void destroy_ray_lighting(Renderer *renderer) {
    RayLighting *ray = renderer->lighting;
    if (!ray) return;
    vkDestroyDescriptorPool(renderer->device, ray->pool, NULL);
    vkDestroyDescriptorSetLayout(renderer->device, ray->layout, NULL);
    ray->destroy(renderer->device, ray->top, NULL);
    ray->destroy(renderer->device, ray->bottom, NULL);
    GpuBuffer *buffers[] = {&ray->vertices, &ray->materials, &ray->uniform, &ray->instances,
        &ray->scratch, &ray->bottom_storage, &ray->top_storage};
    for (unsigned index = 0; index < 7; ++index) destroy_buffer(renderer, buffers[index]);
    free(ray->light_positions); free(ray->light_areas); free(ray);
    renderer->lighting = NULL;
}

static void create_frame_sync(Renderer *renderer) {
    VkCommandPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = renderer->graphics_family
    };
    require_vk(vkCreateCommandPool(renderer->device, &pool, NULL, &renderer->command_pool), "command pool creation");
    VkCommandBufferAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = renderer->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    require_vk(vkAllocateCommandBuffers(renderer->device, &allocation, &renderer->command_buffer),
               "frame command allocation");
    VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    require_vk(vkCreateSemaphore(renderer->device, &semaphore, NULL, &renderer->image_available),
               "acquire semaphore creation");
#ifdef DOOM_FRAME_GENERATION
    if (renderer->fg) {
        require_vk(vkCreateSemaphore(renderer->device, &semaphore, NULL, &renderer->fg_image_available),
                   "second acquire semaphore creation");
        renderer->fg_disable = create_buffer(renderer, 4,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    }
#endif
    VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    require_vk(vkCreateFence(renderer->device, &fence, NULL, &renderer->frame_fence), "frame fence creation");
}

Renderer *renderer_create(GLFWwindow *window, const Scene *scene, RendererOptions options) {
    bool validation = options.validation, dlss_enabled = options.dlss_enabled;
    bool frame_generation = options.frame_generation;
    unsigned upscale_mode = options.upscale_mode;
    if (upscale_mode > 3 || (upscale_mode && !dlss_enabled)) fail("Invalid Super Resolution configuration");
    if (frame_generation && !dlss_enabled) fail("Frame generation currently requires --dlss5");
#ifndef DOOM_DLSS5
    if (dlss_enabled) fail("DLSS 5 requires DOOM_DLSS5=ON, the public NGX SDK, and its NR runtime; see docs/VULKAN.md");
#endif
    if (!window || !scene || !scene->asset_count) fail("Renderer needs a window and a non-empty scene");
    Renderer *renderer = allocate(1, sizeof(*renderer));
    renderer->window = window;
    renderer->ray_lighting = options.ray_lighting;
    renderer->dlss_enabled = dlss_enabled;
    renderer->neural_passes = 1;
    renderer->upscale_mode = upscale_mode;
    renderer->frame_generation = frame_generation;
    renderer->no_vsync = options.no_vsync;
#ifndef DOOM_FRAME_GENERATION
    if (frame_generation) fail("Frame generation requires DOOM_FRAME_GENERATION=ON");
#endif
    atomic_init(&renderer->validation_errors, 0);
    create_instance(renderer, validation);
    select_physical_device(renderer);
    create_device(renderer);
#ifdef DOOM_DLSS5
    if (dlss_enabled) {
        char error[512];
        DoomDlss5InitInfo info = {.instance = renderer->instance, .physical_device = renderer->physical_device,
                                 .device = renderer->device, .application_data_path = L"."};
        if (!doom_dlss5_create(&info, &renderer->dlss, error, sizeof(error))) fail("DLSS 5 initialization: %s", error);
    }
#endif
#ifdef DOOM_FRAME_GENERATION
    if (frame_generation) {
        char error[512];
        if (!doom_frame_generation_create(&renderer->fg, error, sizeof(error)))
            fail("Frame generation initialization: %s", error);
        renderer->fg_frame_seconds = 1.0 / 60.0;
    }
#endif
    create_frame_sync(renderer);
    if (!create_swapchain(renderer)) fail("Cannot create a swapchain for the current window size");
    create_targets(renderer);
#ifdef DOOM_FRAME_GENERATION
    if (renderer->fg) {
        char error[512];
        renderer->presentation = presentation_create(renderer->present_queue, error, sizeof(error));
        if (!renderer->presentation) fail("Frame presentation initialization: %s", error);
    }
#endif
    create_sampler(renderer);
    upload_textures(renderer, scene);
    renderer->batch_offsets = allocate(scene->batch_count, sizeof(*renderer->batch_offsets));
    renderer->base_assets = allocate(scene->batch_count, sizeof(*renderer->base_assets));
    size_t vertex_count = 0;
    for (size_t index = 0; index < scene->batch_count; ++index) {
        renderer->batch_offsets[index] = vertex_count;
        renderer->base_assets[index] = scene->batches[index].asset;
        if (scene->batches[index].count > SIZE_MAX - vertex_count) fail("Scene vertex count overflow");
        vertex_count += scene->batches[index].count;
    }
    renderer->scene_vertex_count = vertex_count;
    renderer->sprite_offset = vertex_count;
    if (scene->sprite_count > (SIZE_MAX - vertex_count) / 6) fail("Sprite vertex count overflow");
    vertex_count += scene->sprite_count * 6;
    renderer->sky_offset = vertex_count;
    if (vertex_count > SIZE_MAX - 36 - MAX_HUD_VERTICES) fail("Renderer vertex count overflow");
    renderer->hud_offset = vertex_count + 36;
    renderer->vertex_capacity = renderer->hud_offset + MAX_HUD_VERTICES;
    if (renderer->vertex_capacity > UINT64_MAX / sizeof(GpuVertex)) fail("Renderer vertex buffer is too large");
    renderer->vertices = allocate(renderer->vertex_capacity, sizeof(*renderer->vertices));
    renderer->previous_scene_positions = allocate(renderer->scene_vertex_count, sizeof(*renderer->previous_scene_positions));
    renderer->previous_sprite_positions = allocate(scene->sprite_count * 6, sizeof(*renderer->previous_sprite_positions));
    char font_character[2] = {' ', '\0'};
    EasyFontVertex font_probe[256];
    for (int character = 32; character <= 126; ++character) {
        font_character[0] = (char)character;
        int quad_count = stb_easy_font_print(0, 0, font_character, NULL, font_probe, (int)sizeof(font_probe));
        if ((size_t)quad_count > renderer->font_quads_per_character)
            renderer->font_quads_per_character = (size_t)quad_count;
    }
    renderer->vertex_buffer = create_buffer(renderer, renderer->vertex_capacity * sizeof(GpuVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | (renderer->ray_lighting ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 0),
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    renderer->uniform_buffer = create_buffer(renderer, sizeof(FrameUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    create_ray_lighting(renderer, scene);
    create_pipelines(renderer);
    create_descriptors(renderer);
    return renderer;
}

void renderer_text(Renderer *renderer, const char *text, int horizontal, int vertical) {
    if (!renderer || !text || !*text) return;
    if (renderer->text_count == renderer->text_capacity) {
        size_t capacity = renderer->text_capacity ? renderer->text_capacity * 2 : 8;
        if (capacity < renderer->text_capacity || capacity > SIZE_MAX / sizeof(*renderer->texts))
            fail("HUD text queue overflow");
        QueuedText *texts = realloc(renderer->texts, capacity * sizeof(*texts));
        if (!texts) fail("Out of memory for HUD text queue");
        renderer->texts = texts;
        renderer->text_capacity = capacity;
    }
    size_t length = strlen(text);
    char *copy = allocate(length + 1, 1);
    memcpy(copy, text, length + 1);
    for (size_t index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)copy[index];
        if ((character < 32 || character > 126) && character != '\n') copy[index] = '?';
    }
    renderer->texts[renderer->text_count++] = (QueuedText){
        .text = copy, .horizontal = horizontal, .vertical = vertical};
}

static void release_queued_text(Renderer *renderer) {
    for (size_t index = 0; index < renderer->text_count; ++index) free(renderer->texts[index].text);
    renderer->text_count = 0;
}

static uint32_t sky_asset_index(const Scene *scene) {
    for (size_t index = 0; index < scene->batch_count; ++index)
        if (scene->batches[index].sky) return scene->batches[index].asset;
    return UINT32_MAX;
}

#ifdef DOOM_FRAME_GENERATION
static void flush_presentation(Renderer *renderer) {
    if (!renderer->presentation) return;
    VkResult result = presentation_flush(renderer->presentation);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
        require_vk(result, "presentation flush");
}
#endif

static bool recreate_swapchain_targets(Renderer *renderer) {
#ifdef DOOM_FRAME_GENERATION
    flush_presentation(renderer);
#endif
    require_vk(vkDeviceWaitIdle(renderer->device), "device wait before resize");
    if (!create_swapchain(renderer)) return false;
    destroy_targets(renderer);
    create_targets(renderer);
    renderer->history_valid = false;
    renderer->swapchain_dirty = false;
#ifdef DOOM_FRAME_GENERATION
    if (renderer->presentation) presentation_reset(renderer->presentation);
    renderer->fg_last_render_time = 0;
    renderer->fg_frame_seconds = 1.0 / 60.0;
#endif
    return true;
}

static bool framebuffer_changed(Renderer *renderer) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(renderer->window, &width, &height);
    return width > 0 && height > 0 && ((uint32_t)width != renderer->extent.width ||
                                      (uint32_t)height != renderer->extent.height);
}

static void blit_output(Renderer *renderer, VkCommandBuffer command, VkImage source, VkImage destination) {
    VkImageBlit blit = {
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .srcOffsets = {{0, 0, 0}, {(int32_t)renderer->extent.width, (int32_t)renderer->extent.height, 1}},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .dstOffsets = {{0, 0, 0}, {(int32_t)renderer->extent.width, (int32_t)renderer->extent.height, 1}}
    };
    vkCmdBlitImage(command, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
}

static void draw_world(Renderer *renderer, const Scene *scene, uint32_t sky_asset) {
    if (renderer->lighting)
        vkCmdBindDescriptorSets(renderer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout, 1, 1, &renderer->lighting->descriptors, 0, NULL);
    VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(renderer->command_buffer, 0, 1, &renderer->vertex_buffer.handle, &vertex_offset);
    if (sky_asset != UINT32_MAX) {
        vkCmdBindPipeline(renderer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->sky_pipeline);
        vkCmdBindDescriptorSets(renderer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout, 0, 1, &renderer->descriptor_sets[sky_asset], 0, NULL);
        vkCmdDraw(renderer->command_buffer, 36, 1, (uint32_t)renderer->sky_offset, 0);
    }
    vkCmdBindPipeline(renderer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->scene_pipeline);
    for (size_t index = 0; index < scene->batch_count; ++index) {
        const Batch *batch = &scene->batches[index];
        if ((batch->plane == SURFACE_FLOOR || batch->plane == SURFACE_CEILING) &&
            scene->sectors[batch->owner].ceiling <= scene->sectors[batch->owner].floor) continue;
        if (batch->asset >= renderer->texture_count) fail("Scene batch has an invalid texture");
        vkCmdBindDescriptorSets(renderer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout, 0, 1, &renderer->descriptor_sets[batch->asset], 0, NULL);
        vkCmdDraw(renderer->command_buffer, (uint32_t)batch->count, 1,
                  (uint32_t)renderer->batch_offsets[index], 0);
    }
    for (size_t index = 0; index < scene->sprite_count; ++index) {
        const Sprite *sprite = &scene->sprites[index];
        if (sprite->hidden) continue;
        vkCmdBindDescriptorSets(renderer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            renderer->pipeline_layout, 0, 1, &renderer->descriptor_sets[sprite->asset], 0, NULL);
        vkCmdDraw(renderer->command_buffer, 6, 1, (uint32_t)(renderer->sprite_offset + index * 6), 0);
    }
}

static void draw_hud(Renderer *renderer, size_t hud_count) {
    if (!hud_count) return;
    VkRenderingAttachmentInfo attachments[1] = {
        {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = renderer->display_target.view,
         .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE}
    };
    VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, renderer->extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = attachments
    };
    vkCmdBeginRendering(renderer->command_buffer, &rendering);
    VkViewport viewport = {0, 0, (float)renderer->extent.width, (float)renderer->extent.height, 0, 1};
    VkRect2D scissor = {{0, 0}, renderer->extent};
    vkCmdSetViewport(renderer->command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(renderer->command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(renderer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->hud_pipeline);
    vkCmdBindDescriptorSets(renderer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        renderer->pipeline_layout, 0, 1, &renderer->descriptor_sets[0], 0, NULL);
    VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(renderer->command_buffer, 0, 1, &renderer->vertex_buffer.handle, &vertex_offset);
    vkCmdDraw(renderer->command_buffer, (uint32_t)hud_count, 1, (uint32_t)renderer->hud_offset, 0);
    vkCmdEndRendering(renderer->command_buffer);
}

static float halton(unsigned index, unsigned base) {
    float fraction = 1, value = 0;
    while (index) {
        fraction /= (float)base;
        value += fraction * (float)(index % base);
        index /= base;
    }
    return value;
}

static void write_frame_uniform(Renderer *renderer, const RenderCamera *camera) {
    FrameUniform uniform = {0};
    float aspect = (float)renderer->extent.width / (float)renderer->extent.height;
    make_view_projection(camera, aspect, uniform.current_matrix);
    if (renderer->history_valid) make_view_projection(&renderer->previous_camera, aspect, uniform.previous_matrix);
    else memcpy(uniform.previous_matrix, uniform.current_matrix, sizeof(uniform.current_matrix));
    uniform.camera_position[0] = camera->position.x;
    uniform.camera_position[1] = camera->position.y;
    uniform.camera_position[2] = camera->position.z;
    uniform.extent[0] = (float)renderer->work_extent.width;
    uniform.extent[1] = (float)renderer->work_extent.height;
    uniform.extent[2] = (float)renderer->extent.width;
    uniform.extent[3] = (float)renderer->extent.height;
    if (!renderer->history_valid) renderer->jitter_index = 0;
    renderer->jitter[0] = renderer->upscale_mode ? halton(renderer->jitter_index + 1, 2) - 0.5f : 0;
    renderer->jitter[1] = renderer->upscale_mode ? halton(renderer->jitter_index + 1, 3) - 0.5f : 0;
    for (unsigned axis = 0; axis < 2; ++axis) {
        float previous = renderer->history_valid ? renderer->previous_jitter[axis] : renderer->jitter[axis];
        uniform.jitter[axis] = renderer->jitter[axis] * 2 / uniform.extent[axis];
        uniform.jitter[axis + 2] = previous * 2 / uniform.extent[axis];
    }
    memcpy(renderer->uniform_buffer.mapped, &uniform, sizeof(uniform));
}

#ifdef DOOM_DLSS5
static DoomDlss5Image dlss_image(const GpuImage *image, VkExtent2D extent) {
    return (DoomDlss5Image){.image = image->handle, .view = image->view, .format = image->format,
                           .width = extent.width, .height = extent.height,
                           .layout = VK_IMAGE_LAYOUT_GENERAL};
}

static void evaluate_dlss(Renderer *renderer) {
    image_barrier(renderer->command_buffer, renderer->output_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    char error[512];
    if (!doom_dlss5_resize(renderer->dlss, renderer->command_buffer, renderer->work_extent,
                           renderer->neural_passes, error, sizeof(error)))
        fail("DLSS 5 feature creation: %s", error);
    for (unsigned pass = 0; pass < renderer->neural_passes; ++pass) {
        const GpuImage *source = pass % 2 ? &renderer->output_target : &renderer->color_target;
        const GpuImage *destination = pass % 2 ? &renderer->color_target : &renderer->output_target;
        if (pass) {
            image_barrier(renderer->command_buffer, source->handle, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
            image_barrier(renderer->command_buffer, destination->handle, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        }
        DoomDlss5Frame frame = {.color = dlss_image(source, renderer->work_extent),
                            .motion = dlss_image(&renderer->motion_target, renderer->work_extent),
                            .depth = dlss_image(&renderer->depth_target, renderer->work_extent),
                            .output = dlss_image(destination, renderer->work_extent), .style = renderer->neural_style, .pass = pass};
        if (!doom_dlss5_evaluate(renderer->dlss, renderer->command_buffer, &frame,
                                !renderer->history_valid, error, sizeof(error)))
            fail("DLSS 5 evaluation: %s", error);
    }
    if (!(renderer->neural_passes % 2)) {
        GpuImage temporary = renderer->output_target;
        renderer->output_target = renderer->color_target;
        renderer->color_target = temporary;
    }
    if (!renderer->has_frame) puts("DLSS 5 neural rendering evaluation recorded");
}
#endif

static void resolve_output(Renderer *renderer) {
    const GpuImage *source = renderer->dlss_enabled ? &renderer->output_target : &renderer->color_target;
    VkImageLayout old_layout = renderer->targets_initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 prior_stage = renderer->targets_initialized ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 prior_access = renderer->targets_initialized ? VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT : 0;
#ifdef DOOM_DLSS5
    if (renderer->upscale_mode) {
        image_barrier(renderer->command_buffer, renderer->display_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
            old_layout, VK_IMAGE_LAYOUT_GENERAL, prior_stage, prior_access,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        DoomDlss5Frame frame = {.color = dlss_image(source, renderer->work_extent),
                                .motion = dlss_image(&renderer->motion_target, renderer->work_extent),
                                .depth = dlss_image(&renderer->depth_target, renderer->work_extent),
                                .output = dlss_image(&renderer->display_target, renderer->extent)};
        char error[512];
        if (!doom_dlss5_upscale(renderer->dlss, renderer->command_buffer, &frame,
                                (DoomDlss5SRMode)renderer->upscale_mode,
                                renderer->jitter[0], renderer->jitter[1], !renderer->history_valid,
                                error, sizeof(error)))
            fail("Super Resolution evaluation: %s", error);
        if (!renderer->has_frame) puts("DLSS Super Resolution evaluation recorded");
        return;
    }
#endif
    image_barrier(renderer->command_buffer, source->handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    image_barrier(renderer->command_buffer, renderer->display_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        old_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, prior_stage, prior_access,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageCopy copy = {
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .extent = {renderer->extent.width, renderer->extent.height, 1}
    };
    vkCmdCopyImage(renderer->command_buffer, source->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        renderer->display_target.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    image_barrier(renderer->command_buffer, source->handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    image_barrier(renderer->command_buffer, renderer->display_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
}

#ifdef DOOM_FRAME_GENERATION
static void multiply_matrix(const float first[16], const float second[16], float output[16]) {
    memset(output, 0, sizeof(float) * 16);
    for (unsigned column = 0; column < 4; ++column)
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned inner = 0; inner < 4; ++inner)
                output[column * 4 + row] += first[inner * 4 + row] * second[column * 4 + inner];
}

static void inverse_matrix(const float input[16], float output[16]) {
    float rows[4][8] = {0};
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 4; ++column) rows[row][column] = input[column * 4 + row];
        rows[row][row + 4] = 1;
    }
    for (unsigned column = 0; column < 4; ++column) {
        unsigned pivot = column;
        for (unsigned row = column + 1; row < 4; ++row)
            if (fabsf(rows[row][column]) > fabsf(rows[pivot][column])) pivot = row;
        if (fabsf(rows[pivot][column]) < 1e-20f) fail("Frame generation camera matrix is singular");
        for (unsigned index = 0; index < 8; ++index) {
            float value = rows[column][index];
            rows[column][index] = rows[pivot][index];
            rows[pivot][index] = value;
        }
        float divisor = rows[column][column];
        for (unsigned index = 0; index < 8; ++index) rows[column][index] /= divisor;
        for (unsigned row = 0; row < 4; ++row) {
            if (row == column) continue;
            float factor = rows[row][column];
            for (unsigned index = 0; index < 8; ++index) rows[row][index] -= factor * rows[column][index];
        }
    }
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column) output[column * 4 + row] = rows[row][column + 4];
}

static DoomFrameGenerationImage frame_generation_image(const GpuImage *image, VkExtent2D extent, VkImageLayout layout) {
    return (DoomFrameGenerationImage){.image = image->handle, .view = image->view,
        .format = image->format, .extent = extent, .layout = layout};
}

static void copy_hudless(Renderer *renderer) {
    if (!renderer->frame_generation) return;
    image_barrier(renderer->command_buffer, renderer->display_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    image_barrier(renderer->command_buffer, renderer->hudless_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        renderer->targets_initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageCopy copy = {.srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .extent = {renderer->extent.width, renderer->extent.height, 1}};
    vkCmdCopyImage(renderer->command_buffer, renderer->display_target.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        renderer->hudless_target.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    image_barrier(renderer->command_buffer, renderer->display_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    image_barrier(renderer->command_buffer, renderer->hudless_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);
}

static void fg_buffer_barrier(Renderer *renderer, VkPipelineStageFlags2 source, VkAccessFlags2 source_access,
                              VkPipelineStageFlags2 destination, VkAccessFlags2 destination_access) {
    VkBufferMemoryBarrier2 barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = source, .srcAccessMask = source_access,
        .dstStageMask = destination, .dstAccessMask = destination_access,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = renderer->fg_disable.handle, .offset = 0, .size = 4};
    VkDependencyInfo dependency = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(renderer->command_buffer, &dependency);
}

static void evaluate_frame_generation(Renderer *renderer, const RenderCamera *camera) {
    GpuImage *inputs[] = {&renderer->display_target, &renderer->depth_target,
                         &renderer->motion_target, &renderer->hudless_target};
    for (unsigned index = 0; index < 4; ++index)
        image_barrier(renderer->command_buffer, inputs[index]->handle,
            index == 1 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);
    image_barrier(renderer->command_buffer, renderer->interpolated_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        renderer->targets_initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    fg_buffer_barrier(renderer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdFillBuffer(renderer->command_buffer, renderer->fg_disable.handle, 0, 4, UINT32_MAX);
    fg_buffer_barrier(renderer, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    DoomFrameGenerationFrame frame = {.command_buffer = renderer->command_buffer,
        .backbuffer = frame_generation_image(&renderer->display_target, renderer->extent, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        .interpolated = frame_generation_image(&renderer->interpolated_target, renderer->extent, VK_IMAGE_LAYOUT_GENERAL),
        .disable_interpolation = renderer->fg_disable.handle,
        .depth = frame_generation_image(&renderer->depth_target, renderer->work_extent, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        .motion = frame_generation_image(&renderer->motion_target, renderer->work_extent, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        .hudless = frame_generation_image(&renderer->hudless_target, renderer->extent, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        .cameraPosition = {camera->position.x, camera->position.y, camera->position.z},
        .fovy = camera->fovy * pi_value / 180.0f,
        .aspect = (float)renderer->extent.width / (float)renderer->extent.height,
        .nearPlane = 0.02f, .farPlane = 10000.0f,
        .motionScale = {-(float)renderer->work_extent.width, -(float)renderer->work_extent.height},
        .pixelJitter = {renderer->jitter[0], renderer->jitter[1]},
        .reset = !renderer->history_valid};
    Position forward = vector_normalize(vector_subtract(camera->target, camera->position));
    Position right = vector_normalize(vector_cross(forward, (Position){0, 1, 0}));
    Position up = vector_cross(right, forward);
    memcpy(frame.cameraForward, &forward, sizeof(frame.cameraForward));
    memcpy(frame.cameraRight, &right, sizeof(frame.cameraRight));
    memcpy(frame.cameraUp, &up, sizeof(frame.cameraUp));
    float focal = 1.0f / tanf(frame.fovy * 0.5f);
    frame.viewToClip[0] = focal / frame.aspect;
    frame.viewToClip[5] = -focal;
    frame.viewToClip[10] = frame.farPlane / (frame.nearPlane - frame.farPlane);
    frame.viewToClip[11] = -1;
    frame.viewToClip[14] = frame.farPlane * frame.nearPlane / (frame.nearPlane - frame.farPlane);
    inverse_matrix(frame.viewToClip, frame.clipToView);
    float current[16], previous[16], inverse[16];
    make_view_projection(camera, frame.aspect, current);
    make_view_projection(renderer->history_valid ? &renderer->previous_camera : camera, frame.aspect, previous);
    inverse_matrix(current, inverse);
    multiply_matrix(previous, inverse, frame.clipToPrevClip);
    inverse_matrix(frame.clipToPrevClip, frame.prevClipToClip);
    char error[512];
    if (!doom_frame_generation_evaluate(renderer->fg, &frame, error, sizeof(error)))
        fail("Frame generation inputs: %s", error);
    for (unsigned index = 0; index < 4; ++index)
        image_barrier(renderer->command_buffer, inputs[index]->handle,
            index == 1 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    fg_buffer_barrier(renderer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    if (!renderer->has_frame) puts("DLSS Frame Generation evaluation recorded");
}

static bool present_generated_frame(Renderer *renderer, bool had_history) {
    require_vk(vkEndCommandBuffer(renderer->command_buffer), "FG render command end");
    VkCommandBufferSubmitInfo command = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = renderer->command_buffer};
    VkSubmitInfo2 submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1, .pCommandBufferInfos = &command};
    require_vk(vkResetFences(renderer->device, 1, &renderer->frame_fence), "FG render fence reset");
    require_vk(vkQueueSubmit2(renderer->graphics_queue, 1, &submit, renderer->frame_fence), "FG render submit");
    require_vk(vkWaitForFences(renderer->device, 1, &renderer->frame_fence, VK_TRUE, UINT64_MAX), "FG result wait");
    uint8_t disabled = *(const uint8_t *)renderer->fg_disable.mapped;
    if (disabled > 1) fail("Frame generation did not return a valid interpolation flag");
    bool generated = had_history && !disabled;
    renderer->last_frame_generated = generated;
    double now = glfwGetTime();
    double elapsed = now - renderer->fg_last_render_time;
    if (renderer->fg_last_render_time > 0 && elapsed > 0 && elapsed < 0.25)
        renderer->fg_frame_seconds = renderer->fg_frame_seconds * 0.9 + elapsed * 0.1;
    else renderer->fg_frame_seconds = 1.0 / 60.0;
    renderer->fg_last_render_time = now;
    PresentationFrame frame = {.swapchain = renderer->swapchain,
        .count = generated ? 2u : 1u, .frame_seconds = renderer->fg_frame_seconds};
    VkSemaphore acquire_semaphores[] = {renderer->image_available, renderer->fg_image_available};
    VkSemaphoreSubmitInfo waits[2] = {0}, signals[2] = {0};
    for (uint32_t index = 0; index < frame.count; ++index) {
        VkResult acquired = presentation_acquire(renderer->presentation, renderer->device, renderer->swapchain,
            acquire_semaphores[index], &frame.image_indices[index]);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR || acquired == VK_TIMEOUT || acquired == VK_NOT_READY) {
            renderer->swapchain_dirty |= acquired == VK_ERROR_OUT_OF_DATE_KHR || index != 0;
            if (index) {
                VkSubmitInfo2 drain = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                    .waitSemaphoreInfoCount = index, .pWaitSemaphoreInfos = waits};
                require_vk(vkResetFences(renderer->device, 1, &renderer->frame_fence), "FG acquire drain reset");
                require_vk(vkQueueSubmit2(renderer->graphics_queue, 1, &drain, renderer->frame_fence), "FG acquire drain");
                require_vk(vkWaitForFences(renderer->device, 1, &renderer->frame_fence, VK_TRUE, UINT64_MAX), "FG acquire drain wait");
            }
            return false;
        }
        if (acquired == VK_SUBOPTIMAL_KHR) renderer->swapchain_dirty = true;
        else require_vk(acquired, "FG image acquisition");
        frame.wait_semaphores[index] = renderer->present_semaphores[frame.image_indices[index]];
        waits[index] = (VkSemaphoreSubmitInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = acquire_semaphores[index], .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
        signals[index] = (VkSemaphoreSubmitInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = frame.wait_semaphores[index], .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    }
    require_vk(vkResetCommandBuffer(renderer->command_buffer, 0), "FG copy command reset");
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    require_vk(vkBeginCommandBuffer(renderer->command_buffer, &begin), "FG copy command begin");
    for (uint32_t index = 0; index < frame.count; ++index) {
        GpuImage *source = generated && index == 0 ? &renderer->interpolated_target : &renderer->display_target;
        uint32_t image_index = frame.image_indices[index];
        image_barrier(renderer->command_buffer, source->handle, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        image_barrier(renderer->command_buffer, renderer->swapchain_images[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
            renderer->swapchain_initialized[image_index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            renderer->swapchain_initialized[image_index] ? VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT : 0,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        blit_output(renderer, renderer->command_buffer, source->handle, renderer->swapchain_images[image_index]);
        image_barrier(renderer->command_buffer, renderer->swapchain_images[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
        image_barrier(renderer->command_buffer, source->handle, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }
    require_vk(vkEndCommandBuffer(renderer->command_buffer), "FG copy command end");
    submit.waitSemaphoreInfoCount = frame.count;
    submit.pWaitSemaphoreInfos = waits;
    submit.signalSemaphoreInfoCount = frame.count;
    submit.pSignalSemaphoreInfos = signals;
    require_vk(vkResetFences(renderer->device, 1, &renderer->frame_fence), "FG copy fence reset");
    require_vk(vkQueueSubmit2(renderer->graphics_queue, 1, &submit, renderer->frame_fence), "FG copy submit");
    for (uint32_t index = 0; index < frame.count; ++index)
        renderer->swapchain_initialized[frame.image_indices[index]] = true;
    VkResult presented = presentation_submit(renderer->presentation, &frame);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) renderer->swapchain_dirty = true;
    else require_vk(presented, "paced presentation submission");
    return presented != VK_ERROR_OUT_OF_DATE_KHR;
}
#endif

void renderer_enable_profile(Renderer *renderer) {
    if (renderer->profile_pool) return;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &count, NULL);
    VkQueueFamilyProperties *queues = allocate(count, sizeof(*queues));
    vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &count, queues);
    uint32_t bits = queues[renderer->graphics_family].timestampValidBits;
    free(queues);
    if (!bits) fail("GPU timestamps are unavailable on this graphics queue");
    renderer->profile_mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(renderer->physical_device, &properties);
    renderer->profile_period = properties.limits.timestampPeriod;
    VkQueryPoolCreateInfo info = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                  .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 5};
    require_vk(vkCreateQueryPool(renderer->device, &info, NULL, &renderer->profile_pool), "profile query pool");
    renderer->profile_last_time = glfwGetTime();
}

static void profile_read(Renderer *renderer) {
    if (!renderer->profile_pending) return;
    uint64_t times[5];
    require_vk(vkGetQueryPoolResults(renderer->device, renderer->profile_pool, 0, 5,
        sizeof(times), times, sizeof(times[0]), VK_QUERY_RESULT_64_BIT), "profile timestamp read");
    double now = glfwGetTime();
    if (renderer->profile_frames++ >= 16) {
        renderer->profile_wall_ms += (now - renderer->profile_last_time) * 1000;
        for (unsigned stage = 0; stage < 4; ++stage) {
            uint64_t ticks = (times[stage + 1] - times[stage]) & renderer->profile_mask;
            renderer->profile_stage_ms[stage] += (double)ticks * renderer->profile_period * 0.000001;
        }
    }
    renderer->profile_last_time = now;
    renderer->profile_pending = false;
}

static void profile_stamp(Renderer *renderer, uint32_t index) {
    if (renderer->profile_pool)
        vkCmdWriteTimestamp2(renderer->command_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                             renderer->profile_pool, index);
}

bool renderer_set_ray_lighting(Renderer *renderer, bool enabled) {
    if (!renderer->lighting) return false;
    if (renderer->lighting->enabled != enabled) renderer->history_valid = false;
    renderer->lighting->enabled = enabled;
    return true;
}

bool renderer_set_neural(Renderer *renderer, bool enabled) {
#ifdef DOOM_DLSS5
    if (!renderer->dlss) return false;
    if (renderer->dlss_enabled != enabled) renderer->history_valid = false;
    renderer->dlss_enabled = enabled;
    return true;
#else
    (void)renderer; (void)enabled;
    return false;
#endif
}

bool renderer_set_upscale_mode(Renderer *renderer, unsigned mode) {
#ifdef DOOM_DLSS5
    if (!renderer->dlss || mode > 3) return false;
    if (renderer->upscale_mode != mode) {
        renderer->upscale_mode = mode;
        renderer->swapchain_dirty = true;
        renderer->history_valid = false;
    }
    return true;
#else
    (void)renderer; (void)mode;
    return false;
#endif
}

bool renderer_set_frame_generation(Renderer *renderer, bool enabled) {
#ifdef DOOM_FRAME_GENERATION
    if (!renderer->fg) return false;
    if (renderer->frame_generation != enabled) {
        flush_presentation(renderer);
        require_vk(vkDeviceWaitIdle(renderer->device), "device wait before FG change");
        presentation_reset(renderer->presentation);
        renderer->frame_generation = enabled;
        renderer->swapchain_dirty = true;
        renderer->history_valid = false;
        renderer->last_frame_generated = false;
        renderer->fg_last_render_time = 0;
    }
    return true;
#else
    (void)renderer; (void)enabled;
    return false;
#endif
}

bool renderer_set_neural_style(Renderer *renderer, unsigned style) {
#ifdef DOOM_DLSS5
    if (!renderer->dlss || style > 2) return false;
    if (renderer->neural_style != style) renderer->history_valid = false;
    renderer->neural_style = style;
    return true;
#else
    (void)renderer; (void)style;
    return false;
#endif
}

bool renderer_set_neural_passes(Renderer *renderer, unsigned passes) {
#ifdef DOOM_DLSS5
    if (!renderer->dlss || !passes || passes > 4) return false;
    if (renderer->neural_passes != passes) renderer->history_valid = false;
    renderer->neural_passes = passes;
    return true;
#else
    (void)renderer; (void)passes;
    return false;
#endif
}


static void finish_frame_history(Renderer *renderer, const RenderCamera *camera, bool valid) {
    renderer->profile_pending = renderer->profile_pool != VK_NULL_HANDLE;
    renderer->history_valid = valid;
    renderer->previous_camera = *camera;
    memcpy(renderer->previous_jitter, renderer->jitter, sizeof(renderer->jitter));
    renderer->jitter_index = (renderer->jitter_index + 1) % 32;
    renderer->has_frame = true;
    release_queued_text(renderer);
}

static void discard_frame(Renderer *renderer) {
    renderer->history_valid = false;
#ifdef DOOM_FRAME_GENERATION
    renderer->last_frame_generated = false;
    renderer->fg_last_render_time = 0;
#endif
    release_queued_text(renderer);
}

void renderer_draw(Renderer *renderer, Scene *scene, const Specials *specials,
                   const RenderCamera *camera, bool reset_history) {
    if (!renderer || !scene || !camera) return;
    if (reset_history) renderer->history_valid = false;
    int framebuffer_width = 0, framebuffer_height = 0;
    glfwGetFramebufferSize(renderer->window, &framebuffer_width, &framebuffer_height);
    if (framebuffer_width <= 0 || framebuffer_height <= 0 || glfwGetWindowAttrib(renderer->window, GLFW_ICONIFIED)) {
        discard_frame(renderer);
        return;
    }
#ifdef DOOM_FRAME_GENERATION
    if (renderer->presentation) {
        VkResult status = presentation_stats(renderer->presentation).result;
        if (status == VK_ERROR_OUT_OF_DATE_KHR || status == VK_SUBOPTIMAL_KHR) renderer->swapchain_dirty = true;
        else require_vk(status, "presentation worker");
    }
#endif
    if ((renderer->swapchain_dirty || framebuffer_changed(renderer)) && !recreate_swapchain_targets(renderer)) {
        discard_frame(renderer);
        return;
    }
    require_vk(vkWaitForFences(renderer->device, 1, &renderer->frame_fence, VK_TRUE, UINT64_MAX),
               "frame fence wait");
    profile_read(renderer);

    uint32_t image_index = 0;
    bool acquire_suboptimal = false;
    bool image_acquired = renderer->frame_generation;
    unsigned out_of_date_count = 0;
    while (!image_acquired) {
        VkResult acquired = vkAcquireNextImageKHR(renderer->device, renderer->swapchain, 250000000,
                                                  renderer->image_available, VK_NULL_HANDLE, &image_index);
        if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY) {
            discard_frame(renderer);
            return;
        }
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            renderer->history_valid = false;
            if (glfwWindowShouldClose(renderer->window)) { discard_frame(renderer); return; }
            int current_width = 0, current_height = 0;
            glfwGetFramebufferSize(renderer->window, &current_width, &current_height);
            if (current_width <= 0 || current_height <= 0) { discard_frame(renderer); return; }
            if (!recreate_swapchain_targets(renderer)) { discard_frame(renderer); return; }
            if (++out_of_date_count % 4 == 0) glfwWaitEventsTimeout(0.02);
            continue;
        }
        if (acquired == VK_SUBOPTIMAL_KHR) acquire_suboptimal = true;
        else require_vk(acquired, "swapchain image acquisition");
        image_acquired = true;
    }
    if (image_index >= renderer->swapchain_image_count) fail("Vulkan acquired an invalid swapchain image");
    require_vk(vkResetCommandBuffer(renderer->command_buffer, 0), "frame command reset");

    size_t hud_count = 0;
    uint32_t sky_asset = sky_asset_index(scene);
    build_scene_vertices(renderer, scene, specials);
    build_sprites(renderer, scene, camera);
    build_sky(renderer, camera);
    build_hud(renderer, &hud_count);
    size_t active_vertices = renderer->hud_offset + hud_count;
    memcpy(renderer->vertex_buffer.mapped, renderer->vertices, active_vertices * sizeof(*renderer->vertices));
    write_frame_uniform(renderer, camera);

    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    require_vk(vkBeginCommandBuffer(renderer->command_buffer, &begin), "frame command begin");
    if (renderer->profile_pool) vkCmdResetQueryPool(renderer->command_buffer, renderer->profile_pool, 0, 5);
    profile_stamp(renderer, 0);
    update_ray_lighting(renderer, scene, camera);
    VkPipelineStageFlags2 prior_stage = renderer->targets_initialized ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT :
                                                                       VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 prior_access = renderer->targets_initialized ? VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT : 0;
    image_barrier(renderer->command_buffer, renderer->color_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        renderer->targets_initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, prior_stage, prior_access,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(renderer->command_buffer, renderer->motion_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        renderer->targets_initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, prior_stage, prior_access,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(renderer->command_buffer, renderer->depth_target.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
        renderer->targets_initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, prior_stage, prior_access,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    image_barrier(renderer->command_buffer, renderer->output_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        renderer->targets_initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL, prior_stage, prior_access,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    VkClearValue color_clear = {.color = {{0, 0, 0, 1}}};
    VkClearValue motion_clear = {.color = {{0, 0, 0, 0}}};
    VkClearValue depth_clear = {.depthStencil = {1, 0}};
    VkRenderingAttachmentInfo attachments[2] = {
        {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = renderer->color_target.view,
         .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .clearValue = color_clear},
        {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = renderer->motion_target.view,
         .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .clearValue = motion_clear}
    };
    VkRenderingAttachmentInfo depth = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = renderer->depth_target.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = depth_clear
    };
    VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, renderer->work_extent},
        .layerCount = 1,
        .colorAttachmentCount = 2,
        .pColorAttachments = attachments,
        .pDepthAttachment = &depth
    };
    vkCmdBeginRendering(renderer->command_buffer, &rendering);
    VkViewport viewport = {0, 0, (float)renderer->work_extent.width, (float)renderer->work_extent.height, 0, 1};
    VkRect2D scissor = {{0, 0}, renderer->work_extent};
    vkCmdSetViewport(renderer->command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(renderer->command_buffer, 0, 1, &scissor);
    draw_world(renderer, scene, sky_asset);
    vkCmdEndRendering(renderer->command_buffer);
    profile_stamp(renderer, 1);
    image_barrier(renderer->command_buffer, renderer->color_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    image_barrier(renderer->command_buffer, renderer->motion_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    image_barrier(renderer->command_buffer, renderer->depth_target.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
#ifdef DOOM_DLSS5
    if (renderer->dlss_enabled) evaluate_dlss(renderer);
#endif
    profile_stamp(renderer, 2);
    resolve_output(renderer);
    profile_stamp(renderer, 3);
#ifdef DOOM_FRAME_GENERATION
    copy_hudless(renderer);
#endif
    if (hud_count) {
        image_barrier(renderer->command_buffer, renderer->display_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        draw_hud(renderer, hud_count);
        image_barrier(renderer->command_buffer, renderer->display_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }
#ifdef DOOM_FRAME_GENERATION
    if (renderer->frame_generation) {
        evaluate_frame_generation(renderer, camera);
        renderer->targets_initialized = true;
        profile_stamp(renderer, 4);
        bool presented = present_generated_frame(renderer, renderer->history_valid);
        finish_frame_history(renderer, camera, presented);
        return;
    }
#endif
    image_barrier(renderer->command_buffer, renderer->display_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkImageLayout swap_old_layout = renderer->swapchain_initialized[image_index] ?
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier(renderer->command_buffer, renderer->swapchain_images[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
        swap_old_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        renderer->swapchain_initialized[image_index] ? VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT : 0,
        VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    blit_output(renderer, renderer->command_buffer, renderer->display_target.handle, renderer->swapchain_images[image_index]);
    image_barrier(renderer->command_buffer, renderer->swapchain_images[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE, 0);
    image_barrier(renderer->command_buffer, renderer->display_target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    renderer->targets_initialized = true;
    profile_stamp(renderer, 4);
    require_vk(vkEndCommandBuffer(renderer->command_buffer), "frame command end");

    VkSemaphoreSubmitInfo wait_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = renderer->image_available,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
    };
    VkCommandBufferSubmitInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = renderer->command_buffer
    };
    VkSemaphoreSubmitInfo signal_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = renderer->present_semaphores[image_index],
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
    };
    VkSubmitInfo2 submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &wait_info,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &command_info,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signal_info
    };
    require_vk(vkResetFences(renderer->device, 1, &renderer->frame_fence), "frame fence reset");
    require_vk(vkQueueSubmit2(renderer->graphics_queue, 1, &submit, renderer->frame_fence), "frame queue submit");
    renderer->swapchain_initialized[image_index] = true;
    finish_frame_history(renderer, camera, true);

    VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderer->present_semaphores[image_index],
        .swapchainCount = 1,
        .pSwapchains = &renderer->swapchain,
        .pImageIndices = &image_index
    };
    VkResult presented = vkQueuePresentKHR(renderer->present_queue, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR || acquire_suboptimal)
        renderer->swapchain_dirty = true;
    else require_vk(presented, "swapchain presentation");
}

static float half_to_float(uint16_t half) {
    unsigned sign = (unsigned)(half >> 15);
    unsigned exponent = (unsigned)(half >> 10) & 31u;
    unsigned mantissa = (unsigned)half & 1023u;
    float value;
    if (!exponent) value = mantissa ? ldexpf((float)mantissa, -24) : 0.0f;
    else if (exponent == 31u) value = mantissa ? NAN : INFINITY;
    else value = ldexpf((float)(1024u + mantissa), (int)exponent - 25);
    return sign ? -value : value;
}

typedef enum { CAPTURE_COLOR, CAPTURE_MOTION, CAPTURE_GENERATED } CaptureTarget;

static void readback(Renderer *renderer, CaptureTarget target) {
    bool motion = target == CAPTURE_MOTION;
    if (!renderer || !renderer->has_frame) fail("No rendered frame is available for capture");
    require_vk(vkWaitForFences(renderer->device, 1, &renderer->frame_fence, VK_TRUE, UINT64_MAX),
               "capture frame wait");
    const GpuImage *image = motion ? &renderer->motion_target : &renderer->display_target;
#ifdef DOOM_FRAME_GENERATION
    if (target == CAPTURE_GENERATED) image = &renderer->interpolated_target;
#endif
    VkExtent2D extent = motion ? renderer->work_extent : renderer->extent;
    const GpuBuffer *buffer = motion ? &renderer->motion_readback : &renderer->color_readback;
    GpuImage display = {0};
    if (!motion)
        display = create_image(renderer, renderer->extent.width, renderer->extent.height,
            renderer->swapchain_format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    VkCommandBuffer command = begin_immediate(renderer);
    image_barrier(command, image->handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    if (!motion) {
        image_barrier(command, display.handle, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        blit_output(renderer, command, image->handle, display.handle);
        image_barrier(command, display.handle, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    }
    VkBufferImageCopy copy = {
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {extent.width, extent.height, 1}
    };
    vkCmdCopyImageToBuffer(command, motion ? image->handle : display.handle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer->handle, 1, &copy);
    image_barrier(command, image->handle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    VkBufferMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT, .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer->handle, .size = VK_WHOLE_SIZE
    };
    VkDependencyInfo dependency = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                   .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(command, &dependency);
    end_immediate(renderer, command);
    if (!motion) {
        destroy_image(renderer, &display);
        if (renderer->swapchain_format == VK_FORMAT_B8G8R8A8_UNORM ||
            renderer->swapchain_format == VK_FORMAT_B8G8R8A8_SRGB) {
            uint8_t *pixels = buffer->mapped;
            for (size_t index = 0; index < (size_t)renderer->extent.width * renderer->extent.height; ++index) {
                uint8_t red = pixels[index * 4];
                pixels[index * 4] = pixels[index * 4 + 2];
                pixels[index * 4 + 2] = red;
            }
        }
    }
}

const uint8_t *renderer_pixels(Renderer *renderer, uint32_t *width, uint32_t *height) {
    readback(renderer, CAPTURE_COLOR);
    *width = renderer->extent.width;
    *height = renderer->extent.height;
    return renderer->color_readback.mapped;
}

const uint8_t *renderer_generated_pixels(Renderer *renderer, uint32_t *width, uint32_t *height) {
    *width = 0;
    *height = 0;
#ifdef DOOM_FRAME_GENERATION
    if (renderer->fg && renderer->has_frame && renderer->last_frame_generated) {
        readback(renderer, CAPTURE_GENERATED);
        *width = renderer->extent.width;
        *height = renderer->extent.height;
        return renderer->color_readback.mapped;
    }
#else
    (void)renderer;
#endif
    return NULL;
}

void renderer_capture(Renderer *renderer, const char *path) {
    if (!path || !*path) fail("Capture path is empty");
    readback(renderer, CAPTURE_COLOR);
    if (renderer->extent.width > INT_MAX || renderer->extent.height > INT_MAX ||
        renderer->extent.width > (uint32_t)(INT_MAX / 4)) fail("Capture dimensions exceed PNG limits");
    if (!stbi_write_png(path, (int)renderer->extent.width, (int)renderer->extent.height, 4,
                        renderer->color_readback.mapped, (int)(renderer->extent.width * 4)))
        fail("Cannot write screenshot %s", path);
}

float renderer_motion_stats(Renderer *renderer) {
    readback(renderer, CAPTURE_MOTION);
    const uint16_t *motion = renderer->motion_readback.mapped;
    size_t value_count = (size_t)renderer->work_extent.width * renderer->work_extent.height * 2;
    float maximum = 0;
    for (size_t index = 0; index < value_count; ++index) {
        float value = half_to_float(motion[index]);
        if (!isfinite(value)) return NAN;
        value = fabsf(value);
        if (value > maximum) maximum = value;
    }
    return maximum;
}

void renderer_capture_motion(Renderer *renderer, const char *path) {
    if (!path || !*path) fail("Motion capture path is empty");
    readback(renderer, CAPTURE_MOTION);
    FILE *file = fopen(path, "wb");
    if (!file) fail("Cannot create motion capture %s", path);
    if (fprintf(file, "PF\n%u %u\n-1.0\n", renderer->work_extent.width, renderer->work_extent.height) < 0)
        fail("Cannot write motion capture header %s", path);
    float *row = allocate((size_t)renderer->work_extent.width * 3, sizeof(*row));
    const uint16_t *motion = renderer->motion_readback.mapped;
    bool written = true;
    for (uint32_t output_row = 0; output_row < renderer->work_extent.height && written; ++output_row) {
        uint32_t source_row = renderer->work_extent.height - output_row - 1;
        for (uint32_t column = 0; column < renderer->work_extent.width; ++column) {
            size_t source = ((size_t)source_row * renderer->work_extent.width + column) * 2;
            size_t target = (size_t)column * 3;
            row[target] = half_to_float(motion[source]);
            row[target + 1] = half_to_float(motion[source + 1]);
            row[target + 2] = 0;
        }
        written = fwrite(row, sizeof(*row), (size_t)renderer->work_extent.width * 3, file) ==
                  (size_t)renderer->work_extent.width * 3;
    }
    free(row);
    bool closed = fclose(file) == 0;
    if (!written || !closed) fail("Cannot write motion capture %s", path);
}

void renderer_destroy(Renderer *renderer) {
    if (!renderer) return;
#ifdef DOOM_FRAME_GENERATION
    if (renderer->presentation) {
        flush_presentation(renderer);
        PresentationStats state = presentation_stats(renderer->presentation);
        printf("FG real=%llu generated=%llu total=%llu\n",
            (unsigned long long)state.real_frames, (unsigned long long)state.generated_frames,
            (unsigned long long)(state.real_frames + state.generated_frames));
        presentation_destroy(renderer->presentation);
        renderer->presentation = NULL;
    }
#endif
    if (renderer->device) require_vk(vkDeviceWaitIdle(renderer->device), "device wait before destroy");
    profile_read(renderer);
    if (renderer->profile_frames > 16) {
        double samples = (double)(renderer->profile_frames - 16);
        printf("PROFILE %ux%u work=%ux%u samples=%u frame=%.3fms GPU-world=%.3fms GPU-NR=%.3fms GPU-SR=%.3fms GPU-post=%.3fms\n",
               renderer->extent.width, renderer->extent.height, renderer->work_extent.width, renderer->work_extent.height,
               renderer->profile_frames - 16,
               renderer->profile_wall_ms / samples, renderer->profile_stage_ms[0] / samples,
               renderer->profile_stage_ms[1] / samples, renderer->profile_stage_ms[2] / samples,
               renderer->profile_stage_ms[3] / samples);
    }
    if (renderer->profile_pool) vkDestroyQueryPool(renderer->device, renderer->profile_pool, NULL);
#ifdef DOOM_FRAME_GENERATION
    if (renderer->fg) {
        char error[512];
        if (!doom_frame_generation_destroy(renderer->fg, error, sizeof(error)))
            fail("Frame generation shutdown: %s", error);
        renderer->fg = NULL;
    }
#endif
#ifdef DOOM_DLSS5
    if (renderer->dlss) {
        char error[512];
        if (!doom_dlss5_destroy(renderer->dlss, error, sizeof(error))) fail("DLSS 5 shutdown: %s", error);
    }
#endif
    release_queued_text(renderer);
    free(renderer->texts);
    free(renderer->font_buffer);
    if (renderer->device) {
        if (renderer->descriptor_pool) vkDestroyDescriptorPool(renderer->device, renderer->descriptor_pool, NULL);
        if (renderer->hud_pipeline) vkDestroyPipeline(renderer->device, renderer->hud_pipeline, NULL);
        if (renderer->sky_pipeline) vkDestroyPipeline(renderer->device, renderer->sky_pipeline, NULL);
        if (renderer->scene_pipeline) vkDestroyPipeline(renderer->device, renderer->scene_pipeline, NULL);
        if (renderer->pipeline_layout) vkDestroyPipelineLayout(renderer->device, renderer->pipeline_layout, NULL);
        if (renderer->descriptor_layout) vkDestroyDescriptorSetLayout(renderer->device, renderer->descriptor_layout, NULL);
        destroy_ray_lighting(renderer);
        destroy_buffer(renderer, &renderer->vertex_buffer);
        destroy_buffer(renderer, &renderer->uniform_buffer);
        destroy_targets(renderer);
        if (renderer->textures) {
            for (size_t index = 0; index < renderer->texture_count; ++index)
                destroy_image(renderer, &renderer->textures[index]);
        }
        free(renderer->textures);
        destroy_image(renderer, &renderer->palette);
        if (renderer->sampler) vkDestroySampler(renderer->device, renderer->sampler, NULL);
        destroy_swapchain(renderer);
        if (renderer->frame_fence) vkDestroyFence(renderer->device, renderer->frame_fence, NULL);
        if (renderer->image_available) vkDestroySemaphore(renderer->device, renderer->image_available, NULL);
#ifdef DOOM_FRAME_GENERATION
        if (renderer->fg_image_available) vkDestroySemaphore(renderer->device, renderer->fg_image_available, NULL);
        destroy_buffer(renderer, &renderer->fg_disable);
#endif
        if (renderer->command_pool) vkDestroyCommandPool(renderer->device, renderer->command_pool, NULL);
        vkDestroyDevice(renderer->device, NULL);
    }
    if (renderer->surface) vkDestroySurfaceKHR(renderer->instance, renderer->surface, NULL);
    if (renderer->debug_messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(renderer->instance,
                "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_messenger) destroy_messenger(renderer->instance, renderer->debug_messenger, NULL);
    }
    if (renderer->instance) vkDestroyInstance(renderer->instance, NULL);
    unsigned validation_errors = atomic_load_explicit(&renderer->validation_errors, memory_order_relaxed);
    free(renderer->descriptor_sets);
    free(renderer->batch_offsets);
    free(renderer->base_assets);
    free(renderer->vertices);
    free(renderer->previous_scene_positions);
    free(renderer->previous_sprite_positions);
    free(renderer);
    if (validation_errors) fail("Vulkan validation reported %u error(s)", validation_errors);
}
