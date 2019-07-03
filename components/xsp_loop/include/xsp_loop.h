// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_LOOP_H_
#define XSP_LOOP_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xsp_loop_config {
    int poll_timeout_ms;
} xsp_loop_config_t;

typedef struct xsp_loop* xsp_loop_handle_t;

typedef void (*on_loop_start_func_t)(xsp_loop_handle_t loop, void* ctx);
typedef void (*on_loop_stop_func_t)(xsp_loop_handle_t loop, void* ctx);
typedef void (*on_loop_idle_func_t)(xsp_loop_handle_t loop, void* ctx);

typedef struct xsp_loop_event_handler {
    on_loop_start_func_t on_loop_start;
    on_loop_stop_func_t on_loop_stop;
    on_loop_idle_func_t on_loop_idle;

    void* ctx;
} xsp_loop_event_handler_t;

typedef void (*on_loop_will_select_func_t)(xsp_loop_handle_t loop, void* ctx, int fd);
typedef void (*on_loop_can_read_fd_func_t)(xsp_loop_handle_t loop, void* ctx, int fd);
typedef void (*on_loop_can_write_fd_func_t)(xsp_loop_handle_t loop, void* ctx, int fd);

typedef struct xsp_loop_fd_event_handler {
    on_loop_will_select_func_t on_loop_will_select;
    on_loop_can_read_fd_func_t on_loop_can_read_fd;
    on_loop_can_write_fd_func_t on_loop_can_write_fd;

    void* ctx;
    int fd;
} xsp_loop_fd_event_handler_t;

typedef struct xsp_loop_fd_watcher* xsp_loop_fd_watcher_handle_t;

// Default configuration.
extern const xsp_loop_config_t xsp_loop_config_default;

// Initializes loop.
xsp_loop_handle_t xsp_loop_init(const xsp_loop_config_t* config,
                                const xsp_loop_event_handler_t* evt_handler);

// Cleans up (shuts down) the loop, which must not be running.
esp_err_t xsp_loop_cleanup(xsp_loop_handle_t loop);

// Runs the loop. The loop will run until `xsp_loop_stop()` is called.
esp_err_t xsp_loop_run(xsp_loop_handle_t loop);

// Stops the loop as soon as possible. Should only be called from "inside" the loop, i.e., inside an
// event handler.
esp_err_t xsp_loop_stop(xsp_loop_handle_t loop);

// TODO(vtl)
xsp_loop_fd_watcher_handle_t xsp_loop_add_fd_watcher(
        const xsp_loop_fd_event_handler_t* fd_evt_handler);

// TODO(vtl)
esp_err_t xsp_loop_remove_fd_watcher(xsp_loop_fd_watcher_handle_t fd_watcher);

#endif  // XSP_WS_CLIENT_LOOP_H_
