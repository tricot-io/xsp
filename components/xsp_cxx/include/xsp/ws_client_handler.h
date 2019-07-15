// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_CXX_INCLUDE_XSP_WS_CLIENT_HANDLER_H_
#define XSP_CXX_INCLUDE_XSP_WS_CLIENT_HANDLER_H_

#include <stddef.h>

#include "xsp_ws_client_handler.h"

#include "xsp/loop.h"
#include "xsp/ws_client.h"
#include "xsp/ws_client_event_handler.h"

namespace xsp {

// A wrapper around xsp_ws_client_handler. This should be destroyed or shut down before the loop.
class WsClientHandler final {
public:
    WsClientHandler(const xsp_ws_client_handler_config_t* config,
                    WsClient ws_client,
                    Loop* loop,
                    WsClientEventHandler* evt_handler);
    ~WsClientHandler();

    // Copy and move not supported.
    WsClientHandler(const WsClientHandler&) = delete;
    WsClientHandler& operator=(const WsClientHandler&) = delete;

    void Shutdown();

    bool SendMessage(bool binary, size_t message_size, const void* message);
    bool Close(int close_status);
    bool Ping(size_t payload_size, const void* payload);

    WsClientEventHandler* ws_client_event_handler() const { return evt_handler_; }
    void set_ws_client_event_handler(WsClientEventHandler* ws_client_event_handler) {
        evt_handler_ = ws_client_event_handler;
    }

    xsp_ws_client_handler_handle_t handle() { return handle_; }

private:
    static void OnClosedThunk(xsp_ws_client_handler_handle_t handler, void* ctx, int status);
    void OnClosed(int status);

    static void OnDataFrameReceivedThunk(xsp_ws_client_handler_handle_t handler,
                                         void* ctx,
                                         bool fin,
                                         xsp_ws_frame_opcode_t opcode,
                                         int payload_size,
                                         const void* payload);
    void OnDataFrameReceived(bool fin,
                             xsp_ws_frame_opcode_t opcode,
                             int payload_size,
                             const void* payload);

    static void OnPingReceivedThunk(xsp_ws_client_handler_handle_t handler,
                                    void* ctx,
                                    int payload_size,
                                    const void* payload);
    void OnPingReceived(int payload_size, const void* payload);

    static void OnPongReceivedThunk(xsp_ws_client_handler_handle_t handler,
                                    void* ctx,
                                    int payload_size,
                                    const void* payload);
    void OnPongReceived(int payload_size, const void* payload);

    static void OnMessageSentThunk(xsp_ws_client_handler_handle_t handler, void* ctx, bool success);
    void OnMessageSent(bool success);

    WsClient ws_client_;
    xsp_ws_client_handler_handle_t handle_ = nullptr;
    WsClientEventHandler* evt_handler_ = nullptr;
};

}  // namespace xsp

#endif  // XSP_CXX_INCLUDE_XSP_WS_CLIENT_HANDLER_H_
