// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp/ws_client_handler.h"

#include <assert.h>

#include <utility>

#include "esp_err.h"

namespace xsp {

WsClientHandler::WsClientHandler(const xsp_ws_client_handler_config_t* config,
                                 WsClient ws_client,
                                 Loop* loop,
                                 WsClientEventHandler* evt_handler)
        : ws_client_(std::move(ws_client)), evt_handler_(evt_handler) {
    xsp_ws_client_event_handler_t evt_handler_thunks = {
            &WsClientHandler::OnClosedThunk,       &WsClientHandler::OnDataFrameReceivedThunk,
            &WsClientHandler::OnPingReceivedThunk, &WsClientHandler::OnPongReceivedThunk,
            &WsClientHandler::OnMessageSentThunk,  this};
    handle_ = xsp_ws_client_handler_init(config, &evt_handler_thunks, ws_client_.handle(),
                                         loop->handle());
    assert(handle_);
}

WsClientHandler::~WsClientHandler() {
    Shutdown();
}

void WsClientHandler::Shutdown() {
    evt_handler_ = nullptr;

    if (handle_) {
        auto err = xsp_ws_client_handler_cleanup(handle_);
        assert(err == ESP_OK);
    }

    ws_client_.Shutdown();
}

bool WsClientHandler::SendMessage(bool binary, size_t message_size, const void* message) {
    return xsp_ws_client_handler_send_message(handle_, binary, static_cast<int>(message_size),
                                              message) == ESP_OK;
}

bool WsClientHandler::Close(int close_status) {
    return xsp_ws_client_handler_close(handle_, close_status) == ESP_OK;
}

bool WsClientHandler::Ping(size_t payload_size, const void* payload) {
    return xsp_ws_client_handler_ping(handle_, static_cast<int>(payload_size), payload) == ESP_OK;
}

// static
void WsClientHandler::OnClosedThunk(xsp_ws_client_handler_handle_t handler, void* ctx, int status) {
    static_cast<WsClientHandler*>(ctx)->OnClosed(status);
}

void WsClientHandler::OnClosed(int status) {
    if (evt_handler_)
        evt_handler_->OnWsClientClosed(status);
}

// static
void WsClientHandler::OnDataFrameReceivedThunk(xsp_ws_client_handler_handle_t handler,
                                               void* ctx,
                                               bool fin,
                                               xsp_ws_frame_opcode_t opcode,
                                               int payload_size,
                                               const void* payload) {
    static_cast<WsClientHandler*>(ctx)->OnDataFrameReceived(fin, opcode, payload_size, payload);
}

void WsClientHandler::OnDataFrameReceived(bool fin,
                                          xsp_ws_frame_opcode_t opcode,
                                          int payload_size,
                                          const void* payload) {
    if (evt_handler_) {
        evt_handler_->OnWsClientDataFrameReceived(fin, opcode, static_cast<size_t>(payload_size),
                                                  payload);
    }
}

// static
void WsClientHandler::OnPingReceivedThunk(xsp_ws_client_handler_handle_t handler,
                                          void* ctx,
                                          int payload_size,
                                          const void* payload) {
    static_cast<WsClientHandler*>(ctx)->OnPingReceived(payload_size, payload);
}

void WsClientHandler::OnPingReceived(int payload_size, const void* payload) {
    if (evt_handler_)
        evt_handler_->OnWsClientPingReceived(static_cast<size_t>(payload_size), payload);
}

// static
void WsClientHandler::OnPongReceivedThunk(xsp_ws_client_handler_handle_t handler,
                                          void* ctx,
                                          int payload_size,
                                          const void* payload) {
    static_cast<WsClientHandler*>(ctx)->OnPongReceived(payload_size, payload);
}

void WsClientHandler::OnPongReceived(int payload_size, const void* payload) {
    if (evt_handler_)
        evt_handler_->OnWsClientPongReceived(static_cast<size_t>(payload_size), payload);
}

// static
void WsClientHandler::OnMessageSentThunk(xsp_ws_client_handler_handle_t handler,
                                         void* ctx,
                                         bool success) {
    static_cast<WsClientHandler*>(ctx)->OnMessageSent(success);
}

void WsClientHandler::OnMessageSent(bool success) {
    if (evt_handler_)
        evt_handler_->OnWsClientMessageSent(success);
}

}  // namespace xsp
