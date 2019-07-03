// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_loop.h"

#include <stdbool.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/select.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_transport_utils.h"

#include "sdkconfig.h"

typedef struct xsp_loop_fd_watcher {
    xsp_loop_fd_event_handler_t fd_evt_handler;
    SLIST_ENTRY(xsp_loop_fd_watcher) fd_watchers;
} xsp_loop_fd_watcher_t;

typedef struct xsp_loop {
    xsp_loop_config_t config;
    xsp_loop_event_handler_t evt_handler;

    bool is_running;
    bool should_stop;

    SLIST_HEAD(fd_watchers_head, xsp_loop_fd_watcher) fd_watchers_head;
} xsp_loop_t;

static const char TAG[] = "LOOP";

#if CONFIG_XSP_LOOP_DEFAULT_POLL_TIMEOUT_MS < 0
#error "Invalid value for CONFIG_XSP_LOOP_DEFAULT_..."
#endif

const xsp_loop_config_t xsp_loop_config_default = {CONFIG_XSP_LOOP_DEFAULT_POLL_TIMEOUT_MS};

static bool validate_config(const xsp_loop_config_t* config) {
    if (!config)
        return true;
    if (config->poll_timeout_ms < 0)
        return false;
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

    SLIST_INIT(&loop->fd_watchers_head);

    return loop;
}

esp_err_t xsp_loop_cleanup(xsp_loop_handle_t loop) {
    if (!loop)
        return ESP_FAIL;

    xsp_loop_fd_watcher_t* fd_watcher;
    xsp_loop_fd_watcher_t* fd_watcher_temp;
    SLIST_FOREACH_SAFE(fd_watcher, &loop->fd_watchers_head, fd_watchers, fd_watcher_temp) {
        free(fd_watcher);
    }

    free(loop);
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
