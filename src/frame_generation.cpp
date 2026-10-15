#include "frame_generation.h"

#include "dlss5.h"

#include <nvsdk_ngx_helpers_dlssg_vk.h>
#include <nvsdk_ngx_vk.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

void clear_error(char *error, size_t capacity) {
    if (error && capacity) error[0] = '\0';
}

int fail(char *error, size_t capacity, const char *message) {
    if (error && capacity) std::snprintf(error, capacity, "%s", message);
    return 0;
}

int fail_ngx(char *error, size_t capacity, const char *operation, NVSDK_NGX_Result result) {
    if (error && capacity)
        std::snprintf(error, capacity, "%s failed with NGX result 0x%08X", operation,
                      static_cast<unsigned int>(result));
    return 0;
}

bool ngx_failed(NVSDK_NGX_Result result) {
    const uint32_t result_bits = static_cast<uint32_t>(result);
    const uint32_t failure_bits = static_cast<uint32_t>(NVSDK_NGX_Result_Fail);
    return (result_bits & 0xFFF00000u) == failure_bits;
}

bool finite_values(const float *values, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        if (!std::isfinite(values[index])) return false;
    }
    return true;
}

bool same_extent(VkExtent2D left, VkExtent2D right) {
    return left.width == right.width && left.height == right.height;
}

bool valid_image(const DoomFrameGenerationImage &image, const char *name,
                 char *error, size_t capacity) {
    if (image.image == VK_NULL_HANDLE || image.view == VK_NULL_HANDLE) {
        if (error && capacity) std::snprintf(error, capacity, "%s image or view is null", name);
        return false;
    }
    if (image.format == VK_FORMAT_UNDEFINED || image.extent.width == 0 || image.extent.height == 0) {
        if (error && capacity) std::snprintf(error, capacity, "%s format or extent is invalid", name);
        return false;
    }
    if (image.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        if (error && capacity) std::snprintf(error, capacity, "%s layout is undefined", name);
        return false;
    }
    return true;
}

NVSDK_NGX_Resource_VK make_image_resource(
    const DoomFrameGenerationImage &image,
    VkImageAspectFlags aspect,
    bool read_write) {
    NVSDK_NGX_Resource_VK resource{};
    resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    resource.ReadWrite = read_write;
    resource.Resource.ImageViewInfo.ImageView = image.view;
    resource.Resource.ImageViewInfo.Image = image.image;
    resource.Resource.ImageViewInfo.SubresourceRange = {
        aspect, 0, 1, 0, 1
    };
    resource.Resource.ImageViewInfo.Format = image.format;
    resource.Resource.ImageViewInfo.Width = image.extent.width;
    resource.Resource.ImageViewInfo.Height = image.extent.height;
    return resource;
}

void copy_matrix_to_ngx(float destination[4][4], const float source[16]) {
    std::memcpy(destination, source, sizeof(float) * 16);
}

void set_identity(float matrix[4][4]) {
    for (uint32_t axis = 0; axis < 4; ++axis) matrix[axis][axis] = 1.0f;
}

}

struct DoomFrameGeneration {
    NVSDK_NGX_Parameter *parameters = nullptr;
    NVSDK_NGX_Handle *feature = nullptr;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
};

extern "C" int doom_frame_generation_create(
    DoomFrameGeneration **out,
    char *error,
    size_t capacity) {
    clear_error(error, capacity);
    if (!out) return fail(error, capacity, "output context pointer is null");
    *out = nullptr;

    DoomFrameGeneration *created = new (std::nothrow) DoomFrameGeneration();
    if (!created) return fail(error, capacity, "could not allocate frame generation context");
    const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&created->parameters);
    if (ngx_failed(result)) {
        if (created->parameters) NVSDK_NGX_VULKAN_DestroyParameters(created->parameters);
        delete created;
        return fail_ngx(error, capacity, "NGX get capability parameters", result);
    }
    if (!created->parameters) {
        delete created;
        return fail(error, capacity, "NGX returned null capability parameters");
    }

    *out = created;
    return 1;
}

extern "C" int doom_frame_generation_resize(
    DoomFrameGeneration *context,
    VkCommandBuffer command,
    VkExtent2D extent,
    VkFormat format,
    char *error,
    size_t capacity) {
    clear_error(error, capacity);
    if (!context || !context->parameters) return fail(error, capacity, "frame generation context is not created");
    if (command == VK_NULL_HANDLE) return fail(error, capacity, "Vulkan command buffer is null");
    if (extent.width == 0 || extent.height == 0) return fail(error, capacity, "frame generation extent is empty");
    if (format == VK_FORMAT_UNDEFINED) return fail(error, capacity, "backbuffer format is undefined");
    if (context->feature && same_extent(context->extent, extent) && context->format == format) return 1;

    if (context->feature) {
        const NVSDK_NGX_Result release_result = NVSDK_NGX_VULKAN_ReleaseFeature(context->feature);
        if (ngx_failed(release_result))
            return fail_ngx(error, capacity, "NGX release frame generation feature", release_result);
        context->feature = nullptr;
        context->extent = {};
        context->format = VK_FORMAT_UNDEFINED;
    }

    NVSDK_NGX_DLSSG_Create_Params create_params{};
    create_params.Width = extent.width;
    create_params.Height = extent.height;
    create_params.NativeBackbufferFormat = static_cast<unsigned int>(format);

    doom_dlss5_begin_fg_images();
    const NVSDK_NGX_Result create_result = NGX_VK_CREATE_DLSSG(
        command, 1, 1, &context->feature, context->parameters, &create_params);
    if (ngx_failed(create_result)) {
        doom_dlss5_end_fg_images(VK_NULL_HANDLE);
        if (context->feature &&
            !ngx_failed(NVSDK_NGX_VULKAN_ReleaseFeature(context->feature)))
            context->feature = nullptr;
        return fail_ngx(error, capacity, "NGX create frame generation feature", create_result);
    }
    doom_dlss5_end_fg_images(command);

    if (!context->feature) return fail(error, capacity, "NGX created a null frame generation feature");
    context->extent = extent;
    context->format = format;
    return 1;
}

extern "C" int doom_frame_generation_evaluate(
    DoomFrameGeneration *context,
    const DoomFrameGenerationFrame *frame,
    char *error,
    size_t capacity) {
    clear_error(error, capacity);
    if (!context || !context->parameters || !context->feature)
        return fail(error, capacity, "frame generation feature is not created");
    if (!frame) return fail(error, capacity, "frame input is null");
    if (frame->command_buffer == VK_NULL_HANDLE) return fail(error, capacity, "Vulkan command buffer is null");

    if (!valid_image(frame->backbuffer, "backbuffer", error, capacity) ||
        !valid_image(frame->interpolated, "interpolated output", error, capacity) ||
        !valid_image(frame->depth, "depth", error, capacity) ||
        !valid_image(frame->motion, "motion", error, capacity) ||
        !valid_image(frame->hudless, "HUD-less", error, capacity))
        return 0;
    if (frame->backbuffer.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        frame->depth.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        frame->motion.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        frame->hudless.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        return fail(error, capacity, "NGX input images must use shader-read-only layout");
    if (frame->interpolated.layout != VK_IMAGE_LAYOUT_GENERAL)
        return fail(error, capacity, "NGX interpolated output must use general layout");
    if (!same_extent(frame->backbuffer.extent, context->extent) ||
        !same_extent(frame->interpolated.extent, context->extent))
        return fail(error, capacity, "backbuffer and interpolated output extents must match the created feature");
    if (frame->backbuffer.format != context->format || frame->interpolated.format != context->format)
        return fail(error, capacity, "backbuffer and interpolated output formats must match the created feature");
    if (!same_extent(frame->depth.extent, frame->motion.extent))
        return fail(error, capacity, "depth and motion extents must match");
    if (!same_extent(frame->hudless.extent, frame->backbuffer.extent))
        return fail(error, capacity, "HUD-less extent must match the backbuffer");
    if (!(frame->fovy > 0.0f && frame->fovy < 3.14159265f) || !(frame->aspect > 0.0f) ||
        !(frame->nearPlane > 0.0f) || !(frame->farPlane > frame->nearPlane))
        return fail(error, capacity, "camera field of view, aspect, near plane, or far plane is invalid");
    if (!finite_values(frame->viewToClip, 16) || !finite_values(frame->clipToView, 16) ||
        !finite_values(frame->clipToPrevClip, 16) || !finite_values(frame->prevClipToClip, 16) ||
        !finite_values(frame->cameraPosition, 3) || !finite_values(frame->cameraForward, 3) ||
        !finite_values(frame->cameraUp, 3) || !finite_values(frame->cameraRight, 3) ||
        !finite_values(frame->motionScale, 2) || !finite_values(frame->pixelJitter, 2))
        return fail(error, capacity, "frame contains a non-finite camera or motion value");
    if (!(frame->motionScale[0] < 0.0f) || !(frame->motionScale[1] < 0.0f))
        return fail(error, capacity, "motion scale must be negative for current-minus-previous vectors");

    NVSDK_NGX_Resource_VK backbuffer = make_image_resource(
        frame->backbuffer, VK_IMAGE_ASPECT_COLOR_BIT, false);
    NVSDK_NGX_Resource_VK depth = make_image_resource(
        frame->depth, VK_IMAGE_ASPECT_DEPTH_BIT, false);
    NVSDK_NGX_Resource_VK motion = make_image_resource(
        frame->motion, VK_IMAGE_ASPECT_COLOR_BIT, false);
    NVSDK_NGX_Resource_VK hudless = make_image_resource(
        frame->hudless, VK_IMAGE_ASPECT_COLOR_BIT, false);
    NVSDK_NGX_Resource_VK output = make_image_resource(
        frame->interpolated, VK_IMAGE_ASPECT_COLOR_BIT, true);
    NVSDK_NGX_Resource_VK disable{};
    if (frame->disable_interpolation != VK_NULL_HANDLE) {
        disable.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_BUFFER;
        disable.ReadWrite = true;
        disable.Resource.BufferInfo.Buffer = frame->disable_interpolation;
        disable.Resource.BufferInfo.SizeInBytes = 4;
    }

    NVSDK_NGX_VK_DLSSG_Eval_Params resources{};
    resources.pBackbuffer = &backbuffer;
    resources.pDepth = &depth;
    resources.pMVecs = &motion;
    resources.pHudless = &hudless;
    resources.pOutputInterpFrame = &output;
    resources.pOutputDisableInterpolation =
        frame->disable_interpolation == VK_NULL_HANDLE ? nullptr : &disable;

    NVSDK_NGX_DLSSG_Opt_Eval_Params params{};
    params.multiFrameCount = 1;
    params.multiFrameIndex = 1;
    copy_matrix_to_ngx(params.cameraViewToClip, frame->viewToClip);
    copy_matrix_to_ngx(params.clipToCameraView, frame->clipToView);
    copy_matrix_to_ngx(params.clipToPrevClip, frame->clipToPrevClip);
    copy_matrix_to_ngx(params.prevClipToClip, frame->prevClipToClip);
    set_identity(params.clipToLensClip);
    params.jitterOffset[0] = frame->pixelJitter[0] * 2.0f /
                              static_cast<float>(frame->motion.extent.width);
    params.jitterOffset[1] = frame->pixelJitter[1] * 2.0f /
                              static_cast<float>(frame->motion.extent.height);
    params.mvecScale[0] = frame->motionScale[0];
    params.mvecScale[1] = frame->motionScale[1];
    params.cameraPos[0] = frame->cameraPosition[0];
    params.cameraPos[1] = frame->cameraPosition[1];
    params.cameraPos[2] = frame->cameraPosition[2];
    params.cameraFwd[0] = frame->cameraForward[0];
    params.cameraFwd[1] = frame->cameraForward[1];
    params.cameraFwd[2] = frame->cameraForward[2];
    params.cameraUp[0] = frame->cameraUp[0];
    params.cameraUp[1] = frame->cameraUp[1];
    params.cameraUp[2] = frame->cameraUp[2];
    params.cameraRight[0] = frame->cameraRight[0];
    params.cameraRight[1] = frame->cameraRight[1];
    params.cameraRight[2] = frame->cameraRight[2];
    params.cameraNear = frame->nearPlane;
    params.cameraFar = frame->farPlane;
    params.cameraFOV = frame->fovy;
    params.cameraAspectRatio = frame->aspect;
    params.cameraMotionIncluded = true;
    params.reset = frame->reset != 0;
    params.motionVectorsInvalidValue = -65504.0f;
    params.mvecsSubrectSize = {frame->motion.extent.width, frame->motion.extent.height};
    params.depthSubrectSize = {frame->depth.extent.width, frame->depth.extent.height};
    params.hudLessSubrectSize = {frame->hudless.extent.width, frame->hudless.extent.height};
    params.backbufferSubrectSize = {frame->backbuffer.extent.width, frame->backbuffer.extent.height};
    params.outputInterpSubrectSize = {frame->interpolated.extent.width, frame->interpolated.extent.height};

    const NVSDK_NGX_Result result = NGX_VK_EVALUATE_DLSSG(
        frame->command_buffer, context->feature, context->parameters, &resources, &params);
    if (ngx_failed(result)) return fail_ngx(error, capacity, "NGX evaluate frame generation", result);
    return 1;
}

extern "C" int doom_frame_generation_destroy(
    DoomFrameGeneration *context,
    char *error,
    size_t capacity) {
    clear_error(error, capacity);
    if (!context) return 1;

    if (context->feature) {
        const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_ReleaseFeature(context->feature);
        if (ngx_failed(result))
            return fail_ngx(error, capacity, "NGX release frame generation feature", result);
        context->feature = nullptr;
    }
    if (context->parameters) {
        const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_DestroyParameters(context->parameters);
        if (ngx_failed(result))
            return fail_ngx(error, capacity, "NGX destroy frame generation parameters", result);
        context->parameters = nullptr;
    }

    delete context;
    return 1;
}
