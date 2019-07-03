// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_WS_CLIENT_HANDLER_H_
#define XSP_WS_CLIENT_HANDLER_H_

#include <stdbool.h>

#include "esp_err.h"

#include "xsp_loop.h"
#include "xsp_ws_client.h"
#include "xsp_ws_client_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xsp_ws_client_handler_config {
    int max_frame_read_size;        // Must be at least 125.
    int max_data_frame_write_size;  // Must be at least 1.

    int read_timeout_ms;
    int write_timeout_ms;
} xsp_ws_client_handler_config_t;

typedef struct xsp_ws_client_handler* xsp_ws_client_handler_handle_t;

typedef void (*on_ws_client_closed_func_t)(xsp_ws_client_handler_handle_t handler,
                                           void* ctx,
                                           int status);
typedef void (*on_ws_client_data_frame_received_func_t)(xsp_ws_client_handler_handle_t handler,
                                                        void* ctx,
                                                        bool fin,
                                                        xsp_ws_frame_opcode_t opcode,
                                                        int payload_size,
                                                        const void* payload);
typedef void (*on_ws_client_ping_received_func_t)(xsp_ws_client_handler_handle_t handler,
                                                  void* ctx,
                                                  int payload_size,
                                                  const void* payload);
typedef void (*on_ws_client_pong_received_func_t)(xsp_ws_client_handler_handle_t handler,
                                                  void* ctx,
                                                  int payload_size,
                                                  const void* payload);
typedef void (*on_ws_client_message_sent_func_t)(xsp_ws_client_handler_handle_t handler,
                                                 void* ctx,
                                                 bool success);

typedef struct xsp_ws_client_event_handler {
    // Event generated when the connection is closed (or failed).
    on_ws_client_closed_func_t on_ws_client_closed;

    // Event generated when a data (non-control) frame is received.
    on_ws_client_data_frame_received_func_t on_ws_client_data_frame_received;

    // Event generated when a ping frame is received.
    on_ws_client_ping_received_func_t on_ws_client_ping_received;

    // Event generated when a pong frame is received.
    on_ws_client_pong_received_func_t on_ws_client_pong_received;

    // Event generated when a message scheduled using `xsp_ws_client_loop_send_message()` has
    // completed transmission (or the transmission has failed).
    on_ws_client_message_sent_func_t on_ws_client_message_sent;

    void* ctx;
} xsp_ws_client_event_handler_t;

// Default configuration.
extern const xsp_ws_client_handler_config_t xsp_ws_client_handler_config_default;

// Initializes the WebSocket client handler; doesn't take ownership of the client or the loop, and
// both should remain valid for the lifetime of the handler. The client must already be connected.
xsp_ws_client_handler_handle_t xsp_ws_client_handler_init(
        const xsp_ws_client_handler_config_t* config,
        const xsp_ws_client_event_handler_t* evt_handler,
        xsp_ws_client_handle_t client,
        xsp_loop_handle_t loop);




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

/*
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
*/

/*
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
*/

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // XSP_WS_CLIENT_HANDLER_H_
