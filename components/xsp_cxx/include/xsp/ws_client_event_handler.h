// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_CXX_INCLUDE_XSP_WS_CLIENT_EVENT_HANDLER_H_
#define XSP_CXX_INCLUDE_XSP_WS_CLIENT_EVENT_HANDLER_H_

#include <stddef.h>

#include "xsp_ws_client_types.h"

namespace xsp {

class Loop;

class WsClientEventHandler {
public:
    virtual void OnWsClientClosed(int status) {}
    virtual void OnWsClientDataFrameReceived(bool fin,
                                             xsp_ws_frame_opcode_t opcode,
                                             size_t payload_size,
                                             const void* payload) {}
    virtual void OnWsClientPingReceived(size_t payload_size, const void* payload) {}
    virtual void OnWsClientPongReceived(size_t payload_size, const void* payload) {}
    virtual void OnWsClientMessageSent(bool success) {}

protected:
    WsClientEventHandler() = default;
    ~WsClientEventHandler() = default;
};

}  // namespace xsp

#endif  // XSP_CXX_INCLUDE_XSP_WS_CLIENT_EVENT_HANDLER_H_
