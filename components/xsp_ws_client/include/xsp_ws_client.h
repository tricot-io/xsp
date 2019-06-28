// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_WS_CLIENT_H_
#define XSP_WS_CLIENT_H_

#include <stdbool.h>

#include "esp_err.h"

#include "xsp_ws_client_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xsp_ws_client_config {
    // WebSocket URL (ws/wss or http/https) of server.
    const char* url;

    // See esp_http_client_config_t.
    // TODO(vtl): Do we want more fields?
    const char* username;
    const char* password;
    const char* cert_pem;  // NOTE: Must remain valid for lifetime of xsp_ws_client.
    int http_timeout_ms;
    bool disable_auto_redirect;
    int max_redirection_count;

    // Subprotocol(s) to request via the Sec-WebSocket-Protocol header (optional), as a string
    // (comma-separated, with optional spaces). If set, it is up to the application to parse and
    // verify the response subprotocols (from
    const char* subprotocols;
} xsp_ws_client_config_t;

typedef struct xsp_ws_client* xsp_ws_client_handle_t;

typedef enum xsp_ws_client_state {
    XSP_WS_CLIENT_STATE_CLOSED = 0,      // The client is closed (or has not been opened yet).
    XSP_WS_CLIENT_STATE_OK,              // The client is open and operational.
    XSP_WS_CLIENT_STATE_FAILED,          // The client has failed, but Close may be sent.
    XSP_WS_CLIENT_STATE_FAILED_NO_CLOSE  // The client has failed, and Close should not be sent.
} xsp_ws_client_state_t;

// Initializes the WebSocket client.
xsp_ws_client_handle_t xsp_ws_client_init(const xsp_ws_client_config_t* config);

// Cleans up (shuts down) the WebSocket client.
esp_err_t xsp_ws_client_cleanup(xsp_ws_client_handle_t client);

// Opens a WebSocket connection.
esp_err_t xsp_ws_client_open(xsp_ws_client_handle_t client);

// Closes a WebSocket connection.
// NOTE: This does *not* send a Close (control) frame.
esp_err_t xsp_ws_client_close(xsp_ws_client_handle_t client);

// Returns the state of the client, which must be valid.
xsp_ws_client_state_t xsp_ws_client_get_state(xsp_ws_client_handle_t client);

// Returns the subprotocol(s) sent by the server in its response (in the Sec-WebSocket-Protocol
// header). Available when the client is open (the returned pointer belongs to the client). It is
// null if the server did not send a Sec-WebSocket-Protocol header; this is valid even if the client
// sent it (and indicates the server rejects all choices provided by the client).
const char* xsp_ws_client_get_response_subprotocols(xsp_ws_client_handle_t client);

// Returns a file descriptor suitable for use with `select()`, or -1 on error. This may only be
// called when both reading and writing are permitted.
int xsp_ws_client_get_select_fd(xsp_ws_client_handle_t client);

// Waits until data can (start to) be written.
esp_err_t xsp_ws_client_poll_write(xsp_ws_client_handle_t client, int timeout_ms);

// Writes a frame.
// NOTE: timeout_ms is per-write at the lower layer (i.e., is a timeout for "progress").
esp_err_t xsp_ws_client_write_frame(xsp_ws_client_handle_t client,
                                    bool fin,
                                    xsp_ws_frame_opcode_t opcode,
                                    int payload_size,
                                    const void* payload,
                                    int timeout_ms);

// Writes a Close frame with the given status and (optional) reason. Note that the reason should be
// valid UTF-8.
esp_err_t xsp_ws_client_write_close_frame(xsp_ws_client_handle_t client,
                                          int status,
                                          const char* reason,
                                          int timeout_ms);

// Waits until data can (start to) be read.
esp_err_t xsp_ws_client_poll_read(xsp_ws_client_handle_t client, int timeout_ms);

// Reads a frame.
// NOTE: timeout_ms is per-read at the lower layer (i.e., is a timeout for "progress").
esp_err_t xsp_ws_client_read_frame(xsp_ws_client_handle_t client,
                                   bool* fin,
                                   xsp_ws_frame_opcode_t* opcode,
                                   int payload_buffer_size,
                                   void* payload_buffer,
                                   int* payload_size,
                                   int timeout_ms);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // XSP_WS_CLIENT_H_
