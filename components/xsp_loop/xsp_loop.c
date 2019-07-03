// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_loop.h"

#include <stdbool.h>
#include <stdlib.h>

#include "esp_log.h"

#include "sdkconfig.h"

typedef struct xsp_loop {
    xsp_loop_config_t config;
    xsp_loop_event_handler_t evt_handler;

    bool is_running;
    bool should_stop;
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

    return loop;
}

esp_err_t xsp_loop_cleanup(xsp_loop_handle_t loop) {
    if (!loop)
        return ESP_FAIL;

    free(loop);
    return ESP_OK;
}

// Returns true if we should continue.
static bool do_loop_iteration(xsp_loop_handle_t loop) {
    if (loop->should_stop)
        return false;

    bool did_something = false;

#if 0
    // Handle pre-buffered data first, as a special case.
    if (xsp_ws_client_has_buffered_read_data(loop->client)) {
        do_read(loop);
        did_something = true;
    }

    // TODO(vtl): We don't need to get the FD each iteration.
    int fd = xsp_ws_client_get_select_fd(loop->client);
    if (fd < 0)
        return false;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    fd_set write_fds;
    FD_ZERO(&write_fds);
    if (loop->sending_message)
        FD_SET(fd, &write_fds);

    // TODO(vtl): We shouldn't have to do this conversion each iteration.
    struct timeval timeout;
    esp_transport_utils_ms_to_timeval(loop->config.poll_timeout_ms, &timeout);
    // TODO(vtl): Possibly, we should check for error (-1) vs timeout (0).
    if (select(fd + 1, &read_fds, &write_fds, NULL, &timeout) > 0) {
        if (loop->sending_message && FD_ISSET(fd, &write_fds)) {
            do_write(loop);
            // Keep on writing while we need to and can.
            while (loop->sending_message && select(fd + 1, NULL, &write_fds, NULL, &timeout) > 0)
                do_write(loop);
            did_something = true;
            if (should_stop(loop))
                return false;
        }
        if (FD_ISSET(fd, &read_fds)) {
            do_read(loop);
            did_something = true;
        }
    }
    if (should_stop(loop))
        return false;
#endif

    // Do idle if nothing happened.
    if (!did_something) {
        if (loop->evt_handler.on_loop_idle)
            loop->evt_handler.on_loop_idle(loop, loop->evt_handler.ctx);
    }
    if (loop->should_stop)
        return false;

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
        const xsp_loop_fd_event_handler_t* fd_evt_handler) {
    // TODO(vtl)
    return NULL;
}

esp_err_t xsp_loop_remove_fd_watcher(xsp_loop_fd_watcher_handle_t fd_watcher) {
    // TODO(vtl)
    return ESP_FAIL;
}

//FIXME
#if 0
#include <sys/select.h>
#include <sys/time.h>

#include "esp_transport_utils.h"
#endif
