#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

typedef struct Presentation Presentation;

typedef struct PresentationFrame {
    VkSwapchainKHR swapchain;
    uint32_t count;
    uint32_t image_indices[2];
    VkSemaphore wait_semaphores[2];
    double frame_seconds;
} PresentationFrame;

typedef struct PresentationStats {
    uint64_t real_frames;
    uint64_t generated_frames;
    VkResult result;
} PresentationStats;

Presentation *presentation_create(VkQueue queue, char *error, size_t cap);
VkResult presentation_acquire(Presentation *presentation, VkDevice device, VkSwapchainKHR swapchain,
                              VkSemaphore semaphore, uint32_t *image_index);
VkResult presentation_submit(Presentation *presentation, const PresentationFrame *frame);
VkResult presentation_flush(Presentation *presentation);
void presentation_reset(Presentation *presentation);
PresentationStats presentation_stats(Presentation *presentation);
void presentation_destroy(Presentation *presentation);
