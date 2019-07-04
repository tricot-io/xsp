// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_ws_client_handler.h"

#include <stdlib.h>

#include "esp_log.h"

#include "xsp_ws_client_utf8.h"

#include "sdkconfig.h"

typedef struct xsp_ws_client_handler {
    xsp_ws_client_handler_config_t config;
    xsp_ws_client_event_handler_t evt_handler;
    xsp_ws_client_handle_t client;
    xsp_loop_handle_t loop;

    xsp_loop_fd_watcher_handle_t fd_watcher;

    void* read_buffer;
    int read_buffer_size;

    // State that's persistent across multiple runnings of the loop.
    bool close_sent;
    bool close_event_sent;
    int close_status;
    bool sending_message;
    bool send_is_binary;       // Only valid when sending message.
    const void* send_message;  // Only valid when sending message. Not owned by us.
    int send_size;             // Only valid when sending message.
    int send_written;          // Only valid when sending message.
} xsp_ws_client_handler_t;

static const char TAG[] = "WS_CLIENT_HANDLER";

#if CONFIG_XSP_WS_CLIENT_HANDLER_DEFAULT_MAX_FRAME_READ_SIZE < 125 ||         \
        CONFIG_XSP_WS_CLIENT_HANDLER_DEFAULT_MAX_DATA_FRAME_WRITE_SIZE < 1 || \
        CONFIG_XSP_WS_CLIENT_HANDLER_DEFAULT_READ_TIMEOUT_MS < 0 ||           \
        CONFIG_XSP_WS_CLIENT_HANDLER_DEFAULT_WRITE_TIMEOUT_MS < 0
#error "Invalid value for CONFIG_XSP_WS_CLIENT_HANDLER_DEFAULT_..."
#endif

const xsp_ws_client_handler_config_t xsp_ws_client_handler_config_default = {
        CONFIG_XSP_WS_CLIENT_HANDLER_DEFAULT_MAX_FRAME_READ_SIZE,
        CONFIG_XSP_WS_CLIENT_HANDLER_DEFAULT_MAX_DATA_FRAME_WRITE_SIZE,
        CONFIG_XSP_WS_CLIENT_HANDLER_DEFAULT_READ_TIMEOUT_MS,
        CONFIG_XSP_WS_CLIENT_HANDLER_DEFAULT_WRITE_TIMEOUT_MS};

static bool validate_config(const xsp_ws_client_handler_config_t* config) {
    if (!config)
        return true;
    if (config->max_frame_read_size < 125)
        return false;
    if (config->max_data_frame_write_size < 1)
        return false;
    if (config->read_timeout_ms < 0)
        return false;
    if (config->write_timeout_ms < 0)
        return false;
    return true;
}

static void do_read(xsp_ws_client_handler_handle_t handler) {
    bool fin;
    xsp_ws_frame_opcode_t opcode;
    int payload_size;
    esp_err_t err = xsp_ws_client_read_frame(handler->client, &fin, &opcode,
                                             handler->read_buffer_size, handler->read_buffer,
                                             &payload_size, handler->config.read_timeout_ms);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Read frame failed: %s", esp_err_to_name(err));
        return;
    }

    switch (opcode) {
    case XSP_WS_FRAME_OPCODE_CONTINUATION:
    case XSP_WS_FRAME_OPCODE_TEXT:
    case XSP_WS_FRAME_OPCODE_BINARY:
        if (handler->evt_handler.on_ws_client_data_frame_received) {
            handler->evt_handler.on_ws_client_data_frame_received(handler, handler->evt_handler.ctx,
                                                                  fin, opcode, payload_size,
                                                                  handler->read_buffer);
        }
        break;

    case XSP_WS_FRAME_OPCODE_CONNECTION_CLOSE:
        if (handler->close_sent)
            break;  // Nothing more to do.

        if (payload_size == 0) {
            handler->close_status = XSP_WS_STATUS_CLOSE_RESERVED_NO_STATUS_RECEIVED;
            xsp_ws_client_write_close_frame(handler->client, XSP_WS_STATUS_NONE, NULL,
                                            handler->config.write_timeout_ms);
        } else if (payload_size == 1) {
            handler->close_status = XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR;
            xsp_ws_client_write_close_frame(handler->client, XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR,
                                            NULL, handler->config.write_timeout_ms);
        } else {
            const unsigned char* read_buffer = handler->read_buffer;
            // Record the close status (if it's valid).
            int close_status = (int)(((int)read_buffer[0] << 8) | (int)read_buffer[1]);
            if (xsp_ws_is_valid_close_frame_status(close_status)) {
                if (xsp_ws_client_utf8_validate(payload_size - 2, read_buffer + 2)) {
                    handler->close_status = close_status;
                    // Echo the Close frame.
                    xsp_ws_client_write_frame(handler->client, true,
                                              XSP_WS_FRAME_OPCODE_CONNECTION_CLOSE, payload_size,
                                              handler->read_buffer,
                                              handler->config.write_timeout_ms);
                } else {
                    handler->close_status = XSP_WS_STATUS_CLOSE_INVALID_DATA;
                    xsp_ws_client_write_close_frame(handler->client,
                                                    XSP_WS_STATUS_CLOSE_INVALID_DATA, NULL,
                                                    handler->config.write_timeout_ms);
                }
            } else {
                handler->close_status = XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR;
                xsp_ws_client_write_close_frame(handler->client, XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR,
                                                NULL, handler->config.write_timeout_ms);
            }
        }
        handler->close_sent = true;
        if (handler->evt_handler.on_ws_client_closed) {
            handler->evt_handler.on_ws_client_closed(handler, handler->evt_handler.ctx,
                                                     handler->close_status);
        }
        break;

    case XSP_WS_FRAME_OPCODE_PING:
        // Send pong automatically.
        xsp_ws_client_write_frame(handler->client, true, XSP_WS_FRAME_OPCODE_PONG, payload_size,
                                  handler->read_buffer, handler->config.write_timeout_ms);
        if (handler->evt_handler.on_ws_client_ping_received) {
            handler->evt_handler.on_ws_client_ping_received(handler, handler->evt_handler.ctx,
                                                            payload_size, handler->read_buffer);
        }
        break;

    case XSP_WS_FRAME_OPCODE_PONG:
        if (handler->evt_handler.on_ws_client_pong_received) {
            handler->evt_handler.on_ws_client_pong_received(handler, handler->evt_handler.ctx,
                                                            payload_size, handler->read_buffer);
        }
        break;
    }
}

static xsp_loop_fd_watch_for_t on_loop_will_select(xsp_loop_handle_t loop, void* ctx, int fd) {
    xsp_ws_client_handler_handle_t handler = (xsp_ws_client_handler_handle_t)ctx;

    // TODO(vtl): Even if We do real work in this, but the loop will still consider us to be idle.
    while (xsp_ws_client_has_buffered_read_data(handler->client))
        do_read(handler);

    return handler->sending_message ? XSP_LOOP_FD_WATCH_FOR_WRITE_READ : XSP_LOOP_FD_WATCH_FOR_READ;
}

static void on_loop_can_read_fd(xsp_loop_handle_t loop, void* ctx, int fd) {
    xsp_ws_client_handler_handle_t handler = (xsp_ws_client_handler_handle_t)ctx;

    do_read(handler);
}

static void send_message_completed(xsp_ws_client_handler_handle_t handler, bool success) {
    handler->sending_message = false;
    if (handler->evt_handler.on_ws_client_message_sent)
        handler->evt_handler.on_ws_client_message_sent(handler, handler->evt_handler.ctx, success);
}

static void do_write(xsp_ws_client_handler_handle_t handler) {
    int write_size = handler->config.max_data_frame_write_size;
    if (write_size > handler->send_size - handler->send_written)
        write_size = handler->send_size - handler->send_written;

    xsp_ws_frame_opcode_t opcode;
    if (handler->send_written == 0)
        opcode = handler->send_is_binary ? XSP_WS_FRAME_OPCODE_BINARY : XSP_WS_FRAME_OPCODE_TEXT;
    else
        opcode = XSP_WS_FRAME_OPCODE_CONTINUATION;
    const void* payload = (const char*)handler->send_message + handler->send_written;
    handler->send_written += write_size;
    bool fin = handler->send_written == handler->send_size;

    esp_err_t err = xsp_ws_client_write_frame(handler->client, fin, opcode, write_size, payload,
                                              handler->config.write_timeout_ms);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Write frame failed: %s", esp_err_to_name(err));
        send_message_completed(handler, false);
        return;
    }

    if (fin)
        send_message_completed(handler, true);
}

static void on_loop_can_write_fd(xsp_loop_handle_t loop, void* ctx, int fd) {
    xsp_ws_client_handler_handle_t handler = (xsp_ws_client_handler_handle_t)ctx;

    do {
        do_write(handler);
    } while (handler->sending_message && xsp_ws_client_poll_write(handler->client, 0) == ESP_OK);
}

xsp_ws_client_handler_handle_t xsp_ws_client_handler_init(
        const xsp_ws_client_handler_config_t* config,
        const xsp_ws_client_event_handler_t* evt_handler,
        xsp_ws_client_handle_t client,
        xsp_loop_handle_t loop) {
    if (!config)
        config = &xsp_ws_client_handler_config_default;

    if (!validate_config(config)) {
        ESP_LOGE(TAG, "Invalid config");
        return NULL;
    }
    if (!evt_handler || !client || !loop) {
        ESP_LOGE(TAG, "Invalid argument");
        return NULL;
    }

    int fd = xsp_ws_client_get_select_fd(client);
    if (fd < 0) {
        ESP_LOGE(TAG, "Invalid client (state)");
        return NULL;
    }

    xsp_ws_client_handler_handle_t handler =
            (xsp_ws_client_handler_handle_t)malloc(sizeof(xsp_ws_client_handler_t));
    if (!handler) {
        ESP_LOGE(TAG, "Allocation failed");
        return NULL;
    }

    handler->config = *config;
    handler->evt_handler = *evt_handler;
    handler->client = client;
    handler->loop = loop;

    xsp_loop_fd_event_handler_t loop_fd_event_handler = {
        on_loop_will_select,
        on_loop_can_read_fd,
        on_loop_can_write_fd,
        handler,
        fd,
    };
    handler->fd_watcher = xsp_loop_add_fd_watcher(loop, &loop_fd_event_handler);
    if (!handler->fd_watcher) {
        ESP_LOGE(TAG, "Failed to watch FD");
        return NULL;
    }

    handler->read_buffer = malloc(handler->config.max_frame_read_size);
    if (!handler->read_buffer) {
        ESP_LOGE(TAG, "Allocation failed");
        // TODO(vtl): Check return value?
        xsp_loop_remove_fd_watcher(loop, handler->fd_watcher);
        free(handler);
        return NULL;
    }
    handler->read_buffer_size = handler->config.max_frame_read_size;

    handler->close_sent = false;
    handler->close_event_sent = false;
    handler->close_status = XSP_WS_STATUS_NONE;
    handler->sending_message = false;

    return handler;
}

esp_err_t xsp_ws_client_handler_cleanup(xsp_ws_client_handler_handle_t handler) {
    if (!handler)
        return ESP_FAIL;

    if (xsp_loop_is_running(handler->loop))
        return ESP_ERR_INVALID_STATE;

    free(handler->read_buffer);
    // TODO(vtl): Check return value?
    xsp_loop_remove_fd_watcher(handler->loop, handler->fd_watcher);
    free(handler);
    return ESP_OK;
}

esp_err_t xsp_ws_client_handler_send_message(xsp_ws_client_handler_handle_t handler,
                                             bool binary,
                                             int message_size,
                                             const void* message) {
    if (!handler || message_size < 0 || (message_size > 0 && !message))
        return ESP_ERR_INVALID_ARG;
    if (!xsp_loop_is_running(handler->loop) || handler->sending_message)
        return ESP_ERR_INVALID_STATE;
    if (xsp_ws_client_get_state(handler->client) != XSP_WS_CLIENT_STATE_OK)
        return ESP_FAIL;

    handler->sending_message = true;
    handler->send_is_binary = binary;
    handler->send_message = message;
    handler->send_size = message_size;
    handler->send_written = 0;
    return ESP_OK;
}

esp_err_t xsp_ws_client_handler_close(xsp_ws_client_handler_handle_t handler, int close_status) {
    if (!handler || !xsp_ws_is_valid_close_frame_status(close_status))
        return ESP_ERR_INVALID_ARG;
    if (!xsp_loop_is_running(handler->loop))
        return ESP_ERR_INVALID_STATE;

    // Don't report an error if we can't actually send a close frame. Note that in the
    // XSP_WS_CLIENT_STATE_FAILED case, we'll send a close frame automatically.
    if (xsp_ws_client_get_state(handler->client) != XSP_WS_CLIENT_STATE_OK)
        return ESP_OK;

    // Note: We'll avoid reporting an error if we're already sent a close frame, since it may have
    // been due to echoing a close frame from the server. (Moreover, an idempotent close is nice to
    // have.)
    if (handler->close_sent)
        return ESP_OK;

    xsp_ws_client_write_close_frame(handler->client, close_status, NULL,
                                    handler->config.write_timeout_ms);
    handler->close_sent = true;
    return ESP_OK;
}

esp_err_t xsp_ws_client_handler_ping(xsp_ws_client_handler_handle_t handler,
                                     int payload_size,
                                     const void* payload) {
    if (!handler || payload_size < 0 || payload_size > 125 || (payload_size != 0 && !payload))
        return ESP_ERR_INVALID_ARG;
    if (!xsp_loop_is_running(handler->loop))
        return ESP_ERR_INVALID_STATE;

    return xsp_ws_client_write_frame(handler->client, true, XSP_WS_FRAME_OPCODE_PING, payload_size,
                                     payload, handler->config.write_timeout_ms);
}
