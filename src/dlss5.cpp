#include "dlss5.h"

#ifndef DOOM_DLSS5
#error DOOM_DLSS5 must be defined when compiling this file
#endif

#ifndef _WIN32
#error DLSS 5 direct NGX runtime requires Windows
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_defs_dlssd.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_helpers_dlssd_vk.h>
#include <nvsdk_ngx_vk.h>

namespace {

constexpr char kProjectId[] = "1997bea8-f121-4527-b9cf-0caea4f83c24";
constexpr char kEngineVersion[] = "0.1";
constexpr unsigned long long kCommunitySnippetCompatibilityId = 0x1122334455667788ULL;
constexpr wchar_t kNRRuntimeName[] = L"nvngx_dlssnr.dll";
constexpr wchar_t kSRRuntimeName[] = L"nvngx_dlss.dll";
constexpr wchar_t kRRRuntimeName[] = L"nvngx_dlssd.dll";

struct TrackedImage {
    VkImage image;
    VkImageSubresourceRange range;
    bool bound;
    bool transitioned;
};
std::mutex image_tracking_mutex;
std::vector<TrackedImage> tracked_images;
bool track_fg_images = false;

VKAPI_ATTR VkResult VKAPI_CALL ngx_create_image(VkDevice device, const VkImageCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkImage *image) {
    VkResult result = vkCreateImage(device, info, allocator, image);
    if (result != VK_SUCCESS) return result;
    try {
        std::lock_guard<std::mutex> lock(image_tracking_mutex);
        if (track_fg_images && !(info->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
            tracked_images.push_back({*image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, info->mipLevels, 0, info->arrayLayers},
                false, info->initialLayout != VK_IMAGE_LAYOUT_UNDEFINED});
    } catch (const std::bad_alloc &) {
        vkDestroyImage(device, *image, allocator);
        *image = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL ngx_destroy_image(VkDevice device, VkImage image, const VkAllocationCallbacks *allocator) {
    {
        std::lock_guard<std::mutex> lock(image_tracking_mutex);
        tracked_images.erase(std::remove_if(tracked_images.begin(), tracked_images.end(),
            [image](const TrackedImage &tracked) { return tracked.image == image; }), tracked_images.end());
    }
    vkDestroyImage(device, image, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL ngx_bind_image(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    VkResult result = vkBindImageMemory(device, image, memory, offset);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::mutex> lock(image_tracking_mutex);
        for (auto &tracked : tracked_images) if (tracked.image == image) tracked.bound = true;
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL ngx_bind_images(VkDevice device, uint32_t count, const VkBindImageMemoryInfo *infos) {
    VkResult result = vkBindImageMemory2(device, count, infos);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::mutex> lock(image_tracking_mutex);
        for (uint32_t index = 0; index < count; ++index)
            for (auto &tracked : tracked_images) if (tracked.image == infos[index].image) tracked.bound = true;
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL ngx_barrier(VkCommandBuffer command, VkPipelineStageFlags source,
    VkPipelineStageFlags destination, VkDependencyFlags dependencies,
    uint32_t memory_count, const VkMemoryBarrier *memory,
    uint32_t buffer_count, const VkBufferMemoryBarrier *buffers,
    uint32_t image_count, const VkImageMemoryBarrier *images) {
    {
        std::lock_guard<std::mutex> lock(image_tracking_mutex);
        for (uint32_t index = 0; index < image_count; ++index)
            for (auto &tracked : tracked_images) if (tracked.image == images[index].image) tracked.transitioned = true;
    }
    vkCmdPipelineBarrier(command, source, destination, dependencies,
        memory_count, memory, buffer_count, buffers, image_count, images);
}

VKAPI_ATTR void VKAPI_CALL ngx_barrier2(VkCommandBuffer command, const VkDependencyInfo *info) {
    {
        std::lock_guard<std::mutex> lock(image_tracking_mutex);
        for (uint32_t index = 0; index < info->imageMemoryBarrierCount; ++index)
            for (auto &tracked : tracked_images)
                if (tracked.image == info->pImageMemoryBarriers[index].image) tracked.transitioned = true;
    }
    vkCmdPipelineBarrier2(command, info);
}

void ngx_transfer_dependency(VkCommandBuffer command) {
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

VKAPI_ATTR void VKAPI_CALL ngx_clear_color_image(
    VkCommandBuffer command, VkImage image, VkImageLayout layout,
    const VkClearColorValue *color, uint32_t range_count,
    const VkImageSubresourceRange *ranges) {
    ngx_transfer_dependency(command);
    vkCmdClearColorImage(command, image, layout, color, range_count, ranges);
}

VKAPI_ATTR void VKAPI_CALL ngx_fill_buffer(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset,
    VkDeviceSize size, uint32_t value) {
    ngx_transfer_dependency(command);
    vkCmdFillBuffer(command, buffer, offset, size, value);
}

PFN_vkVoidFunction ngx_override(const char *name, PFN_vkVoidFunction original) {
    if (!original) return nullptr;
    const struct { const char *name; PFN_vkVoidFunction function; } commands[] = {
        {"vkCreateImage", reinterpret_cast<PFN_vkVoidFunction>(ngx_create_image)},
        {"vkDestroyImage", reinterpret_cast<PFN_vkVoidFunction>(ngx_destroy_image)},
        {"vkBindImageMemory", reinterpret_cast<PFN_vkVoidFunction>(ngx_bind_image)},
        {"vkBindImageMemory2", reinterpret_cast<PFN_vkVoidFunction>(ngx_bind_images)},
        {"vkBindImageMemory2KHR", reinterpret_cast<PFN_vkVoidFunction>(ngx_bind_images)},
        {"vkCmdPipelineBarrier", reinterpret_cast<PFN_vkVoidFunction>(ngx_barrier)},
        {"vkCmdPipelineBarrier2", reinterpret_cast<PFN_vkVoidFunction>(ngx_barrier2)},
        {"vkCmdPipelineBarrier2KHR", reinterpret_cast<PFN_vkVoidFunction>(ngx_barrier2)},
        {"vkCmdClearColorImage", reinterpret_cast<PFN_vkVoidFunction>(ngx_clear_color_image)},
        {"vkCmdFillBuffer", reinterpret_cast<PFN_vkVoidFunction>(ngx_fill_buffer)}
    };
    for (const auto &entry : commands)
        if (std::strcmp(name, entry.name) == 0) return entry.function;
    return original;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ngx_device_proc(VkDevice device, const char *name) {
    return ngx_override(name, vkGetDeviceProcAddr(device, name));
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ngx_instance_proc(VkInstance instance, const char *name) {
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(ngx_device_proc);
    return ngx_override(name, vkGetInstanceProcAddr(instance, name));
}

using PfnInitExt2 = NVSDK_NGX_Result (NVSDK_CONV *)(
    unsigned long long,
    const wchar_t *,
    VkInstance,
    VkPhysicalDevice,
    VkDevice,
    PFN_vkGetInstanceProcAddr,
    PFN_vkGetDeviceProcAddr,
    NVSDK_NGX_Version,
    const NVSDK_NGX_Parameter *);
using PfnGetInstanceExtensions =
    decltype(&NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements);
using PfnGetDeviceExtensions =
    decltype(&NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements);
using PfnCreateFeature1 = decltype(&NVSDK_NGX_VULKAN_CreateFeature1);
using PfnEvaluateFeature = decltype(&NVSDK_NGX_VULKAN_EvaluateFeature);
using PfnReleaseFeature = decltype(&NVSDK_NGX_VULKAN_ReleaseFeature);
using PfnShutdown1 = decltype(&NVSDK_NGX_VULKAN_Shutdown1);

struct ScopedModule {
    HMODULE handle = nullptr;
    ~ScopedModule() {
        if (handle) FreeLibrary(handle);
    }
};

void clear_error(char *error, size_t capacity) {
    if (error && capacity) error[0] = '\0';
}

bool fail(char *error, size_t capacity, const char *message) {
    if (error && capacity) std::snprintf(error, capacity, "%s", message);
    return false;
}

bool win_fail(char *error, size_t capacity, const char *operation) {
    if (error && capacity)
        std::snprintf(error, capacity, "%s failed with Win32 error 0x%08X", operation,
                      static_cast<unsigned int>(GetLastError()));
    return false;
}

bool ngx_fail(char *error, size_t capacity, const char *operation, NVSDK_NGX_Result result) {
    if (error && capacity)
        std::snprintf(error, capacity, "%s failed with NGX result 0x%08X", operation,
                      static_cast<unsigned int>(result));
    return false;
}

bool ngx_failed(NVSDK_NGX_Result result) {
    return (static_cast<uint32_t>(result) & 0xFFF00000u) ==
           static_cast<uint32_t>(NVSDK_NGX_Result_Fail);
}

template <typename Function>
bool resolve_export(HMODULE module, const char *name, Function *function, char *error, size_t capacity) {
    SetLastError(ERROR_SUCCESS);
    FARPROC address = GetProcAddress(module, name);
    if (!address) {
        char operation[160];
        std::snprintf(operation, sizeof(operation), "GetProcAddress(%s)", name);
        return win_fail(error, capacity, operation);
    }
    *function = reinterpret_cast<Function>(address);
    return true;
}

bool runtime_path(
    const wchar_t *runtime_name,
    std::wstring *path,
    char *error,
    size_t capacity) {
    if (!path) return fail(error, capacity, "runtime path output is null");
    if (!runtime_name || !runtime_name[0])
        return fail(error, capacity, "runtime name must be nonempty");
    std::vector<wchar_t> buffer(512);
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                 static_cast<DWORD>(buffer.size()));
        if (!length) return win_fail(error, capacity, "GetModuleFileNameW");
        if (length < buffer.size()) {
            std::wstring executable(buffer.data(), length);
            const size_t separator = executable.find_last_of(L"\\/");
            if (separator == std::wstring::npos)
                return fail(error, capacity, "current executable path has no directory");
            *path = executable.substr(0, separator + 1);
            path->append(runtime_name);
            return true;
        }
        if (buffer.size() >= 32768)
            return fail(error, capacity, "current executable path exceeds 32767 characters");
        buffer.resize(std::min<size_t>(buffer.size() * 2, 32768));
    }
}

bool load_runtime(
    const wchar_t *runtime_name,
    HMODULE *module,
    char *error,
    size_t capacity) {
    if (!module) return fail(error, capacity, "runtime module output is null");
    *module = nullptr;
    std::wstring path;
    if (!runtime_path(runtime_name, &path, error, capacity)) return false;
    SetLastError(ERROR_SUCCESS);
    *module = LoadLibraryExW(path.c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!*module) return win_fail(error, capacity, "LoadLibraryExW(NGX feature runtime)");
    return true;
}

void make_discovery_info(
    const wchar_t *application_data_path,
    NVSDK_NGX_Feature feature,
    NVSDK_NGX_FeatureCommonInfo *common_info,
    NVSDK_NGX_FeatureDiscoveryInfo *discovery_info) {
    *common_info = {};
    *discovery_info = {};
    discovery_info->SDKVersion = NVSDK_NGX_Version_API;
    discovery_info->FeatureID = feature;
    discovery_info->Identifier.IdentifierType =
        NVSDK_NGX_Application_Identifier_Type_Project_Id;
    discovery_info->Identifier.v.ProjectDesc.ProjectId = kProjectId;
    discovery_info->Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    discovery_info->Identifier.v.ProjectDesc.EngineVersion = kEngineVersion;
    discovery_info->ApplicationDataPath = application_data_path;
    discovery_info->FeatureInfo = common_info;
}

bool query_extensions(
    const wchar_t *application_data_path,
    VkInstance instance,
    VkPhysicalDevice physical_device,
    bool device_query,
    VkExtensionProperties *extensions,
    uint32_t capacity,
    uint32_t *extension_count,
    char *error,
    size_t error_capacity) {
    clear_error(error, error_capacity);
    if (!application_data_path || !application_data_path[0])
        return fail(error, error_capacity, "application data path must be nonempty");
    if (!extension_count) return fail(error, error_capacity, "extension_count is null");
    *extension_count = 0;
    if (!extensions && capacity)
        return fail(error, error_capacity, "extension output is null but capacity is nonzero");
    if (device_query && (instance == VK_NULL_HANDLE || physical_device == VK_NULL_HANDLE))
        return fail(error, error_capacity, "instance and physical_device are required");

    std::vector<VkExtensionProperties> combined_extensions;
    auto append_feature_extensions = [&](const wchar_t *runtime_name,
                                         NVSDK_NGX_Feature feature,
                                         const char *operation,
                                         char *feature_error,
                                         size_t feature_error_capacity) {
        clear_error(feature_error, feature_error_capacity);
        ScopedModule runtime;
        if (!load_runtime(runtime_name, &runtime.handle, feature_error,
                          feature_error_capacity)) return false;

        PfnGetInstanceExtensions get_instance_extensions = nullptr;
        PfnGetDeviceExtensions get_device_extensions = nullptr;
        if (device_query) {
            if (!resolve_export(runtime.handle,
                                "NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements",
                                &get_device_extensions, feature_error,
                                feature_error_capacity))
                return false;
        } else if (!resolve_export(
                       runtime.handle,
                       "NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements",
                       &get_instance_extensions, feature_error,
                       feature_error_capacity)) {
            return false;
        }

        NVSDK_NGX_FeatureCommonInfo common_info{};
        NVSDK_NGX_FeatureDiscoveryInfo discovery_info{};
        make_discovery_info(application_data_path, feature, &common_info, &discovery_info);
        uint32_t required_count = 0;
        VkExtensionProperties *required = nullptr;
        const NVSDK_NGX_Result result = device_query ?
            get_device_extensions(instance, physical_device, &discovery_info,
                                  &required_count, &required) :
            get_instance_extensions(&discovery_info, &required_count, &required);
        if (ngx_failed(result))
            return ngx_fail(feature_error, feature_error_capacity, operation, result);
        if (required_count && !required)
            return fail(feature_error, feature_error_capacity,
                        "NGX returned a null extension list");

        for (uint32_t index = 0; index < required_count; ++index) {
            auto existing = std::find_if(
                combined_extensions.begin(), combined_extensions.end(),
                [&](const VkExtensionProperties &entry) {
                    return std::strncmp(entry.extensionName, required[index].extensionName,
                                        VK_MAX_EXTENSION_NAME_SIZE) == 0;
                });
            if (existing == combined_extensions.end()) {
                combined_extensions.push_back(required[index]);
            } else {
                existing->specVersion = std::max(existing->specVersion, required[index].specVersion);
            }
        }
        return true;
    };

    if (!append_feature_extensions(kNRRuntimeName, NVSDK_NGX_Feature_Reserved18,
                                   "DLSSNR Vulkan extension query", error,
                                   error_capacity) ||
        !append_feature_extensions(kSRRuntimeName, NVSDK_NGX_Feature_SuperSampling,
                                   "DLSS Vulkan extension query", error,
                                   error_capacity))
        return false;

    std::wstring rr_runtime_path;
    if (!runtime_path(kRRRuntimeName, &rr_runtime_path, error, error_capacity))
        return false;
    if (GetFileAttributesW(rr_runtime_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        char rr_error[256]{};
        append_feature_extensions(kRRRuntimeName, NVSDK_NGX_Feature_RayReconstruction,
                                  "DLSS RR Vulkan extension query", rr_error,
                                  sizeof(rr_error));
    }

    if (combined_extensions.size() > UINT32_MAX)
        return fail(error, error_capacity, "NGX extension list exceeds uint32 capacity");
    *extension_count = static_cast<uint32_t>(combined_extensions.size());
    if (!extensions) return true;
    if (capacity < *extension_count) {
        if (error && error_capacity)
            std::snprintf(error, error_capacity, "extension output needs %u entries; capacity is %u",
                          *extension_count, capacity);
        return false;
    }
    if (*extension_count) std::copy_n(combined_extensions.data(), *extension_count, extensions);
    return true;
}

bool validate_image(
    const DoomDlss5Image &image,
    VkFormat format,
    const char *name,
    char *error,
    size_t error_capacity) {
    if (image.image == VK_NULL_HANDLE || image.view == VK_NULL_HANDLE) {
        if (error && error_capacity)
            std::snprintf(error, error_capacity, "%s image and view are required", name);
        return false;
    }
    if (image.format != format) {
        if (error && error_capacity)
            std::snprintf(error, error_capacity, "%s has an invalid Vulkan format", name);
        return false;
    }
    if (!image.width || !image.height) {
        if (error && error_capacity)
            std::snprintf(error, error_capacity, "%s dimensions must be nonzero", name);
        return false;
    }
    if (image.layout != VK_IMAGE_LAYOUT_GENERAL) {
        if (error && error_capacity)
            std::snprintf(error, error_capacity, "%s must be in VK_IMAGE_LAYOUT_GENERAL", name);
        return false;
    }
    return true;
}

}

struct DoomDlss5 {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    NVSDK_NGX_Parameter *parameters = nullptr;
    NVSDK_NGX_Parameter *sr_parameters = nullptr;
    NVSDK_NGX_Parameter *rr_parameters = nullptr;
    NVSDK_NGX_Handle *features[4]{};
    NVSDK_NGX_Handle *sr_feature = nullptr;
    NVSDK_NGX_Handle *rr_feature = nullptr;
    unsigned pass_count = 0;
    HMODULE runtime = nullptr;
    PfnCreateFeature1 create_feature = nullptr;
    PfnEvaluateFeature evaluate_feature = nullptr;
    PfnReleaseFeature release_feature = nullptr;
    PfnShutdown1 shutdown_runtime = nullptr;
    VkExtent2D output_size{};
    VkExtent2D sr_output_size{};
    VkExtent2D sr_work_size{};
    VkExtent2D rr_output_size{};
    VkExtent2D rr_work_size{};
    DoomDlss5SRMode sr_mode = DOOM_DLSS5_SR_OFF;
    DoomDlss5SRMode rr_mode = DOOM_DLSS5_SR_OFF;
    std::wstring application_data_path;
    bool core_initialized = false;
    bool snippet_initialized = false;
    bool sr_reset_pending = true;
    bool rr_reset_pending = true;
    bool reset_pending[4]{};
};

namespace {

bool release_sr_feature(DoomDlss5 *dlss5, char *error, size_t error_capacity) {
    bool success = true;
    if (dlss5->sr_feature) {
        const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_ReleaseFeature(dlss5->sr_feature);
        dlss5->sr_feature = nullptr;
        if (ngx_failed(result)) {
            ngx_fail(error, error_capacity, "DLSS SR feature release", result);
            success = false;
        }
    }
    dlss5->sr_mode = DOOM_DLSS5_SR_OFF;
    dlss5->sr_output_size = {};
    dlss5->sr_work_size = {};
    dlss5->sr_reset_pending = true;
    return success;
}

bool release_rr_feature(DoomDlss5 *dlss5, char *error, size_t error_capacity) {
    bool success = true;
    if (dlss5->rr_feature) {
        const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_ReleaseFeature(dlss5->rr_feature);
        dlss5->rr_feature = nullptr;
        if (ngx_failed(result)) {
            ngx_fail(error, error_capacity, "DLSS RR feature release", result);
            success = false;
        }
    }
    dlss5->rr_output_size = {};
    dlss5->rr_work_size = {};
    dlss5->rr_mode = DOOM_DLSS5_SR_OFF;
    dlss5->rr_reset_pending = true;
    return success;
}

bool release_feature(DoomDlss5 *dlss5, char *error, size_t error_capacity) {
    bool success = true;
    for (auto &feature : dlss5->features) {
        if (!feature) continue;
        const NVSDK_NGX_Result result = dlss5->release_feature(feature);
        feature = nullptr;
        if (ngx_failed(result) && success) {
            ngx_fail(error, error_capacity, "DLSSNR feature release", result);
            success = false;
        }
    }
    dlss5->pass_count = 0;
    dlss5->output_size = {};
    std::fill_n(dlss5->reset_pending, 4, true);
    return success;
}

bool shutdown(DoomDlss5 *dlss5, char *error, size_t error_capacity) {
    clear_error(error, error_capacity);
    bool success = true;
    auto remember_ngx = [&](const char *operation, NVSDK_NGX_Result result) {
        if (success) {
            ngx_fail(error, error_capacity, operation, result);
            success = false;
        }
    };
    auto remember_win = [&](const char *operation) {
        if (success) {
            win_fail(error, error_capacity, operation);
            success = false;
        }
    };

    if (!release_rr_feature(dlss5, error, error_capacity)) success = false;
    if (!release_sr_feature(dlss5, error, error_capacity)) success = false;
    if (!release_feature(dlss5, error, error_capacity)) success = false;
    if (dlss5->sr_parameters) {
        const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_DestroyParameters(dlss5->sr_parameters);
        dlss5->sr_parameters = nullptr;
        if (ngx_failed(result)) remember_ngx("DLSS SR parameter destroy", result);
    }
    if (dlss5->rr_parameters) {
        const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_DestroyParameters(dlss5->rr_parameters);
        dlss5->rr_parameters = nullptr;
        if (ngx_failed(result)) remember_ngx("DLSS RR parameter destroy", result);
    }
    if (dlss5->parameters) {
        const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_DestroyParameters(dlss5->parameters);
        dlss5->parameters = nullptr;
        if (ngx_failed(result)) remember_ngx("NGX parameter destroy", result);
    }
    if (dlss5->core_initialized) {
        const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Shutdown1(dlss5->device);
        dlss5->core_initialized = false;
        if (ngx_failed(result)) remember_ngx("NGX Vulkan shutdown", result);
    }
    if (dlss5->snippet_initialized) {
        const NVSDK_NGX_Result result = dlss5->shutdown_runtime(dlss5->device);
        dlss5->snippet_initialized = false;
        if (ngx_failed(result)) remember_ngx("DLSSNR runtime shutdown", result);
    }
    if (dlss5->runtime) {
        if (!FreeLibrary(dlss5->runtime)) remember_win("FreeLibrary(nvngx_dlssnr.dll)");
        dlss5->runtime = nullptr;
    }
    dlss5->instance = VK_NULL_HANDLE;
    dlss5->physical_device = VK_NULL_HANDLE;
    dlss5->device = VK_NULL_HANDLE;
    dlss5->create_feature = nullptr;
    dlss5->evaluate_feature = nullptr;
    dlss5->release_feature = nullptr;
    dlss5->shutdown_runtime = nullptr;
    dlss5->output_size = {};
    dlss5->sr_output_size = {};
    dlss5->sr_work_size = {};
    dlss5->rr_output_size = {};
    dlss5->rr_work_size = {};
    dlss5->sr_mode = DOOM_DLSS5_SR_OFF;
    dlss5->rr_mode = DOOM_DLSS5_SR_OFF;
    dlss5->sr_reset_pending = true;
    dlss5->rr_reset_pending = true;
    dlss5->application_data_path.clear();
    return success;
}

bool valid_sr_mode(DoomDlss5SRMode mode) {
    return mode == DOOM_DLSS5_SR_OFF || mode == DOOM_DLSS5_SR_QUALITY ||
           mode == DOOM_DLSS5_SR_BALANCED || mode == DOOM_DLSS5_SR_PERFORMANCE;
}

NVSDK_NGX_PerfQuality_Value dlss_quality_value(DoomDlss5SRMode mode) {
    switch (mode) {
        case DOOM_DLSS5_SR_OFF: return NVSDK_NGX_PerfQuality_Value_DLAA;
        case DOOM_DLSS5_SR_QUALITY: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
        case DOOM_DLSS5_SR_BALANCED: return NVSDK_NGX_PerfQuality_Value_Balanced;
        case DOOM_DLSS5_SR_PERFORMANCE: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
        default: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    }
}

}

extern "C" {

void doom_dlss5_begin_fg_images(void) {
    std::lock_guard<std::mutex> lock(image_tracking_mutex);
    tracked_images.clear();
    track_fg_images = true;
}

void doom_dlss5_end_fg_images(VkCommandBuffer command) {
    std::lock_guard<std::mutex> lock(image_tracking_mutex);
    track_fg_images = false;
    if (command) {
        for (const auto &image : tracked_images) {
            if (!image.bound || image.transitioned) continue;
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.image = image.image;
            barrier.subresourceRange = image.range;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    }
    tracked_images.clear();
}

bool doom_dlss5_get_instance_extensions(
    const wchar_t *application_data_path,
    VkExtensionProperties *extensions,
    uint32_t extension_capacity,
    uint32_t *extension_count,
    char *error,
    size_t error_capacity) {
    return query_extensions(application_data_path, VK_NULL_HANDLE, VK_NULL_HANDLE, false,
                            extensions, extension_capacity, extension_count, error, error_capacity);
}

bool doom_dlss5_get_device_extensions(
    const wchar_t *application_data_path,
    VkInstance instance,
    VkPhysicalDevice physical_device,
    VkExtensionProperties *extensions,
    uint32_t extension_capacity,
    uint32_t *extension_count,
    char *error,
    size_t error_capacity) {
    return query_extensions(application_data_path, instance, physical_device, true,
                            extensions, extension_capacity, extension_count, error, error_capacity);
}

bool doom_dlss5_create(
    const DoomDlss5InitInfo *info,
    DoomDlss5 **out_dlss5,
    char *error,
    size_t error_capacity) {
    clear_error(error, error_capacity);
    if (!info) return fail(error, error_capacity, "init info is null");
    if (!out_dlss5) return fail(error, error_capacity, "out_dlss5 is null");
    *out_dlss5 = nullptr;
    if (info->instance == VK_NULL_HANDLE || info->physical_device == VK_NULL_HANDLE ||
        info->device == VK_NULL_HANDLE)
        return fail(error, error_capacity, "instance, physical_device, and device are required");
    if (!info->application_data_path || !info->application_data_path[0])
        return fail(error, error_capacity, "application data path must be nonempty");

    auto *dlss5 = new (std::nothrow) DoomDlss5{};
    if (!dlss5) return fail(error, error_capacity, "DLSSNR context allocation failed");
    dlss5->instance = info->instance;
    dlss5->physical_device = info->physical_device;
    dlss5->device = info->device;
    dlss5->application_data_path = info->application_data_path;

    {
        NVSDK_NGX_FeatureCommonInfo common_info{};
        const NVSDK_NGX_Result init_result = NVSDK_NGX_VULKAN_Init_with_ProjectID(
            kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion,
            dlss5->application_data_path.c_str(), dlss5->instance, dlss5->physical_device,
            dlss5->device, ngx_instance_proc, ngx_device_proc, &common_info,
            NVSDK_NGX_Version_API);
        if (ngx_failed(init_result)) {
            ngx_fail(error, error_capacity, "NGX Vulkan Init_with_ProjectID", init_result);
            delete dlss5;
            return false;
        }
    }
    dlss5->core_initialized = true;

    const NVSDK_NGX_Result params_result =
        NVSDK_NGX_VULKAN_GetCapabilityParameters(&dlss5->parameters);
    if (ngx_failed(params_result) || !dlss5->parameters) {
        if (ngx_failed(params_result))
            ngx_fail(error, error_capacity, "NGX capability parameter query", params_result);
        else
            fail(error, error_capacity, "NGX returned null capability parameters");
        shutdown(dlss5, nullptr, 0);
        delete dlss5;
        return false;
    }

    const NVSDK_NGX_Result sr_params_result =
        NVSDK_NGX_VULKAN_GetCapabilityParameters(&dlss5->sr_parameters);
    if (ngx_failed(sr_params_result) || !dlss5->sr_parameters) {
        if (ngx_failed(sr_params_result))
            ngx_fail(error, error_capacity, "DLSS SR capability parameter query", sr_params_result);
        else
            fail(error, error_capacity, "NGX returned null DLSS SR capability parameters");
        shutdown(dlss5, nullptr, 0);
        delete dlss5;
        return false;
    }

    if (!load_runtime(kNRRuntimeName, &dlss5->runtime, error, error_capacity)) {
        shutdown(dlss5, nullptr, 0);
        delete dlss5;
        return false;
    }

    PfnInitExt2 init_runtime = nullptr;
    if (!resolve_export(dlss5->runtime, "NVSDK_NGX_VULKAN_Init_Ext2",
                        &init_runtime, error, error_capacity) ||
        !resolve_export(dlss5->runtime, "NVSDK_NGX_VULKAN_CreateFeature1",
                        &dlss5->create_feature, error, error_capacity) ||
        !resolve_export(dlss5->runtime, "NVSDK_NGX_VULKAN_EvaluateFeature",
                        &dlss5->evaluate_feature, error, error_capacity) ||
        !resolve_export(dlss5->runtime, "NVSDK_NGX_VULKAN_ReleaseFeature",
                        &dlss5->release_feature, error, error_capacity) ||
        !resolve_export(dlss5->runtime, "NVSDK_NGX_VULKAN_Shutdown1",
                        &dlss5->shutdown_runtime, error, error_capacity)) {
        shutdown(dlss5, nullptr, 0);
        delete dlss5;
        return false;
    }

    const NVSDK_NGX_Result runtime_init = init_runtime(
        kCommunitySnippetCompatibilityId, dlss5->application_data_path.c_str(),
        dlss5->instance, dlss5->physical_device, dlss5->device,
        vkGetInstanceProcAddr, vkGetDeviceProcAddr, NVSDK_NGX_Version_API,
        dlss5->parameters);
    if (ngx_failed(runtime_init)) {
        ngx_fail(error, error_capacity, "DLSSNR snippet Init_Ext2", runtime_init);
        shutdown(dlss5, nullptr, 0);
        delete dlss5;
        return false;
    }
    dlss5->snippet_initialized = true;
    *out_dlss5 = dlss5;
    return true;
}


bool doom_dlss5_upscale(
    DoomDlss5 *dlss5,
    VkCommandBuffer command_buffer,
    const DoomDlss5Frame *frame,
    DoomDlss5SRMode mode,
    float jitter_x_pixels,
    float jitter_y_pixels,
    bool reset_history,
    char *error,
    size_t error_capacity) {
    clear_error(error, error_capacity);
    if (!dlss5 || !dlss5->core_initialized || !dlss5->snippet_initialized ||
        !dlss5->parameters || !dlss5->sr_parameters)
        return fail(error, error_capacity, "NGX runtime is not initialized");
    if (!valid_sr_mode(mode)) return fail(error, error_capacity, "invalid DLSS SR mode");
    if (mode == DOOM_DLSS5_SR_OFF)
        return release_sr_feature(dlss5, error, error_capacity);
    if (command_buffer == VK_NULL_HANDLE)
        return fail(error, error_capacity, "command_buffer is null");
    if (!frame) return fail(error, error_capacity, "frame is null");
    if (!std::isfinite(jitter_x_pixels) || !std::isfinite(jitter_y_pixels))
        return fail(error, error_capacity, "jitter offsets must be finite");

    const VkExtent2D work_extent{frame->color.width, frame->color.height};
    const VkExtent2D output_extent{frame->output.width, frame->output.height};
    if (!validate_image(frame->color, VK_FORMAT_R8G8B8A8_UNORM, "color", error, error_capacity) ||
        !validate_image(frame->motion, VK_FORMAT_R16G16_SFLOAT, "motion", error, error_capacity) ||
        !validate_image(frame->depth, VK_FORMAT_D32_SFLOAT, "depth", error, error_capacity) ||
        !validate_image(frame->output, VK_FORMAT_R8G8B8A8_UNORM, "output", error, error_capacity))
        return false;
    if (frame->color.width != work_extent.width || frame->color.height != work_extent.height ||
        frame->motion.width != work_extent.width || frame->motion.height != work_extent.height ||
        frame->depth.width != work_extent.width || frame->depth.height != work_extent.height)
        return fail(error, error_capacity, "color, motion, and depth dimensions must match the render size");
    if (work_extent.width > output_extent.width || work_extent.height > output_extent.height)
        return fail(error, error_capacity, "DLSS SR output must not be smaller than its input");

    if (dlss5->sr_feature &&
        (dlss5->sr_mode != mode ||
         dlss5->sr_output_size.width != output_extent.width ||
         dlss5->sr_output_size.height != output_extent.height ||
         dlss5->sr_work_size.width != work_extent.width ||
         dlss5->sr_work_size.height != work_extent.height)) {
        if (!release_sr_feature(dlss5, error, error_capacity)) return false;
    }

    if (!dlss5->sr_feature) {
        NVSDK_NGX_DLSS_Create_Params create_params{};
        create_params.Feature.InWidth = work_extent.width;
        create_params.Feature.InHeight = work_extent.height;
        create_params.Feature.InTargetWidth = output_extent.width;
        create_params.Feature.InTargetHeight = output_extent.height;
        create_params.Feature.InPerfQualityValue = work_extent.width == output_extent.width && work_extent.height == output_extent.height ?
            NVSDK_NGX_PerfQuality_Value_DLAA : dlss_quality_value(mode);
        create_params.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVJittered |
                                             NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        const NVSDK_NGX_Result create_result = NGX_VULKAN_CREATE_DLSS_EXT1(
            dlss5->device, command_buffer, 1, 1, &dlss5->sr_feature,
            dlss5->sr_parameters, &create_params);
        if (ngx_failed(create_result)) {
            dlss5->sr_feature = nullptr;
            return ngx_fail(error, error_capacity, "DLSS SR CreateFeature", create_result);
        }
        if (!dlss5->sr_feature)
            return fail(error, error_capacity, "NGX returned a null DLSS SR feature handle");
        dlss5->sr_mode = mode;
        dlss5->sr_output_size = output_extent;
        dlss5->sr_work_size = work_extent;
        dlss5->sr_reset_pending = true;
    }

    const VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkImageSubresourceRange depth_range{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    NVSDK_NGX_Resource_VK color_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->color.view, frame->color.image, color_range, frame->color.format,
        frame->color.width, frame->color.height, false);
    NVSDK_NGX_Resource_VK motion_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->motion.view, frame->motion.image, color_range, frame->motion.format,
        frame->motion.width, frame->motion.height, false);
    NVSDK_NGX_Resource_VK depth_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->depth.view, frame->depth.image, depth_range, frame->depth.format,
        frame->depth.width, frame->depth.height, false);
    NVSDK_NGX_Resource_VK output_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->output.view, frame->output.image, color_range, frame->output.format,
        frame->output.width, frame->output.height, true);

    NVSDK_NGX_VK_DLSS_Eval_Params eval_params{};
    eval_params.Feature.pInColor = &color_resource;
    eval_params.Feature.pInOutput = &output_resource;
    eval_params.pInDepth = &depth_resource;
    eval_params.pInMotionVectors = &motion_resource;
    eval_params.InJitterOffsetX = jitter_x_pixels;
    eval_params.InJitterOffsetY = jitter_y_pixels;
    eval_params.InRenderSubrectDimensions = {work_extent.width, work_extent.height};
    eval_params.InReset = (reset_history || dlss5->sr_reset_pending) ? 1 : 0;
    eval_params.InMVScaleX = -static_cast<float>(work_extent.width);
    eval_params.InMVScaleY = -static_cast<float>(work_extent.height);
    eval_params.InToneMapperType = NVSDK_NGX_TONEMAPPER_STRING;

    const NVSDK_NGX_Result evaluate_result = NGX_VULKAN_EVALUATE_DLSS_EXT(
        command_buffer, dlss5->sr_feature, dlss5->sr_parameters, &eval_params);
    if (ngx_failed(evaluate_result)) {
        dlss5->sr_reset_pending = true;
        return ngx_fail(error, error_capacity, "DLSS SR EvaluateFeature", evaluate_result);
    }
    dlss5->sr_reset_pending = false;
    return true;
}

bool doom_dlss5_reconstruct(
    DoomDlss5 *dlss5,
    VkCommandBuffer command_buffer,
    const DoomDlss5ReconstructionFrame *frame,
    char *error,
    size_t error_capacity) {
    clear_error(error, error_capacity);
    if (!dlss5 || !dlss5->core_initialized)
        return fail(error, error_capacity, "NGX runtime is not initialized");
    if (command_buffer == VK_NULL_HANDLE)
        return fail(error, error_capacity, "command_buffer is null");
    if (!frame) return fail(error, error_capacity, "frame is null");
    if (!valid_sr_mode(frame->mode))
        return fail(error, error_capacity, "invalid DLSS RR performance mode");
    if (!std::isfinite(frame->jitter_x_pixels) || !std::isfinite(frame->jitter_y_pixels))
        return fail(error, error_capacity, "jitter offsets must be finite");

    const VkExtent2D output_extent{frame->output.width, frame->output.height};
    const VkExtent2D work_extent{frame->color.width, frame->color.height};
    if (!validate_image(frame->color, VK_FORMAT_R16G16B16A16_SFLOAT,
                        "RR color", error, error_capacity) ||
        !validate_image(frame->motion, VK_FORMAT_R16G16_SFLOAT,
                        "RR motion", error, error_capacity) ||
        !validate_image(frame->depth, VK_FORMAT_R32_SFLOAT,
                        "RR linear depth", error, error_capacity) ||
        !validate_image(frame->diffuse_albedo, VK_FORMAT_R16G16B16A16_SFLOAT,
                        "RR diffuse albedo", error, error_capacity) ||
        !validate_image(frame->specular_albedo, VK_FORMAT_R16G16B16A16_SFLOAT,
                        "RR specular albedo", error, error_capacity) ||
        !validate_image(frame->normals_roughness, VK_FORMAT_R16G16B16A16_SFLOAT,
                        "RR normals and roughness", error, error_capacity) ||
        !validate_image(frame->output, VK_FORMAT_R16G16B16A16_SFLOAT,
                        "RR output", error, error_capacity))
        return false;

    const DoomDlss5Image *input_images[] = {
        &frame->color,
        &frame->motion,
        &frame->depth,
        &frame->diffuse_albedo,
        &frame->specular_albedo,
        &frame->normals_roughness
    };
    for (const DoomDlss5Image *image : input_images) {
        if (image->width != work_extent.width || image->height != work_extent.height)
            return fail(error, error_capacity, "RR input images must match the selected render size");
    }
    if (frame->mode == DOOM_DLSS5_SR_OFF &&
        (work_extent.width != output_extent.width || work_extent.height != output_extent.height))
        return fail(error, error_capacity, "DLAA requires matching input and output dimensions");
    if (work_extent.width > output_extent.width || work_extent.height > output_extent.height)
        return fail(error, error_capacity, "DLSS RR output must not be smaller than its input");

    if (dlss5->rr_feature &&
        (dlss5->rr_mode != frame->mode ||
         dlss5->rr_output_size.width != output_extent.width ||
         dlss5->rr_output_size.height != output_extent.height ||
         dlss5->rr_work_size.width != work_extent.width ||
         dlss5->rr_work_size.height != work_extent.height)) {
        if (!release_rr_feature(dlss5, error, error_capacity)) return false;
    }

    if (!dlss5->rr_feature) {
        std::wstring rr_runtime_path;
        if (!runtime_path(kRRRuntimeName, &rr_runtime_path, error, error_capacity))
            return false;
        if (GetFileAttributesW(rr_runtime_path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return fail(error, error_capacity,
                        "nvngx_dlssd.dll must be beside the executable to use DLSS RR");

        if (!dlss5->rr_parameters) {
            const NVSDK_NGX_Result params_result =
                NVSDK_NGX_VULKAN_GetCapabilityParameters(&dlss5->rr_parameters);
            if (ngx_failed(params_result) || !dlss5->rr_parameters) {
                if (ngx_failed(params_result))
                    return ngx_fail(error, error_capacity,
                                    "DLSS RR capability parameter query", params_result);
                return fail(error, error_capacity,
                            "NGX returned null DLSS RR capability parameters");
            }
        }

        unsigned int available = 0;
        const NVSDK_NGX_Result availability_result = NVSDK_NGX_Parameter_GetUI(
            dlss5->rr_parameters, NVSDK_NGX_Parameter_SuperSamplingDenoising_Available,
            &available);
        if (ngx_failed(availability_result))
            return ngx_fail(error, error_capacity, "DLSS RR capability query",
                            availability_result);
        if (!available) {
            unsigned int needs_updated_driver = 0;
            const NVSDK_NGX_Result driver_result = NVSDK_NGX_Parameter_GetUI(
                dlss5->rr_parameters,
                NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver,
                &needs_updated_driver);
            if (!ngx_failed(driver_result) && needs_updated_driver)
                return fail(error, error_capacity,
                            "DLSS RR requires a newer NVIDIA driver");
            return fail(error, error_capacity,
                        "DLSS RR is unavailable on this GPU or driver");
        }

        NVSDK_NGX_DLSSD_Create_Params create_params{};
        create_params.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
        create_params.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Packed;
        create_params.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_Linear;
        create_params.InWidth = work_extent.width;
        create_params.InHeight = work_extent.height;
        create_params.InTargetWidth = output_extent.width;
        create_params.InTargetHeight = output_extent.height;
        create_params.InPerfQualityValue = dlss_quality_value(frame->mode);
        create_params.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
                                             NVSDK_NGX_DLSS_Feature_Flags_MVJittered |
                                             NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        const NVSDK_NGX_Result create_result = NGX_VULKAN_CREATE_DLSSD_EXT1(
            dlss5->device, command_buffer, 1, 1, &dlss5->rr_feature,
            dlss5->rr_parameters, &create_params);
        if (ngx_failed(create_result)) {
            dlss5->rr_feature = nullptr;
            return ngx_fail(error, error_capacity, "DLSS RR CreateFeature", create_result);
        }
        if (!dlss5->rr_feature)
            return fail(error, error_capacity, "NGX returned a null DLSS RR feature handle");
        dlss5->rr_mode = frame->mode;
        dlss5->rr_output_size = output_extent;
        dlss5->rr_work_size = work_extent;
        dlss5->rr_reset_pending = true;
    }

    const VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    NVSDK_NGX_Resource_VK color_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->color.view, frame->color.image, color_range, frame->color.format,
        frame->color.width, frame->color.height, false);
    NVSDK_NGX_Resource_VK motion_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->motion.view, frame->motion.image, color_range, frame->motion.format,
        frame->motion.width, frame->motion.height, false);
    NVSDK_NGX_Resource_VK depth_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->depth.view, frame->depth.image, color_range, frame->depth.format,
        frame->depth.width, frame->depth.height, false);
    NVSDK_NGX_Resource_VK diffuse_albedo_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->diffuse_albedo.view, frame->diffuse_albedo.image, color_range,
        frame->diffuse_albedo.format, frame->diffuse_albedo.width,
        frame->diffuse_albedo.height, false);
    NVSDK_NGX_Resource_VK specular_albedo_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->specular_albedo.view, frame->specular_albedo.image, color_range,
        frame->specular_albedo.format, frame->specular_albedo.width,
        frame->specular_albedo.height, false);
    NVSDK_NGX_Resource_VK normals_roughness_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->normals_roughness.view, frame->normals_roughness.image, color_range,
        frame->normals_roughness.format, frame->normals_roughness.width,
        frame->normals_roughness.height, false);
    NVSDK_NGX_Resource_VK output_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->output.view, frame->output.image, color_range, frame->output.format,
        frame->output.width, frame->output.height, true);

    NVSDK_NGX_VK_DLSSD_Eval_Params eval_params{};
    eval_params.pInDiffuseAlbedo = &diffuse_albedo_resource;
    eval_params.pInSpecularAlbedo = &specular_albedo_resource;
    eval_params.pInNormals = &normals_roughness_resource;
    eval_params.pInColor = &color_resource;
    eval_params.pInOutput = &output_resource;
    eval_params.pInDepth = &depth_resource;
    eval_params.pInMotionVectors = &motion_resource;
    eval_params.InJitterOffsetX = frame->jitter_x_pixels;
    eval_params.InJitterOffsetY = frame->jitter_y_pixels;
    eval_params.InRenderSubrectDimensions = {work_extent.width, work_extent.height};
    eval_params.InReset = (frame->reset_history || dlss5->rr_reset_pending) ? 1 : 0;
    eval_params.InMVScaleX = -static_cast<float>(work_extent.width);
    eval_params.InMVScaleY = -static_cast<float>(work_extent.height);
    eval_params.InPreExposure = 1.0f;
    eval_params.InExposureScale = 1.0f;
    eval_params.InToneMapperType = NVSDK_NGX_TONEMAPPER_STRING;

    const NVSDK_NGX_Result evaluate_result = NGX_VULKAN_EVALUATE_DLSSD_EXT(
        command_buffer, dlss5->rr_feature, dlss5->rr_parameters, &eval_params);
    if (ngx_failed(evaluate_result)) {
        dlss5->rr_reset_pending = true;
        return ngx_fail(error, error_capacity, "DLSS RR EvaluateFeature", evaluate_result);
    }
    dlss5->rr_reset_pending = false;
    return true;
}

bool doom_dlss5_resize(
    DoomDlss5 *dlss5,
    VkCommandBuffer command_buffer,
    VkExtent2D output_size,
    unsigned passes,
    char *error,
    size_t error_capacity) {
    clear_error(error, error_capacity);
    if (!dlss5 || !dlss5->snippet_initialized || !dlss5->parameters)
        return fail(error, error_capacity, "DLSSNR runtime is not initialized");
    if (command_buffer == VK_NULL_HANDLE) return fail(error, error_capacity, "command_buffer is null");
    if (!output_size.width || !output_size.height)
        return fail(error, error_capacity, "output dimensions must be nonzero");
    if (!passes || passes > 4) return fail(error, error_capacity, "NR pass count must be 1 through 4");
    if (dlss5->pass_count == passes && dlss5->output_size.width == output_size.width &&
        dlss5->output_size.height == output_size.height)
        return true;
    if (!release_feature(dlss5, error, error_capacity)) return false;

    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "CreationNodeMask", 1);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "VisibilityNodeMask", 1);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "Width", output_size.width);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "Height", output_size.height);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "OutWidth", output_size.width);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "OutHeight", output_size.height);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.Width", output_size.width);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.Height", output_size.height);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.OutputWidth", output_size.width);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.OutputHeight", output_size.height);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.Enabled", 1);
    NVSDK_NGX_Parameter_SetI(dlss5->parameters, "DLSSNR.Hint.Render.Preset", 0);

    for (unsigned pass = 0; pass < passes; ++pass) {
        const NVSDK_NGX_Result result = dlss5->create_feature(
            dlss5->device, command_buffer, NVSDK_NGX_Feature_Reserved18,
            dlss5->parameters, &dlss5->features[pass]);
        if (ngx_failed(result)) {
            dlss5->features[pass] = nullptr;
            return ngx_fail(error, error_capacity, "DLSSNR CreateFeature1(18)", result);
        }
        if (!dlss5->features[pass]) return fail(error, error_capacity, "DLSSNR returned a null feature handle");
    }
    dlss5->pass_count = passes;
    dlss5->output_size = output_size;
    return true;
}

bool doom_dlss5_evaluate(
    DoomDlss5 *dlss5,
    VkCommandBuffer command_buffer,
    const DoomDlss5Frame *frame,
    bool reset_history,
    char *error,
    size_t error_capacity) {
    clear_error(error, error_capacity);
    if (!dlss5 || !dlss5->snippet_initialized || !dlss5->pass_count)
        return fail(error, error_capacity, "DLSSNR feature is not active");
    if (command_buffer == VK_NULL_HANDLE) return fail(error, error_capacity, "command_buffer is null");
    if (!frame) return fail(error, error_capacity, "frame is null");
    if (frame->pass >= dlss5->pass_count) return fail(error, error_capacity, "NR pass is out of range");
    if (!validate_image(frame->color, VK_FORMAT_R8G8B8A8_UNORM, "color", error, error_capacity) ||
        !validate_image(frame->motion, VK_FORMAT_R16G16_SFLOAT, "motion", error, error_capacity) ||
        !validate_image(frame->depth, VK_FORMAT_D32_SFLOAT, "depth", error, error_capacity) ||
        !validate_image(frame->output, VK_FORMAT_R8G8B8A8_UNORM, "output", error, error_capacity))
        return false;
    if (frame->color.width != dlss5->output_size.width ||
        frame->color.height != dlss5->output_size.height ||
        frame->output.width != dlss5->output_size.width ||
        frame->output.height != dlss5->output_size.height)
        return fail(error, error_capacity, "color and output dimensions must match the created feature");

    const VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkImageSubresourceRange depth_range{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    NVSDK_NGX_Resource_VK color_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->color.view, frame->color.image, color_range, frame->color.format,
        frame->color.width, frame->color.height, false);
    NVSDK_NGX_Resource_VK motion_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->motion.view, frame->motion.image, color_range, frame->motion.format,
        frame->motion.width, frame->motion.height, false);
    NVSDK_NGX_Resource_VK depth_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->depth.view, frame->depth.image, depth_range, frame->depth.format,
        frame->depth.width, frame->depth.height, false);
    NVSDK_NGX_Resource_VK output_resource = NVSDK_NGX_Create_ImageView_Resource_VK(
        frame->output.view, frame->output.image, color_range, frame->output.format,
        frame->output.width, frame->output.height, true);

    NVSDK_NGX_Parameter_SetVoidPointer(dlss5->parameters, "DLSSNR.Color", &color_resource);
    NVSDK_NGX_Parameter_SetVoidPointer(dlss5->parameters, "DLSSNR.Output", &output_resource);
    NVSDK_NGX_Parameter_SetVoidPointer(dlss5->parameters, "DLSSNR.Depth", &depth_resource);
    NVSDK_NGX_Parameter_SetVoidPointer(dlss5->parameters, "DLSSNR.MVec", &motion_resource);

    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.ColorSubrectWidth", frame->color.width);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.ColorSubrectHeight", frame->color.height);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.DepthSubrectWidth", frame->depth.width);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.DepthSubrectHeight", frame->depth.height);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.MVecSubrectWidth", frame->motion.width);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.MVecSubrectHeight", frame->motion.height);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.OutputSubrectWidth", frame->output.width);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.OutputSubrectHeight", frame->output.height);
    NVSDK_NGX_Parameter_SetF(dlss5->parameters, "DLSSNR.MVecScaleX",
                              -static_cast<float>(frame->output.width));
    NVSDK_NGX_Parameter_SetF(dlss5->parameters, "DLSSNR.MVecScaleY",
                              -static_cast<float>(frame->output.height));
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.Reset",
                              (reset_history || dlss5->reset_pending[frame->pass]) ? 1 : 0);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.DepthInverted", 0);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.Enabled", 1);
    NVSDK_NGX_Parameter_SetF(dlss5->parameters, "DLSSNR.Intensity", 1.0f);
    NVSDK_NGX_Parameter_SetF(dlss5->parameters, "DLSSNR.LocalToneStrength", 1.0f);
    NVSDK_NGX_Parameter_SetF(dlss5->parameters, "DLSSNR.LocalStructureStrength", 1.6f);
    NVSDK_NGX_Parameter_SetF(dlss5->parameters, "DLSSNR.SkinStructureStrength", -1.0f);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.Style", frame->style);
    NVSDK_NGX_Parameter_SetUI(dlss5->parameters, "DLSSNR.UseAutoMask", 1);

    const NVSDK_NGX_Result result =
        dlss5->evaluate_feature(command_buffer, dlss5->features[frame->pass], dlss5->parameters, nullptr);
    if (ngx_failed(result))
        return ngx_fail(error, error_capacity, "DLSSNR EvaluateFeature", result);
    dlss5->reset_pending[frame->pass] = false;
    return true;
}

bool doom_dlss5_destroy(DoomDlss5 *dlss5, char *error, size_t error_capacity) {
    clear_error(error, error_capacity);
    if (!dlss5) return fail(error, error_capacity, "dlss5 is null");
    const bool success = shutdown(dlss5, error, error_capacity);
    delete dlss5;
    return success;
}

}
