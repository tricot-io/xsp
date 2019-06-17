// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_ws_client_types.h"

bool xsp_ws_is_valid_frame_opcode(int opcode) {
    switch (opcode) {
    case (int)XSP_WS_FRAME_OPCODE_CONTINUATION:
    case (int)XSP_WS_FRAME_OPCODE_TEXT:
    case (int)XSP_WS_FRAME_OPCODE_BINARY:
    case (int)XSP_WS_FRAME_OPCODE_CONNECTION_CLOSE:
    case (int)XSP_WS_FRAME_OPCODE_PING:
    case (int)XSP_WS_FRAME_OPCODE_PONG:
        return true;
    default:
        return false;
    }
}

bool xsp_ws_is_data_frame_opcode(xsp_ws_frame_opcode_t opcode) {
    return (int)opcode < 8;
}

bool xsp_ws_is_control_frame_opcode(xsp_ws_frame_opcode_t opcode) {
    return (int)opcode >= 8;
}

bool xsp_ws_is_valid_close_frame_status(int status) {
    switch (status) {
    case XSP_WS_STATUS_CLOSE_NORMAL_CLOSURE:
    case XSP_WS_STATUS_CLOSE_GOING_AWAY:
    case XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR:
    case XSP_WS_STATUS_CLOSE_UNSUPPORTED_DATA_TYPE:
    case XSP_WS_STATUS_CLOSE_INVALID_DATA:
    case XSP_WS_STATUS_CLOSE_POLICY_VIOLATION:
    case XSP_WS_STATUS_CLOSE_MESSAGE_TOO_BIG:
    case XSP_WS_STATUS_CLOSE_MANDATORY_EXTENSION:
    case XSP_WS_STATUS_CLOSE_INTERNAL_SERVER_ERROR:
        return true;
    default:
        if (status >= XSP_WS_STATUS_CLOSE_PUBLIC_BASE && status <= XSP_WS_STATUS_CLOSE_PRIVATE_MAX)
            return true;
        return false;
    }
}
