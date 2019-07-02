// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_loop.h"

#include <stdbool.h>
#include <stdlib.h>

#include "esp_log.h"

#include "sdkconfig.h"

typedef struct xsp_loop {
    xsp_loop_config_t config;
    xsp_loop_event_handler_t evt_handler;
    void* ctx;

    bool is_running;
    bool should_stop;
} xsp_loop_t;

static const char TAG[] = "LOOP";

#if CONFIG_XSP_LOOP_DEFAULT_POLL_TIMEOUT_MS < 0
#error "Invalid value for CONFIG_XSP_LOOP_DEFAULT_..."
#endif

const xsp_loop_config_t xsp_loop_config_default = {CONFIG_XSP_LOOP_DEFAULT_POLL_TIMEOUT_MS};

static bool validate_config(const xsp_loop_config_t* config) {
    if (!config)
        return true;
    if (config->poll_timeout_ms < 0)
        return false;
    return true;
}

xsp_loop_handle_t xsp_loop_init(const xsp_loop_config_t* config,
                                const xsp_loop_event_handler_t* evt_handler,
                                void* ctx) {
    if (!config)
        config = &xsp_loop_config_default;

    if (!validate_config(config)) {
        ESP_LOGE(TAG, "Invalid config");
        return NULL;
    }

    xsp_loop_handle_t loop = (xsp_loop_handle_t)calloc(1, sizeof(xsp_loop_t));
    if (!loop) {
        ESP_LOGE(TAG, "Allocation failed");
        return NULL;
    }

    loop->config = *config;
    if (evt_handler)
        loop->evt_handler = *evt_handler;
    loop->ctx = ctx;

    return loop;
}

esp_err_t xsp_loop_cleanup(xsp_loop_handle_t loop) {
    if (!loop)
        return ESP_FAIL;

    free(loop);
    return ESP_OK;
}

// Returns true if we should continue.
static bool do_loop_iteration(xsp_loop_handle_t loop) {
    if (loop->should_stop)
        return false;

    bool did_something = false;

#if 0
    // Handle pre-buffered data first, as a special case.
    if (xsp_ws_client_has_buffered_read_data(loop->client)) {
        do_read(loop);
        did_something = true;
    }

    // TODO(vtl): We don't need to get the FD each iteration.
    int fd = xsp_ws_client_get_select_fd(loop->client);
    if (fd < 0)
        return false;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    fd_set write_fds;
    FD_ZERO(&write_fds);
    if (loop->sending_message)
        FD_SET(fd, &write_fds);

    // TODO(vtl): We shouldn't have to do this conversion each iteration.
    struct timeval timeout;
    esp_transport_utils_ms_to_timeval(loop->config.poll_timeout_ms, &timeout);
    // TODO(vtl): Possibly, we should check for error (-1) vs timeout (0).
    if (select(fd + 1, &read_fds, &write_fds, NULL, &timeout) > 0) {
        if (loop->sending_message && FD_ISSET(fd, &write_fds)) {
            do_write(loop);
            // Keep on writing while we need to and can.
            while (loop->sending_message && select(fd + 1, NULL, &write_fds, NULL, &timeout) > 0)
                do_write(loop);
            did_something = true;
            if (should_stop(loop))
                return false;
        }
        if (FD_ISSET(fd, &read_fds)) {
            do_read(loop);
            did_something = true;
        }
    }
    if (should_stop(loop))
        return false;
#endif

    // Do idle if nothing happened.
    if (!did_something) {
        if (loop->evt_handler.on_loop_idle)
            loop->evt_handler.on_loop_idle(loop, loop->ctx);
    }
    if (loop->should_stop)
        return false;

    return true;
}

esp_err_t xsp_loop_run(xsp_loop_handle_t loop) {
    if (!loop)
        return ESP_ERR_INVALID_ARG;
    if (loop->is_running)
        return ESP_ERR_INVALID_STATE;

    loop->is_running = true;
    loop->should_stop = false;
    if (loop->evt_handler.on_loop_start)
        loop->evt_handler.on_loop_start(loop, loop->ctx);
    while (do_loop_iteration(loop))
        ;  // Nothing.
    if (loop->evt_handler.on_loop_stop)
        loop->evt_handler.on_loop_stop(loop, loop->ctx);
    loop->is_running = false;

    return ESP_OK;
}

esp_err_t xsp_loop_stop(xsp_loop_handle_t loop) {
    if (!loop)
        return ESP_ERR_INVALID_ARG;
    if (!loop->is_running)
        return ESP_ERR_INVALID_STATE;

    loop->should_stop = true;
    return ESP_OK;
}

//FIXME
#if 0
#include <sys/select.h>
#include <sys/time.h>

#include "esp_transport_utils.h"

// Returns true if the loop should stop.
static void do_read(xsp_ws_client_loop_handle_t loop) {
    bool fin;
    xsp_ws_frame_opcode_t opcode;
    int payload_size;
    esp_err_t err = xsp_ws_client_read_frame(loop->client, &fin, &opcode, loop->read_buffer_size,
                                             loop->read_buffer, &payload_size,
                                             loop->config.read_timeout_ms);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Read frame failed: %s", esp_err_to_name(err));
        return;
    }

    switch (opcode) {
    case XSP_WS_FRAME_OPCODE_CONTINUATION:
    case XSP_WS_FRAME_OPCODE_TEXT:
    case XSP_WS_FRAME_OPCODE_BINARY: {
        xsp_ws_client_loop_event_t evt;
        evt.type = XSP_WS_CLIENT_LOOP_EVENT_DATA_FRAME_RECEIVED;
        evt.data.data_frame_received.fin = fin;
        evt.data.data_frame_received.opcode = opcode;
        evt.data.data_frame_received.payload_size = payload_size;
        evt.data.data_frame_received.payload = loop->read_buffer;
        loop->evt_handler(loop, loop->ctx, &evt);
        break;
    }

    case XSP_WS_FRAME_OPCODE_CONNECTION_CLOSE:
        if (loop->close_sent)
            break;  // Nothing more to do.

        if (payload_size == 0) {
            loop->close_status = XSP_WS_STATUS_CLOSE_RESERVED_NO_STATUS_RECEIVED;
            xsp_ws_client_write_close_frame(loop->client, XSP_WS_STATUS_NONE, NULL,
                                            loop->config.write_timeout_ms);
        } else if (payload_size == 1) {
            loop->close_status = XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR;
            xsp_ws_client_write_close_frame(loop->client, XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR, NULL,
                                            loop->config.write_timeout_ms);
        } else {
            const unsigned char* read_buffer = loop->read_buffer;
            // Record the close status (if it's valid).
            int close_status = (int)(((int)read_buffer[0] << 8) | (int)read_buffer[1]);
            if (xsp_ws_is_valid_close_frame_status(close_status)) {
                if (xsp_ws_client_utf8_validate(payload_size - 2, read_buffer + 2)) {
                    loop->close_status = close_status;
                    // Echo the Close frame.
                    xsp_ws_client_write_frame(loop->client, true,
                                              XSP_WS_FRAME_OPCODE_CONNECTION_CLOSE, payload_size,
                                              loop->read_buffer, loop->config.write_timeout_ms);
                } else {
                    loop->close_status = XSP_WS_STATUS_CLOSE_INVALID_DATA;
                    xsp_ws_client_write_close_frame(loop->client, XSP_WS_STATUS_CLOSE_INVALID_DATA,
                                                    NULL, loop->config.write_timeout_ms);
                }
            } else {
                loop->close_status = XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR;
                xsp_ws_client_write_close_frame(loop->client, XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR,
                                                NULL, loop->config.write_timeout_ms);
            }
        }
        loop->close_sent = true;
        // Note: We'll send the close event on leaving the loop.
        break;

    case XSP_WS_FRAME_OPCODE_PING: {
        // Send pong automatically.
        xsp_ws_client_write_frame(loop->client, true, XSP_WS_FRAME_OPCODE_PONG, payload_size,
                                  loop->read_buffer, loop->config.write_timeout_ms);

        xsp_ws_client_loop_event_t evt;
        evt.type = XSP_WS_CLIENT_LOOP_EVENT_PING_RECEIVED;
        evt.data.ping_received.payload_size = payload_size;
        evt.data.ping_received.payload = loop->read_buffer;
        loop->evt_handler(loop, loop->ctx, &evt);
        break;
    }

    case XSP_WS_FRAME_OPCODE_PONG: {
        xsp_ws_client_loop_event_t evt;
        evt.type = XSP_WS_CLIENT_LOOP_EVENT_PONG_RECEIVED;
        evt.data.ping_received.payload_size = payload_size;
        evt.data.ping_received.payload = loop->read_buffer;
        loop->evt_handler(loop, loop->ctx, &evt);
        break;
    }
    }
}

static void send_message_failed(xsp_ws_client_loop_handle_t loop) {
    loop->sending_message = false;
    xsp_ws_client_loop_event_t evt;
    evt.type = XSP_WS_CLIENT_LOOP_EVENT_MESSAGE_SENT;
    evt.data.message_sent.success = false;
    loop->evt_handler(loop, loop->ctx, &evt);
}

static void do_write(xsp_ws_client_loop_handle_t loop) {
    int write_size = loop->config.max_data_frame_write_size;
    if (write_size > loop->send_size - loop->send_written)
        write_size = loop->send_size - loop->send_written;

    xsp_ws_frame_opcode_t opcode;
    if (loop->send_written == 0)
        opcode = loop->send_is_binary ? XSP_WS_FRAME_OPCODE_BINARY : XSP_WS_FRAME_OPCODE_TEXT;
    else
        opcode = XSP_WS_FRAME_OPCODE_CONTINUATION;
    const void* payload = (const char*)loop->send_message + loop->send_written;
    loop->send_written += write_size;
    bool fin = loop->send_written == loop->send_size;

    esp_err_t err = xsp_ws_client_write_frame(loop->client, fin, opcode, write_size, payload,
                                              loop->config.write_timeout_ms);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Write frame failed: %s", esp_err_to_name(err));
        send_message_failed(loop);
        return;
    }

    if (fin) {
        loop->sending_message = false;
        xsp_ws_client_loop_event_t evt;
        evt.type = XSP_WS_CLIENT_LOOP_EVENT_MESSAGE_SENT;
        evt.data.message_sent.success = true;
        loop->evt_handler(loop, loop->ctx, &evt);
    }
}

static void do_idle(xsp_ws_client_loop_handle_t loop) {
    xsp_ws_client_loop_event_t evt;
    evt.type = XSP_WS_CLIENT_LOOP_EVENT_IDLE;
    loop->evt_handler(loop, loop->ctx, &evt);
}

static bool is_closed(xsp_ws_client_loop_handle_t loop) {
    return loop->close_sent || xsp_ws_client_get_state(loop->client) != XSP_WS_CLIENT_STATE_OK;
}
#endif
