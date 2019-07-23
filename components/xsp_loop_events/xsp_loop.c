// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_loop.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_transport_utils.h"

#include "xsp_eventfd.h"

#include "sdkconfig.h"

#define LOCK_TYPE _lock_t

#define INIT_LOCK(l) _lock_init(l)
#define DEINIT_LOCK(l) _lock_close(l)

#define LOCK(l) _lock_acquire(l)
#define UNLOCK(l) _lock_release(l)

typedef struct xsp_loop_fd_watcher {
    xsp_loop_fd_event_handler_t fd_evt_handler;
    SLIST_ENTRY(xsp_loop_fd_watcher) fd_watchers;
} xsp_loop_fd_watcher_t;

typedef struct xsp_loop {
    xsp_loop_config_t config;
    xsp_loop_event_handler_t evt_handler;

    bool is_running;
    bool should_stop;

    // Protects event_queue....
    LOCK_TYPE event_queue_lock;
    // Size is config.custom_event_data_size * config.custom_event_queue_size.
    char* event_queue;
    int event_queue_head;
    int event_queue_count;
    int event_queue_wake_fd;

    // Only accessed from the loop task.
    void* event_data_bounce_buffer;  // Size is config.custom_event_data_size.

    SLIST_HEAD(fd_watchers_head, xsp_loop_fd_watcher) fd_watchers_head;
} xsp_loop_t;

static const char TAG[] = "LOOP";

#if CONFIG_XSP_LOOP_DEFAULT_POLL_TIMEOUT_MS < 0
#error "Invalid value for CONFIG_XSP_LOOP_DEFAULT_..."
#endif

const xsp_loop_config_t xsp_loop_config_default = {
        CONFIG_XSP_LOOP_DEFAULT_POLL_TIMEOUT_MS,
        8,   // Data size. TODO(vtl): Add config.
        16,  // Queue size. TODO(vtl): Add config.
};

static bool validate_config(const xsp_loop_config_t* config) {
    if (!config)
        return true;
    if (config->poll_timeout_ms < 0)
        return false;
    if (config->custom_event_data_size < 0)
        return false;
    if (config->custom_event_queue_size < 0)
        return false;
    // TODO(vtl): Should make sure that config->custom_event_data_size *
    // config->custom_event_queue_size doesn't overflow.
    return true;
}

static void* event_queue_entry_locked(xsp_loop_handle_t loop, int raw_idx) {
    return &loop->event_queue[raw_idx * loop->config.custom_event_data_size];
}

static bool event_queue_pop_head_locked(xsp_loop_handle_t loop) {
    if (loop->event_queue_count == 0)
        return false;

    memcpy(loop->event_data_bounce_buffer, event_queue_entry_locked(loop, loop->event_queue_head),
           loop->config.custom_event_data_size);
    loop->event_queue_head = (loop->event_queue_head + 1) % loop->config.custom_event_queue_size;
    loop->event_queue_count--;
    return true;
}

static bool event_queue_push_tail_locked(xsp_loop_handle_t loop, const void* data) {
    if (loop->event_queue_count == loop->config.custom_event_queue_size)
        return false;

    int raw_idx = (loop->event_queue_head + loop->event_queue_count) %
                  loop->config.custom_event_queue_size;
    memcpy(event_queue_entry_locked(loop, raw_idx), data, loop->config.custom_event_data_size);
    loop->event_queue_count++;
    return true;
}

xsp_loop_handle_t xsp_loop_init(const xsp_loop_config_t* config,
                                const xsp_loop_event_handler_t* evt_handler) {
    if (!config)
        config = &xsp_loop_config_default;

    if (!validate_config(config)) {
        ESP_LOGE(TAG, "Invalid config");
        return NULL;
    }

    xsp_loop_handle_t loop = (xsp_loop_handle_t)calloc(1, sizeof(xsp_loop_t));
    if (!loop) {
        ESP_LOGE(TAG, "Allocation failed");
        return NULL;
    }

    loop->config = *config;
    if (evt_handler)
        loop->evt_handler = *evt_handler;

    INIT_LOCK(&loop->event_queue_lock);
    if (config->custom_event_queue_size > 0) {
        size_t size =
                (size_t)config->custom_event_data_size * (size_t)config->custom_event_queue_size;
        if (size > 0) {
            loop->event_queue = (char*)malloc(size);
            if (!loop->event_queue) {
                ESP_LOGE(TAG, "Allocation failed");
                DEINIT_LOCK(&loop->event_queue_lock);
                free(loop);
                return NULL;
            }
        }

        if (config->custom_event_data_size > 0) {
            loop->event_data_bounce_buffer = malloc((size_t)config->custom_event_data_size);
            if (!loop->event_data_bounce_buffer) {
                ESP_LOGE(TAG, "Allocation failed");
                free(loop->event_queue);
                DEINIT_LOCK(&loop->event_queue_lock);
                free(loop);
                return NULL;
            }
        }

        loop->event_queue_wake_fd = xsp_eventfd(0, XSP_EVENTFD_NONBLOCK);
        if (loop->event_queue_wake_fd == -1) {
            ESP_LOGE(TAG, "Eventfd creation failed");
            free(loop->event_data_bounce_buffer);
            free(loop->event_queue);
            DEINIT_LOCK(&loop->event_queue_lock);
            free(loop);
            return NULL;
        }
    } else {
        loop->event_queue_wake_fd = -1;
    }

    SLIST_INIT(&loop->fd_watchers_head);

    return loop;
}

bool xsp_loop_is_running(xsp_loop_handle_t loop) {
    if (!loop)
        return false;
    return loop->is_running;
}

bool xsp_loop_should_stop(xsp_loop_handle_t loop) {
    return !xsp_loop_is_running(loop) || loop->should_stop;
}

esp_err_t xsp_loop_cleanup(xsp_loop_handle_t loop) {
    if (!loop)
        return ESP_FAIL;

    xsp_loop_fd_watcher_t* fd_watcher;
    xsp_loop_fd_watcher_t* fd_watcher_temp;
    SLIST_FOREACH_SAFE(fd_watcher, &loop->fd_watchers_head, fd_watchers, fd_watcher_temp) {
        free(fd_watcher);
    }

    if (loop->event_queue_wake_fd != -1)
        close(loop->event_queue_wake_fd);
    free(loop->event_data_bounce_buffer);
    free(loop->event_queue);
    DEINIT_LOCK(&loop->event_queue_lock);
    free(loop);
    return ESP_OK;
}

esp_err_t xsp_loop_post_custom_event(xsp_loop_handle_t loop, const void* data) {
    LOCK(&loop->event_queue_lock);

    if (!event_queue_push_tail_locked(loop, data)) {
        UNLOCK(&loop->event_queue_lock);
        return ESP_FAIL;
    }

    uint64_t inc = 1;
    int result = write(loop->event_queue_wake_fd, &inc, sizeof(inc));
    assert(result == 8);

    UNLOCK(&loop->event_queue_lock);
    return ESP_OK;
}

// Returns true if we should continue.
static bool do_loop_iteration(xsp_loop_handle_t loop) {
    if (loop->should_stop)
        return false;

    bool did_something = false;

    fd_set write_fds;
    fd_set read_fds;
    FD_ZERO(&write_fds);
    FD_ZERO(&read_fds);

    // First, send notifications that we *will* call select().
    int max_fd = -1;
    xsp_loop_fd_watcher_t* fd_watcher;
    SLIST_FOREACH(fd_watcher, &loop->fd_watchers_head, fd_watchers) {
        xsp_loop_fd_event_handler_t* feh = &fd_watcher->fd_evt_handler;
        xsp_loop_fd_watch_for_t watch_for = XSP_LOOP_FD_WATCH_FOR_NONE;
        if (feh->on_loop_will_select) {
            watch_for = feh->on_loop_will_select(loop, feh->ctx, feh->fd);
            if (loop->should_stop)
                return false;
        } else {
            if (feh->on_loop_can_write_fd)
                watch_for |= XSP_LOOP_FD_WATCH_FOR_WRITE;
            if (feh->on_loop_can_read_fd)
                watch_for |= XSP_LOOP_FD_WATCH_FOR_READ;
        }

        if (watch_for) {
            if (feh->fd > max_fd)
                max_fd = feh->fd;
            if ((watch_for & XSP_LOOP_FD_WATCH_FOR_WRITE))
                FD_SET(feh->fd, &write_fds);
            if ((watch_for & XSP_LOOP_FD_WATCH_FOR_READ))
                FD_SET(feh->fd, &read_fds);
        }
    }

    // TODO(vtl): We shouldn't have to do this conversion each iteration.
    struct timeval timeout;
    esp_transport_utils_ms_to_timeval(loop->config.poll_timeout_ms, &timeout);
    // TODO(vtl): Possibly, we should check for error (-1) vs timeout (0).
    if (select(max_fd + 1, &read_fds, &write_fds, NULL, &timeout) > 0) {
        xsp_loop_fd_watcher_t* fd_watcher;
        SLIST_FOREACH(fd_watcher, &loop->fd_watchers_head, fd_watchers) {
            xsp_loop_fd_event_handler_t* feh = &fd_watcher->fd_evt_handler;

            if (FD_ISSET(feh->fd, &write_fds)) {
                feh->on_loop_can_write_fd(loop, feh->ctx, feh->fd);
                if (loop->should_stop)
                    return false;
            }
            if (FD_ISSET(feh->fd, &read_fds)) {
                feh->on_loop_can_read_fd(loop, feh->ctx, feh->fd);
                if (loop->should_stop)
                    return false;
            }
        }
        did_something = true;
    }
    if (loop->should_stop)
        return false;

    // Do idle if nothing happened.
    if (!did_something) {
        if (loop->evt_handler.on_loop_idle)
            loop->evt_handler.on_loop_idle(loop, loop->evt_handler.ctx);
        if (loop->should_stop)
            return false;
    }

    return true;
}

esp_err_t xsp_loop_run(xsp_loop_handle_t loop) {
    if (!loop)
        return ESP_ERR_INVALID_ARG;
    if (loop->is_running)
        return ESP_ERR_INVALID_STATE;

    loop->is_running = true;
    loop->should_stop = false;
    if (loop->evt_handler.on_loop_start)
        loop->evt_handler.on_loop_start(loop, loop->evt_handler.ctx);
    while (do_loop_iteration(loop))
        ;  // Nothing.
    if (loop->evt_handler.on_loop_stop)
        loop->evt_handler.on_loop_stop(loop, loop->evt_handler.ctx);
    loop->is_running = false;

    return ESP_OK;
}

esp_err_t xsp_loop_stop(xsp_loop_handle_t loop) {
    if (!loop)
        return ESP_ERR_INVALID_ARG;
    if (!loop->is_running)
        return ESP_ERR_INVALID_STATE;

    loop->should_stop = true;
    return ESP_OK;
}

xsp_loop_fd_watcher_handle_t xsp_loop_add_fd_watcher(
        xsp_loop_handle_t loop,
        const xsp_loop_fd_event_handler_t* fd_evt_handler) {
    if (!loop || !fd_evt_handler ||
        (!fd_evt_handler->on_loop_can_read_fd && !fd_evt_handler->on_loop_can_write_fd) ||
        (fd_evt_handler->fd < 0 || fd_evt_handler->fd >= FD_SETSIZE)) {
        ESP_LOGE(TAG, "Invalid argument");
        return NULL;
    }

    xsp_loop_fd_watcher_handle_t fd_watcher =
            (xsp_loop_fd_watcher_handle_t)malloc(sizeof(xsp_loop_fd_watcher_t));
    if (!fd_watcher) {
        ESP_LOGE(TAG, "Allocation failed");
        return NULL;
    }
    fd_watcher->fd_evt_handler = *fd_evt_handler;
    SLIST_INSERT_HEAD(&loop->fd_watchers_head, fd_watcher, fd_watchers);
    return fd_watcher;
}

esp_err_t xsp_loop_remove_fd_watcher(xsp_loop_handle_t loop,
                                     xsp_loop_fd_watcher_handle_t fd_watcher) {
    if (!loop || !fd_watcher)
        return ESP_ERR_INVALID_ARG;
    SLIST_REMOVE(&loop->fd_watchers_head, fd_watcher, xsp_loop_fd_watcher, fd_watchers);
    free(fd_watcher);
    return ESP_OK;
}
