// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_WS_CLIENT_TYPES_H_
#define XSP_WS_CLIENT_TYPES_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xsp_ws_frame_opcode {
    XSP_WS_FRAME_OPCODE_CONTINUATION = 0x0,
    XSP_WS_FRAME_OPCODE_TEXT = 0x1,
    XSP_WS_FRAME_OPCODE_BINARY = 0x2,
    XSP_WS_FRAME_OPCODE_CONNECTION_CLOSE = 0x8,
    XSP_WS_FRAME_OPCODE_PING = 0x9,
    XSP_WS_FRAME_OPCODE_PONG = 0xa
} xsp_ws_frame_opcode_t;

enum {
    // Signifies the lack of a status (e.g., to send a Close frame with no payload).
    XSP_WS_STATUS_NONE = 0,

    // Standard close codes:
    XSP_WS_STATUS_CLOSE_NORMAL_CLOSURE = 1000,
    XSP_WS_STATUS_CLOSE_GOING_AWAY = 1001,
    XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR = 1002,
    XSP_WS_STATUS_CLOSE_UNSUPPORTED_DATA_TYPE = 1003,
    XSP_WS_STATUS_CLOSE_INVALID_DATA = 1007,
    XSP_WS_STATUS_CLOSE_POLICY_VIOLATION = 1008,
    XSP_WS_STATUS_CLOSE_MESSAGE_TOO_BIG = 1009,
    XSP_WS_STATUS_CLOSE_MANDATORY_EXTENSION = 1010,
    XSP_WS_STATUS_CLOSE_INTERNAL_SERVER_ERROR = 1011,

    // Public (registered) close codes:
    XSP_WS_STATUS_CLOSE_PUBLIC_BASE = 3000,
    XSP_WS_STATUS_CLOSE_PUBLIC_MAX = 3999,

    // Private (unregistered) close codes:
    XSP_WS_STATUS_CLOSE_PRIVATE_BASE = 4000,
    XSP_WS_STATUS_CLOSE_PRIVATE_MAX = 4999,

    // Reserved codes not to be sent in a Close frame:
    XSP_WS_STATUS_CLOSE_RESERVED_NO_STATUS_RECEIVED = 1005,
    XSP_WS_STATUS_CLOSE_RESERVED_ABNORMAL_CLOSURE = 1006,
    XSP_WS_STATUS_CLOSE_RESERVED_TLS_HANDSHAKE_FAILED = 1015
};

bool xsp_ws_is_valid_frame_opcode(int opcode);
bool xsp_ws_is_data_frame_opcode(xsp_ws_frame_opcode_t opcode);
bool xsp_ws_is_control_frame_opcode(xsp_ws_frame_opcode_t opcode);

bool xsp_ws_is_valid_close_frame_status(int status);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // XSP_WS_CLIENT_TYPES_H_
