// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_WS_CLIENT_DEFRAG_H_
#define XSP_WS_CLIENT_DEFRAG_H_

#include "esp_err.h"

#include "xsp_ws_client_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// TODO(vtl): Maybe add a configurable allocator (basically a realloc()-equivalent will do).
typedef struct xsp_ws_client_defrag_config {
    int max_message_size;  // Must be strictly positive.
} xsp_ws_client_defrag_config_t;

typedef struct xsp_ws_client_defrag* xsp_ws_client_defrag_handle_t;

// Initializes the WebSocket client defragmenter.
xsp_ws_client_defrag_handle_t xsp_ws_client_defrag_init(
        const xsp_ws_client_defrag_config_t* config);

// Cleans up (shuts down) the WebSocket client defragmenter.
esp_err_t xsp_ws_client_defrag_cleanup(xsp_ws_client_defrag_handle_t defrag);

// Handles a data frame. When a message complete, `*done` will be set to true (otherwise it is set
// to false) and the `*message_*` will also be set, in which case `*message_opcode` will be the
// opcode of the first frame of the message. Note that caller takes ownership of `*message_data`
// and must eventually `free()` it.
//
// This only returns an error (i.e., not ESP_OK) on failure:
// *   ESP_ERR_NO_MEM on allocation failure,
// *   ESP_ERR_INVALID_SIZE if the current message is too big (per the configuration),
// *   ESP_FAIL on protocol error (non-continuation opcode after the first frame),
// *   ESP_ERR_INVALID_RESPONSE if the current message contains invalid UTF-8 data.
//
// To discard the current message in an error case, this may be continued to be called on subsequent
// frames. This will continue to return the same error until (and including) the call in which
// `*done` is set to true. (The subsequent call will start a new message.)
esp_err_t xsp_ws_client_defrag_on_data_frame(xsp_ws_client_defrag_handle_t defrag,
                                             bool fin,
                                             xsp_ws_frame_opcode_t opcode,
                                             int payload_size,
                                             const void* payload,
                                             bool* done,
                                             xsp_ws_frame_opcode_t* message_opcode,
                                             int* message_size,
                                             void** message_data);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // XSP_WS_CLIENT_DEFRAG_H_
