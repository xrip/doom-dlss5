#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "presentation.h"

#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <windows.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

struct Presentation {
    VkQueue queue;
    HANDLE thread;
    HANDLE timer;
    HANDLE wake_event;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE condition;
    PresentationFrame pending;
    PresentationStats stats;
    bool has_pending;
    bool active;
    bool draining;
    bool stopping;
};

static void record_result_locked(Presentation *presentation, VkResult result) {
    if (result != VK_SUCCESS) presentation->stats.result = result;
}

static void set_creation_error(char *error, size_t cap, const char *operation, DWORD code) {
    if (error && cap) snprintf(error, cap, "%s failed (Win32 error %lu)", operation, (unsigned long)code);
}

static void record_worker_error(Presentation *presentation) {
    EnterCriticalSection(&presentation->lock);
    record_result_locked(presentation, VK_ERROR_UNKNOWN);
    LeaveCriticalSection(&presentation->lock);
}

static void wait_for_second_present(Presentation *presentation, double frame_seconds) {
    LARGE_INTEGER due_time;
    due_time.QuadPart = -(LONGLONG)(frame_seconds * 5000000.0);
    if (!SetWaitableTimer(presentation->timer, &due_time, 0, NULL, NULL, FALSE)) {
        record_worker_error(presentation);
        return;
    }

    HANDLE handles[] = {presentation->wake_event, presentation->timer};
    DWORD wait_result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    CancelWaitableTimer(presentation->timer);
    if (wait_result != WAIT_OBJECT_0 && wait_result != WAIT_OBJECT_0 + 1)
        record_worker_error(presentation);
}

static VkResult present_frame(Presentation *presentation, const PresentationFrame *frame, uint32_t index) {
    VkPresentInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->wait_semaphores[index],
        .swapchainCount = 1,
        .pSwapchains = &frame->swapchain,
        .pImageIndices = &frame->image_indices[index]
    };
    EnterCriticalSection(&presentation->lock);
    VkResult result = vkQueuePresentKHR(presentation->queue, &info);
    record_result_locked(presentation, result);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        if (frame->count == 2 && index == 0) ++presentation->stats.generated_frames;
        else ++presentation->stats.real_frames;
    }
    WakeAllConditionVariable(&presentation->condition);
    LeaveCriticalSection(&presentation->lock);
    return result;
}

static void present_job(Presentation *presentation, const PresentationFrame *frame) {
    for (uint32_t index = 0; index < frame->count; ++index) {
        if (frame->count == 2 && index == 1)
            wait_for_second_present(presentation, frame->frame_seconds);

        VkResult result = present_frame(presentation, frame, index);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) break;
    }
}

static DWORD WINAPI presentation_worker(void *parameter) {
    Presentation *presentation = parameter;

    for (;;) {
        EnterCriticalSection(&presentation->lock);
        while (!presentation->has_pending && !presentation->stopping) {
            if (!SleepConditionVariableCS(&presentation->condition, &presentation->lock, INFINITE)) {
                record_result_locked(presentation, VK_ERROR_UNKNOWN);
                presentation->stopping = true;
                SetEvent(presentation->wake_event);
                WakeAllConditionVariable(&presentation->condition);
                break;
            }
        }

        if (presentation->stopping && !presentation->has_pending) {
            WakeAllConditionVariable(&presentation->condition);
            LeaveCriticalSection(&presentation->lock);
            return 0;
        }

        PresentationFrame frame = presentation->pending;
        presentation->has_pending = false;
        presentation->active = true;
        WakeAllConditionVariable(&presentation->condition);
        LeaveCriticalSection(&presentation->lock);

        present_job(presentation, &frame);

        EnterCriticalSection(&presentation->lock);
        presentation->active = false;
        WakeAllConditionVariable(&presentation->condition);
        LeaveCriticalSection(&presentation->lock);
    }
}

Presentation *presentation_create(VkQueue queue, char *error, size_t cap) {
    if (error && cap) error[0] = '\0';

    Presentation *presentation = calloc(1, sizeof(*presentation));
    if (!presentation) {
        if (error && cap) snprintf(error, cap, "out of memory");
        return NULL;
    }
    presentation->queue = queue;
    presentation->stats.result = VK_SUCCESS;

    bool lock_initialized = false;
    if (!InitializeCriticalSectionEx(&presentation->lock, 0, 0)) goto fail;
    lock_initialized = true;
    InitializeConditionVariable(&presentation->condition);

    presentation->timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                  TIMER_ALL_ACCESS);
    if (!presentation->timer) goto fail;
    presentation->wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!presentation->wake_event) goto fail;

    presentation->thread = CreateThread(NULL, 0, presentation_worker, presentation, 0, NULL);
    if (!presentation->thread) goto fail;
    return presentation;

fail: {
        DWORD code = GetLastError();
        set_creation_error(error, cap, "presentation initialization", code);
        if (presentation->wake_event) CloseHandle(presentation->wake_event);
        if (presentation->timer) CloseHandle(presentation->timer);
        if (lock_initialized) DeleteCriticalSection(&presentation->lock);
        free(presentation);
        return NULL;
    }
}

VkResult presentation_acquire(Presentation *presentation, VkDevice device, VkSwapchainKHR swapchain,
                              VkSemaphore semaphore, uint32_t *image_index) {
    if (!presentation || !device || !swapchain || !semaphore || !image_index)
        return VK_ERROR_INITIALIZATION_FAILED;
    ULONGLONG deadline = GetTickCount64() + 250;
    EnterCriticalSection(&presentation->lock);
    VkResult result;
    for (;;) {
        result = vkAcquireNextImageKHR(device, swapchain, 0, semaphore, VK_NULL_HANDLE, image_index);
        if (result != VK_NOT_READY && result != VK_TIMEOUT) break;
        if (GetTickCount64() >= deadline) { result = VK_TIMEOUT; break; }
        if (!SleepConditionVariableCS(&presentation->condition, &presentation->lock, 2) && GetLastError() != ERROR_TIMEOUT) {
            result = VK_ERROR_UNKNOWN;
            break;
        }
    }
    LeaveCriticalSection(&presentation->lock);
    return result;
}

VkResult presentation_submit(Presentation *presentation, const PresentationFrame *frame) {
    if (!presentation || !frame) return VK_ERROR_INITIALIZATION_FAILED;
    if (!frame->swapchain || frame->count < 1 || frame->count > 2 ||
        !isfinite(frame->frame_seconds) || frame->frame_seconds <= 0 || frame->frame_seconds > 1)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t index = 0; index < frame->count; ++index)
        if (!frame->wait_semaphores[index]) return VK_ERROR_INITIALIZATION_FAILED;

    EnterCriticalSection(&presentation->lock);
    while ((presentation->has_pending || presentation->draining) && !presentation->stopping) {
        if (!SleepConditionVariableCS(&presentation->condition, &presentation->lock, INFINITE)) {
            record_result_locked(presentation, VK_ERROR_UNKNOWN);
            presentation->stopping = true;
            SetEvent(presentation->wake_event);
            WakeAllConditionVariable(&presentation->condition);
            break;
        }
    }

    VkResult prior_status = presentation->stats.result;
    if (!presentation->stopping && (prior_status == VK_SUCCESS || prior_status == VK_SUBOPTIMAL_KHR)) {
        presentation->pending = *frame;
        presentation->has_pending = true;
        WakeConditionVariable(&presentation->condition);
    } else if (presentation->stopping && prior_status == VK_SUCCESS) {
        prior_status = VK_ERROR_INITIALIZATION_FAILED;
    }
    LeaveCriticalSection(&presentation->lock);
    return prior_status;
}

VkResult presentation_flush(Presentation *presentation) {
    if (!presentation) return VK_ERROR_INITIALIZATION_FAILED;

    EnterCriticalSection(&presentation->lock);
    while (presentation->draining) {
        if (!SleepConditionVariableCS(&presentation->condition, &presentation->lock, INFINITE)) {
            record_result_locked(presentation, VK_ERROR_UNKNOWN);
            LeaveCriticalSection(&presentation->lock);
            return VK_ERROR_UNKNOWN;
        }
    }
    presentation->draining = true;
    SetEvent(presentation->wake_event);
    while (presentation->has_pending || presentation->active) {
        if (!SleepConditionVariableCS(&presentation->condition, &presentation->lock, INFINITE)) {
            record_result_locked(presentation, VK_ERROR_UNKNOWN);
            LeaveCriticalSection(&presentation->lock);
            return VK_ERROR_UNKNOWN;
        }
    }
    LeaveCriticalSection(&presentation->lock);

    VkResult idle_result = vkQueueWaitIdle(presentation->queue);

    EnterCriticalSection(&presentation->lock);
    record_result_locked(presentation, idle_result);
    WaitForSingleObject(presentation->wake_event, 0);
    presentation->draining = false;
    WakeAllConditionVariable(&presentation->condition);
    VkResult result = presentation->stats.result;
    LeaveCriticalSection(&presentation->lock);
    return result;
}

void presentation_reset(Presentation *presentation) {
    if (!presentation) return;

    EnterCriticalSection(&presentation->lock);
    if (presentation->stats.result == VK_ERROR_OUT_OF_DATE_KHR ||
        presentation->stats.result == VK_SUBOPTIMAL_KHR)
        presentation->stats.result = VK_SUCCESS;
    LeaveCriticalSection(&presentation->lock);
}

PresentationStats presentation_stats(Presentation *presentation) {
    if (!presentation) return (PresentationStats){.result = VK_ERROR_INITIALIZATION_FAILED};

    EnterCriticalSection(&presentation->lock);
    PresentationStats stats = presentation->stats;
    LeaveCriticalSection(&presentation->lock);
    return stats;
}

void presentation_destroy(Presentation *presentation) {
    if (!presentation) return;

    presentation_flush(presentation);

    EnterCriticalSection(&presentation->lock);
    presentation->stopping = true;
    SetEvent(presentation->wake_event);
    WakeAllConditionVariable(&presentation->condition);
    LeaveCriticalSection(&presentation->lock);

    WaitForSingleObject(presentation->thread, INFINITE);
    CloseHandle(presentation->thread);
    CloseHandle(presentation->wake_event);
    CloseHandle(presentation->timer);
    DeleteCriticalSection(&presentation->lock);
    free(presentation);
}
