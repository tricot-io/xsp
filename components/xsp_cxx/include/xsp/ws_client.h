// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_CXX_INCLUDE_XSP_WS_CLIENT_H_
#define XSP_CXX_INCLUDE_XSP_WS_CLIENT_H_

#include <assert.h>
#include <stddef.h>

#include "esp_err.h"

#include "xsp_ws_client.h"

namespace xsp {

// A thin wrapper around xsp_ws_client.
class WsClient final {
public:
    WsClient() = default;
    explicit WsClient(const xsp_ws_client_config_t& config) : handle_(xsp_ws_client_init(&config)) {
        assert(handle_);
    }
    explicit WsClient(const char* url) : WsClient(DefaultConfigForUrl(url)) {}

    ~WsClient() { Shutdown(); }

    // Move-only.
    WsClient(WsClient&& rhs) : handle_(rhs.handle_) { rhs.handle_ = nullptr; }
    WsClient& operator=(WsClient&& rhs) {
        if (handle_)
            Shutdown();
        handle_ = rhs.handle_;
        rhs.handle_ = nullptr;
        return *this;
    }

    void Shutdown() {
        if (handle_) {
            auto success = Close();
            assert(success);

            auto err = xsp_ws_client_cleanup(handle_);
            assert(err == ESP_OK);

            handle_ = nullptr;
        }
    }

    bool Open() { return xsp_ws_client_open(handle_) == ESP_OK; }
    bool Close() { return xsp_ws_client_close(handle_) == ESP_OK; }

    bool PollWrite(int timeout_ms) {
        return xsp_ws_client_poll_write(handle_, timeout_ms) != ESP_ERR_TIMEOUT;
    }
    bool WriteFrame(bool fin,
                    xsp_ws_frame_opcode_t opcode,
                    size_t payload_size,
                    const void* payload,
                    int timeout_ms) {
        return xsp_ws_client_write_frame(handle_, fin, opcode, static_cast<int>(payload_size),
                                         payload, timeout_ms) == ESP_OK;
    }
    bool WriteCloseFrame(int status, const char* reason, int timeout_ms) {
        return xsp_ws_client_write_close_frame(handle_, status, reason, timeout_ms) == ESP_OK;
    }

    bool PollRead(int timeout_ms) {
        return xsp_ws_client_poll_read(handle_, timeout_ms) != ESP_ERR_TIMEOUT;
    }
    bool ReadFrame(bool* fin,
                   xsp_ws_frame_opcode_t* opcode,
                   size_t payload_buffer_size,
                   void* payload_buffer,
                   size_t* payload_size,
                   int timeout_ms) {
        int payload_size2 = 0;
        auto rv = xsp_ws_client_read_frame(handle_, fin, opcode,
                                           static_cast<int>(payload_buffer_size), payload_buffer,
                                           &payload_size2, timeout_ms) == ESP_OK;
        *payload_size = static_cast<size_t>(payload_size2);
        return rv;
    }

    const char* response_subprotocols() { return xsp_ws_client_get_response_subprotocols(handle_); }

    xsp_ws_client_handle_t handle() { return handle_; }

private:
    static xsp_ws_client_config_t DefaultConfigForUrl(const char* url) {
        xsp_ws_client_config_t config = {};
        config.url = url;
        return config;
    }

    xsp_ws_client_handle_t handle_ = nullptr;
};

}  // namespace xsp

#endif  // XSP_CXX_INCLUDE_XSP_WS_CLIENT_H_
