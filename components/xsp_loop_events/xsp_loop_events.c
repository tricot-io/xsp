// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_loop_events.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/lock.h>
#include <unistd.h>

#include "esp_log.h"

#include "xsp_eventfd.h"

#include "sdkconfig.h"

#define LOCK_TYPE _lock_t

#define INIT_LOCK(l) _lock_init(l)
#define DEINIT_LOCK(l) _lock_close(l)

#define LOCK(l) _lock_acquire(l)
#define UNLOCK(l) _lock_release(l)

typedef struct xsp_loop_events {
    xsp_loop_events_config_t config;
    xsp_loop_events_event_handler_t evt_handler;
    xsp_loop_handle_t loop;

    xsp_loop_fd_watcher_handle_t fd_watcher;

    // Protects `queue`....
    LOCK_TYPE queue_lock;
    // Size is config.data_size * config.queue_size.
    char* queue;
    int queue_head;
    int queue_count;
    int wake_fd;

    // Only accessed from the loop task.
    void* event_data_bounce_buffer;  // Size is config.data_size.
} xsp_loop_events_t;

static const char TAG[] = "LOOP_EVTS";

const xsp_loop_events_config_t xsp_loop_events_config_default = {
        8,   // Data size. TODO(vtl): Add config.
        16,  // Queue size. TODO(vtl): Add config.
};

static bool validate_config(const xsp_loop_events_config_t* config) {
    if (!config)
        return true;
    if (config->data_size < 0)
        return false;
    if (config->queue_size <= 0)
        return false;
    // TODO(vtl): Should make sure that config->data_size * config->queue_size doesn't overflow.
    return true;
}

static void* event_queue_entry_locked(xsp_loop_events_handle_t loop_events, int raw_idx) {
    return &loop_events->queue[raw_idx * loop_events->config.data_size];
}

static bool event_queue_pop_head_locked(xsp_loop_events_handle_t loop_events) {
    if (loop_events->queue_count == 0)
        return false;

    memcpy(loop_events->event_data_bounce_buffer,
           event_queue_entry_locked(loop_events, loop_events->queue_head),
           loop_events->config.data_size);
    loop_events->queue_head = (loop_events->queue_head + 1) % loop_events->config.queue_size;
    loop_events->queue_count--;
    return true;
}

static bool event_queue_push_tail_locked(xsp_loop_events_handle_t loop_events, const void* data) {
    if (loop_events->queue_count == loop_events->config.queue_size)
        return false;

    int raw_idx =
            (loop_events->queue_head + loop_events->queue_count) % loop_events->config.queue_size;
    memcpy(event_queue_entry_locked(loop_events, raw_idx), data, loop_events->config.data_size);
    loop_events->queue_count++;
    return true;
}

static void on_loop_can_read_fd(xsp_loop_handle_t loop, void* ctx, int fd) {
    xsp_loop_events_handle_t loop_events = (xsp_loop_events_handle_t)ctx;
    LOCK(&loop_events->queue_lock);

    // Only process the current number of events to prevent starvation.
    int count = loop_events->queue_count;
    assert(count > 0);

    // Reset the wake FD.
    uint64_t unused = 0;
    int result = read(loop_events->wake_fd, &unused, sizeof(unused));
    assert(result == 8);
    assert((uint64_t)count == unused);

    for (;;) {
        bool success = event_queue_pop_head_locked(loop_events);
        assert(success);

        UNLOCK(&loop_events->queue_lock);
        loop_events->evt_handler.on_loop_events_event(loop_events, loop_events->evt_handler.ctx,
                                                      loop_events->event_data_bounce_buffer);
        count--;
        if (count == 0 || xsp_loop_should_stop(loop_events->loop))
            break;
        LOCK(&loop_events->queue_lock);
    }
}

xsp_loop_events_handle_t xsp_loop_events_init(const xsp_loop_events_config_t* config,
                                              const xsp_loop_events_event_handler_t* evt_handler,
                                              xsp_loop_handle_t loop) {
    if (!config)
        config = &xsp_loop_events_config_default;

    if (!validate_config(config)) {
        ESP_LOGE(TAG, "Invalid config");
        return NULL;
    }

    if (!evt_handler || !loop) {
        ESP_LOGE(TAG, "Invalid argument");
        return NULL;
    }

    xsp_loop_events_handle_t loop_events =
            (xsp_loop_events_handle_t)calloc(1, sizeof(xsp_loop_events_t));
    if (!loop_events) {
        ESP_LOGE(TAG, "Allocation failed");
        return NULL;
    }
    INIT_LOCK(&loop_events->queue_lock);
    loop_events->wake_fd = -1;

    loop_events->config = *config;
    loop_events->evt_handler = *evt_handler;
    loop_events->loop = loop;

    size_t size = (size_t)config->data_size * (size_t)config->queue_size;
    if (size > 0) {
        loop_events->queue = (char*)malloc(size);
        if (!loop_events->queue) {
            ESP_LOGE(TAG, "Allocation failed");
            goto fail;
        }
    }

    if (config->data_size > 0) {
        loop_events->event_data_bounce_buffer = malloc((size_t)config->data_size);
        if (!loop_events->event_data_bounce_buffer) {
            ESP_LOGE(TAG, "Allocation failed");
            goto fail;
        }
    }

    loop_events->wake_fd = xsp_eventfd(0, XSP_EVENTFD_NONBLOCK);
    if (loop_events->wake_fd == -1) {
        ESP_LOGE(TAG, "Eventfd creation failed");
        goto fail;
    }

    xsp_loop_fd_event_handler_t loop_fd_event_handler = {
            NULL, NULL, on_loop_can_read_fd, loop_events, loop_events->wake_fd,
    };
    loop_events->fd_watcher = xsp_loop_add_fd_watcher(loop, &loop_fd_event_handler);
    if (!loop_events->fd_watcher) {
        ESP_LOGE(TAG, "Failed to watch FD");
        goto fail;
    }

    return loop_events;

fail:
    if (loop_events->wake_fd != -1)
        close(loop_events->wake_fd);
    free(loop_events->event_data_bounce_buffer);
    free(loop_events->queue);
    DEINIT_LOCK(&loop_events->queue_lock);
    free(loop_events);
    return NULL;
}

esp_err_t xsp_loop_events_cleanup(xsp_loop_events_handle_t loop_events) {
    if (!loop_events)
        return ESP_FAIL;

    if (loop_events->queue_count > 0)
        ESP_LOGW(TAG, "Cleaning up with %d undispatched events", loop_events->queue_count);

    if (loop_events->wake_fd != -1)
        close(loop_events->wake_fd);
    free(loop_events->event_data_bounce_buffer);
    free(loop_events->queue);
    DEINIT_LOCK(&loop_events->queue_lock);
    free(loop_events);
    return ESP_OK;
}

esp_err_t xsp_loop_events_post_event(xsp_loop_events_handle_t loop_events, const void* data) {
    LOCK(&loop_events->queue_lock);

    if (!event_queue_push_tail_locked(loop_events, data)) {
        UNLOCK(&loop_events->queue_lock);
        return ESP_FAIL;
    }

    uint64_t inc = 1;
    int result = write(loop_events->wake_fd, &inc, sizeof(inc));
    assert(result == 8);

    UNLOCK(&loop_events->queue_lock);
    return ESP_OK;
}
