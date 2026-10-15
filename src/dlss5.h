#ifndef DOOM_DLSS5_H
#define DOOM_DLSS5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DoomDlss5 DoomDlss5;

void doom_dlss5_begin_fg_images(void);
void doom_dlss5_end_fg_images(VkCommandBuffer command);

typedef enum DoomDlss5SRMode {
    DOOM_DLSS5_SR_OFF = 0,
    DOOM_DLSS5_SR_QUALITY = 1,
    DOOM_DLSS5_SR_BALANCED = 2,
    DOOM_DLSS5_SR_PERFORMANCE = 3
} DoomDlss5SRMode;

typedef struct DoomDlss5Image {
    VkImage image;
    VkImageView view;
    VkFormat format;
    uint32_t width;
    uint32_t height;
    VkImageLayout layout;
} DoomDlss5Image;

typedef struct DoomDlss5Frame {
    DoomDlss5Image color;
    DoomDlss5Image motion;
    DoomDlss5Image depth;
    DoomDlss5Image output;
    uint32_t style;
    uint32_t pass;
} DoomDlss5Frame;

typedef struct DoomDlss5InitInfo {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    const wchar_t *application_data_path;
} DoomDlss5InitInfo;

bool doom_dlss5_get_instance_extensions(
    const wchar_t *application_data_path,
    VkExtensionProperties *extensions,
    uint32_t extension_capacity,
    uint32_t *extension_count,
    char *error,
    size_t error_capacity);

bool doom_dlss5_get_device_extensions(
    const wchar_t *application_data_path,
    VkInstance instance,
    VkPhysicalDevice physical_device,
    VkExtensionProperties *extensions,
    uint32_t extension_capacity,
    uint32_t *extension_count,
    char *error,
    size_t error_capacity);

bool doom_dlss5_create(
    const DoomDlss5InitInfo *info,
    DoomDlss5 **out_dlss5,
    char *error,
    size_t error_capacity);
bool doom_dlss5_render_size(
    DoomDlss5 *dlss5,
    DoomDlss5SRMode mode,
    VkExtent2D output_extent,
    VkExtent2D *work_extent,
    char *error,
    size_t error_capacity);
bool doom_dlss5_upscale(
    DoomDlss5 *dlss5,
    VkCommandBuffer command_buffer,
    const DoomDlss5Frame *frame,
    DoomDlss5SRMode mode,
    float jitter_x_pixels,
    float jitter_y_pixels,
    bool reset_history,
    char *error,
    size_t error_capacity);
bool doom_dlss5_resize(
    DoomDlss5 *dlss5,
    VkCommandBuffer command_buffer,
    VkExtent2D output_size,
    unsigned passes,
    char *error,
    size_t error_capacity);
bool doom_dlss5_evaluate(
    DoomDlss5 *dlss5,
    VkCommandBuffer command_buffer,
    const DoomDlss5Frame *frame,
    bool reset_history,
    char *error,
    size_t error_capacity);
bool doom_dlss5_destroy(DoomDlss5 *dlss5, char *error, size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif
