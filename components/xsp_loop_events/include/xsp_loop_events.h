// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_LOOP_EVENTS_H_
#define XSP_LOOP_EVENTS_H_

#include "esp_err.h"

#include "xsp_loop.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xsp_loop_events_config {
    int data_size;
    int queue_size;
} xsp_loop_events_config_t;

typedef struct xsp_loop_events* xsp_loop_events_handle_t;

typedef void (*on_loop_events_event_func_t)(xsp_loop_events_handle_t loop_events,
                                            void* ctx,
                                            void* data);

typedef struct xsp_loop_events_event_handler {
    on_loop_events_event_func_t on_loop_events_event;

    void* ctx;
} xsp_loop_events_event_handler_t;

// Default configuration.
extern const xsp_loop_events_config_t xsp_loop_events_config_default;

// Initializes loop events.
xsp_loop_events_handle_t xsp_loop_events_init(const xsp_loop_events_config_t* config,
                                              const xsp_loop_events_event_handler_t* evt_handler,
                                              xsp_loop_handle_t loop);

// Cleans up (shuts down) the loop events.
esp_err_t xsp_loop_events_cleanup(xsp_loop_events_handle_t loop_events);

// Returns the loop for the loop events (must be initialized and not cleaned up).
xsp_loop_handle_t xsp_loop_events_get_loop(xsp_loop_events_handle_t loop_events);

// Posts (schedules) an event with the given data.
esp_err_t xsp_loop_events_post_event(xsp_loop_events_handle_t loop_events, const void* data);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // XSP_WS_CLIENT_LOOP_EVENTS_H_
