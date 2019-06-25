// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_WS_CLIENT_LOOP_H_
#define XSP_WS_CLIENT_LOOP_H_

#include <stdbool.h>

#include "esp_err.h"

#include "xsp_ws_client.h"
#include "xsp_ws_client_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOTES
//
// *   The loop receives *frames* and provides them to the user (to be defragmented into messages if
//     desired at another layer).
//     *   Users may not need to defragment, or may not be able to defragment.
//     *   Users may want to set their own policy for message data allocation and storage.
// *   It sends *messages* provided by the user (fragmenting them as required).
//     *   This makes it easy to use.
//     *   Note however that this precludes sending "streamed" messages (i.e., a message whose data
//         is provided dynamically).
// *   It receives and sends frames *synchronously*.
//     *   This is because `xsp_ws_client` is synchronous.
// *   It sends messages asynchronously.
//     *   This allows larger messages to be sent without blocking, and frames to be received
//         while doing so (for multi-frame messages).

typedef struct xsp_ws_client_loop_config {
    int max_frame_read_size;        // Must be at least 125.
    int max_data_frame_write_size;  // Must be at least 1.

    int poll_read_timeout_ms;
    int poll_write_timeout_ms;
    int read_timeout_ms;
    int write_timeout_ms;
} xsp_ws_client_loop_config_t;

typedef struct xsp_ws_client_loop* xsp_ws_client_loop_handle_t;

// WebSocket client loop events.
typedef enum xsp_ws_client_loop_event_type {
    // First event generated each time the event loop is run.
    XSP_WS_CLIENT_LOOP_EVENT_STARTED = 0,

    // Last event generated each time the event loop is run.
    XSP_WS_CLIENT_LOOP_EVENT_STOPPED,

    // Event generated when no work was done.
    // NOTE: To prevent spinning, you may want to briefly sleep on this event.
    XSP_WS_CLIENT_LOOP_EVENT_IDLE,

    // Event generated when the connection is closed (or failed). The loop will terminate
    // immediately afterwards.
    XSP_WS_CLIENT_LOOP_EVENT_CLOSED,

    // Event generated when a data (non-control) frame is received.
    XSP_WS_CLIENT_LOOP_EVENT_DATA_FRAME_RECEIVED,

    // Event generated when a ping frame is received.
    XSP_WS_CLIENT_LOOP_EVENT_PING_RECEIVED,

    // Event generated when a pong frame is received.
    XSP_WS_CLIENT_LOOP_EVENT_PONG_RECEIVED,

    // Event generated when a message scheduled using `xsp_ws_client_loop_send_message()` has
    // completed transmission (or the transmission has failed).
    XSP_WS_CLIENT_LOOP_EVENT_MESSAGE_SENT
} xsp_ws_client_loop_event_type_t;

struct xsp_ws_client_loop_event_started {};

struct xsp_ws_client_loop_event_stopped {};

struct xsp_ws_client_loop_event_idle {};

struct xsp_ws_client_loop_event_closed {
    int status;
};

struct xsp_ws_client_loop_event_data_frame_received {
    bool fin;
    xsp_ws_frame_opcode_t opcode;
    int payload_size;
    const void* payload;
};

struct xsp_ws_client_loop_event_ping_received {
    int payload_size;
    const void* payload;
};

struct xsp_ws_client_loop_event_pong_received {
    int payload_size;
    const void* payload;
};

struct xsp_ws_client_loop_event_message_sent {
    bool success;
};

union xsp_ws_client_loop_event_data {
    struct xsp_ws_client_loop_event_started started;
    struct xsp_ws_client_loop_event_stopped stopped;
    struct xsp_ws_client_loop_event_idle idle;
    struct xsp_ws_client_loop_event_closed closed;
    struct xsp_ws_client_loop_event_data_frame_received data_frame_received;
    struct xsp_ws_client_loop_event_ping_received ping_received;
    struct xsp_ws_client_loop_event_pong_received pong_received;
    struct xsp_ws_client_loop_event_message_sent message_sent;
};

typedef struct xsp_ws_client_loop_event {
    xsp_ws_client_loop_event_type_t type;
    union xsp_ws_client_loop_event_data data;
} xsp_ws_client_loop_event_t;

typedef void (*xsp_ws_client_loop_event_handler_t)(xsp_ws_client_loop_handle_t loop,
                                                   void* ctx,
                                                   const xsp_ws_client_loop_event_t* evt);

// Default configuration.
extern const xsp_ws_client_loop_config_t xsp_ws_client_loop_config_default;

// Initializes the WebSocket client loop; doesn't take ownership of the client: `client` should
// remain valid for the lifetime of the loop; it must be connected before "run". The resulting loop
// may be used only once per connection (even if the same client is reconnected, a new loop must be
// created).
// TODO(vtl): Add a reinit or reset function?
xsp_ws_client_loop_handle_t xsp_ws_client_loop_init(const xsp_ws_client_loop_config_t* config,
                                                    xsp_ws_client_handle_t client,
                                                    xsp_ws_client_loop_event_handler_t evt_handler,
                                                    void* ctx);

// Cleans up (shuts down) the WebSocket client loop (does not clean up the client).
esp_err_t xsp_ws_client_loop_cleanup(xsp_ws_client_loop_handle_t loop);

// Runs the client loop. The loop will run until either the `xsp_ws_client_loop_stop()` is called or
// the connection is closed.
esp_err_t xsp_ws_client_loop_run(xsp_ws_client_loop_handle_t loop);

// Stops the loop as soon as possible. Should only be called from "inside" the loop, i.e., inside
// the event handler.
esp_err_t xsp_ws_client_loop_stop(xsp_ws_client_loop_handle_t loop);

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

#endif  // XSP_WS_CLIENT_LOOP_H_
