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

/*
typedef struct xsp_loop_fd_watcher {
    on_loop_will_select_func_t o
} xsp_loop_fd_watcher_t;
*/

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

//FIXME
#if 0

// Schedules the given message to be sent. On success (of scheduling),
// `XSP_WS_CLIENT_LOOP_EVENT_MESSAGE_SENT` will be generated upon completion of the send and
// `message` must remain valid until that event (or until loop shutdown). Should only be called from
// "inside" the loop, i.e., inside the event handler.
// TODO(vtl): Possibly this should try to send the first frame immediately.
esp_err_t xsp_ws_client_loop_send_message(xsp_ws_client_loop_handle_t loop,
                                          bool binary,
                                          int message_size,
                                          const void* message);

// Closes the connection. Note that this sends a close message (if possible) and stops the loop; it
// does not close the underlying client. Should only be called from "inside" the loop, i.e., inside
// the event handler.
esp_err_t xsp_ws_client_loop_close(xsp_ws_client_loop_handle_t loop, int close_status);

// Sends a ping.
esp_err_t xsp_ws_client_loop_ping(xsp_ws_client_loop_handle_t loop,
                                  int payload_size,
                                  const void* payload);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif

#endif  // XSP_WS_CLIENT_LOOP_H_
