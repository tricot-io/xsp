// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_ws_client.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"

#include "sdkconfig.h"

#if CONFIG_XSP_WS_CLIENT_WRITE_FRAME_BUFFER_SIZE < 4 || \
        CONFIG_XSP_WS_CLIENT_WRITE_FRAME_BUFFER_SIZE % 4 != 0
#error "Invalid value for CONFIG_XSP_WS_CLIENT_WRITE_FRAME_BUFFER_SIZE"
#endif

#if CONFIG_XSP_WS_CLIENT_CLOSE_DELAY_MS < 0
#error "Invalid value for CONFIG_XSP_WS_CLIENT_CLOSE_DELAY_MS"
#endif

typedef struct xsp_ws_client {
    char* url;  // http/https (instead of ws/wss.

    char* username;
    char* password;
    const char* cert_pem;  // NOTE: Must remain valid for lifetime of xsp_ws_client.
    int http_timeout_ms;
    bool disable_auto_redirect;
    int max_redirection_count;

    char* request_subprotocols;

    xsp_ws_client_state_t state;

    // Set after open (connection established).
    char* response_subprotocols;
    int overread_size;
    void* overread_data;
    int overread_consumed;
    esp_transport_handle_t transport;
} xsp_ws_client_t;

static const char TAG[] = "WS_CLIENT";

static bool is_ok_or_timeout(esp_err_t err) {
    return err == ESP_OK || err == ESP_ERR_TIMEOUT;
}

static bool can_write(xsp_ws_client_state_t state) {
    return state == XSP_WS_CLIENT_STATE_OK || state == XSP_WS_CLIENT_STATE_FAILED;
}

static bool can_read(xsp_ws_client_state_t state) {
    return state == XSP_WS_CLIENT_STATE_OK;
}

static esp_err_t poll_result_to_esp_err(int poll_result) {
    switch (poll_result) {
    case 0:
        return ESP_ERR_TIMEOUT;
    case -1:
        return ESP_FAIL;
    default:
        return ESP_OK;
    }
}

xsp_ws_client_handle_t xsp_ws_client_init(const xsp_ws_client_config_t* config) {
    xsp_ws_client_handle_t client = NULL;

    if (!config || !config->url) {
        ESP_LOGE(TAG, "Invalid config");
        goto fail;
    }

    client = (xsp_ws_client_handle_t)calloc(1, sizeof(xsp_ws_client_t));
    if (!client)
        goto fail;

    // We'll substitute http for ws and https for wss.
    client->url = (char*)malloc(strlen(config->url) + 2 + 1);
    if (!client->url)
        goto fail;
    if (strncasecmp(config->url, "ws:", 3) == 0) {
        strcpy(client->url, "http");
        strcpy(client->url + 4, config->url + 2);
    } else if (strncasecmp(config->url, "wss:", 4) == 0) {
        strcpy(client->url, "https");
        strcpy(client->url + 5, config->url + 3);
    } else {
        strcpy(client->url, config->url);
    }

    if (config->username) {
        client->username = strdup(config->username);
        if (!client->username)
            goto fail;
    }
    if (config->password) {
        client->password = strdup(config->password);
        if (!client->password)
            goto fail;
    }
    // Note: Don't strdup cert_pem.
    client->cert_pem = config->cert_pem;
    client->http_timeout_ms = config->http_timeout_ms;
    client->disable_auto_redirect = config->disable_auto_redirect;
    client->max_redirection_count = config->max_redirection_count;

    if (config->subprotocols) {
        client->request_subprotocols = strdup(config->subprotocols);
        if (!client->request_subprotocols)
            goto fail;
    }

    client->state = XSP_WS_CLIENT_STATE_CLOSED;

    return client;

fail:
    if (client)
        xsp_ws_client_cleanup(client);  // Ignore any error.
    return NULL;
}

esp_err_t xsp_ws_client_cleanup(xsp_ws_client_handle_t client) {
    if (!client)
        return ESP_FAIL;

    if (client->transport)
        esp_transport_destroy(client->transport);  // Ignore any error.
    free(client->overread_data);
    free(client->response_subprotocols);
    free(client->request_subprotocols);
    free(client->password);
    free(client->username);
    free(client->url);
    free(client);
    return ESP_OK;
}

// Used during the HTTP handshake phase.
typedef struct http_connection_context {
    bool enabled;
    const char* expected_sec_websocket_accept;
    bool allow_sec_websocket_protocol;

    bool encountered_error;
    bool got_upgrade;
    bool got_connection;
    bool got_sec_websocket_accept;
    char* got_sec_websocket_protocol;
} http_connection_context_t;

// NOTE: Even though event handlers return esp_err_t, esp_http_client ignores it.
static esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    http_connection_context_t* ctx = (http_connection_context_t*)evt->user_data;
    if (!ctx) {
        ESP_LOGE(TAG, "Missing user data");
        return ESP_FAIL;
    }

    if (!ctx->enabled)
        return ESP_OK;

    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);

        if (ctx->encountered_error)
            return ESP_FAIL;  // Already got an error, so fail everything else.

        if (strcasecmp(evt->header_key, "Upgrade") == 0) {
            if (ctx->got_upgrade) {
                ESP_LOGE(TAG, "Already got Upgrade header");
                ctx->encountered_error = true;
                return ESP_FAIL;
            }
            if (strcasecmp(evt->header_value, "websocket") != 0) {
                ESP_LOGE(TAG, "Expected Upgrade: websocket; got %s", evt->header_value);
                ctx->encountered_error = true;
                return ESP_FAIL;
            }
            ctx->got_upgrade = true;
            return ESP_OK;
        }
        if (strcasecmp(evt->header_key, "Connection") == 0) {
            if (ctx->got_connection) {
                ESP_LOGE(TAG, "Already got Connection header");
                ctx->encountered_error = true;
                return ESP_FAIL;
            }
            if (strcasecmp(evt->header_value, "Upgrade") != 0) {
                ESP_LOGE(TAG, "Expected Connection: Upgrade; got %s", evt->header_value);
                ctx->encountered_error = true;
                return ESP_FAIL;
            }
            ctx->got_connection = true;
            return ESP_OK;
        }
        if (strcasecmp(evt->header_key, "Sec-WebSocket-Accept") == 0) {
            if (ctx->got_sec_websocket_accept) {
                ESP_LOGE(TAG, "Already got Sec-WebSocket-Accept header");
                ctx->encountered_error = true;
                return ESP_FAIL;
            }
            if (strcasecmp(evt->header_value, ctx->expected_sec_websocket_accept) != 0) {
                ESP_LOGE(TAG, "Expected Sec-WebSocket-Accept: %s; got %s",
                         ctx->expected_sec_websocket_accept, evt->header_value);
                ctx->encountered_error = true;
                return ESP_FAIL;
            }
            ctx->got_sec_websocket_accept = true;
            return ESP_OK;
        }
        if (strcasecmp(evt->header_key, "Sec-WebSocket-Protocol") == 0) {
            if (!ctx->allow_sec_websocket_protocol) {
                ESP_LOGE(TAG, "Got Sec-WebSocket-Protocol header, but didn't send it");
                ctx->encountered_error = true;
                return ESP_FAIL;
            }
            if (!!ctx->got_sec_websocket_protocol) {
                ESP_LOGE(TAG, "Already got Sec-WebSocket-Protocol header");
                ctx->encountered_error = true;
                return ESP_FAIL;
            }
            ctx->got_sec_websocket_protocol = strdup(evt->header_value);
            if (!ctx->got_sec_websocket_protocol) {
                ESP_LOGE(TAG, "Allocation failed");
                ctx->encountered_error = true;
                return ESP_FAIL;
            }
            return ESP_OK;
        }
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, size=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        ESP_LOGE(TAG, "HTTP disconnected");
        ctx->encountered_error = true;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t xsp_ws_client_open(xsp_ws_client_handle_t client) {
    if (!client)
        return ESP_ERR_INVALID_ARG;
    if (client->transport || client->state != XSP_WS_CLIENT_STATE_CLOSED)
        return ESP_ERR_INVALID_STATE;

    esp_err_t err;
    esp_http_client_handle_t http_client = NULL;

    // Base-64 encoding of 16 random bytes (with terminating null).
    // Size = ceil(16 * 8 / 24) * 4 + 1 = 25.
    char sec_websocket_key[25];
    {
        unsigned char sec_websocket_key_bytes[16];
        // This shouldn't fail.
        getrandom(sec_websocket_key_bytes, sizeof(sec_websocket_key_bytes), 0);

        // NOTE: mbedtls_base64_encode() null-terminates its output.
        // This shouldn't fail.
        size_t ignored;
        mbedtls_base64_encode((unsigned char*)sec_websocket_key, sizeof(sec_websocket_key),
                              &ignored, sec_websocket_key_bytes, sizeof(sec_websocket_key_bytes));
    }

    // Base-64 encoding of SHA-1 hash (160 bits).
    // Size = ceil(160 / 24) * 4 + 1 = 29.
    char expected_sec_websocket_accept[29];
    {
        static const char kRfc6455Guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

        // We'll hash the concatenation of sec_websocket_key with kGuid.
        unsigned char to_hash[sizeof(sec_websocket_key) - 1 + sizeof(kRfc6455Guid) - 1];
        memcpy(to_hash, sec_websocket_key, sizeof(sec_websocket_key) - 1);
        memcpy(to_hash + sizeof(sec_websocket_key) - 1, kRfc6455Guid, sizeof(kRfc6455Guid) - 1);

        // The SHA-1 hash.
        // Size = 160 / 8 = 20.
        unsigned char hash[20];
        mbedtls_sha1_ret(to_hash, sizeof(to_hash), hash);  // This shouldn't fail.

        // NOTE: mbedtls_base64_encode() null-terminates its output.
        // This shouldn't fail.
        size_t ignored;
        mbedtls_base64_encode((unsigned char*)expected_sec_websocket_accept,
                              sizeof(expected_sec_websocket_accept), &ignored, hash, sizeof(hash));
    }

    http_connection_context_t ctx = {
            .expected_sec_websocket_accept = expected_sec_websocket_accept,
            .allow_sec_websocket_protocol = !!client->request_subprotocols,
    };
    esp_http_client_config_t http_config = {
            .url = client->url,
            .username = client->username,
            .password = client->password,
            .cert_pem = client->cert_pem,
            .timeout_ms = client->http_timeout_ms,
            .disable_auto_redirect = client->disable_auto_redirect,
            .max_redirection_count = client->max_redirection_count,
            .event_handler = http_event_handler,
            .user_data = &ctx,
    };
    http_client = esp_http_client_init(&http_config);
    if (!http_client) {
        ESP_LOGE(TAG, "HTTP client initialization failed");
        err = ESP_FAIL;
        goto done;
    }

    err = esp_http_client_set_header(http_client, "Upgrade", "websocket");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set header failed: %s", esp_err_to_name(err));
        goto done;
    }
    err = esp_http_client_set_header(http_client, "Connection", "Upgrade");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set header failed: %s", esp_err_to_name(err));
        goto done;
    }
    err = esp_http_client_set_header(http_client, "Sec-WebSocket-Key", sec_websocket_key);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set header failed: %s", esp_err_to_name(err));
        goto done;
    }
    if (client->request_subprotocols) {
        err = esp_http_client_set_header(http_client, "Sec-WebSocket-Protocol",
                                         client->request_subprotocols);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "set header failed: %s", esp_err_to_name(err));
            goto done;
        }
    }
    err = esp_http_client_set_header(http_client, "Sec-WebSocket-Version", "13");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set header failed: %s", esp_err_to_name(err));
        goto done;
    }

    ctx.enabled = true;
    err = esp_http_client_perform(http_client);
    ctx.enabled = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        goto done;
    }
    if (ctx.encountered_error) {
        err = ESP_FAIL;
        goto done;
    }
    int http_status = esp_http_client_get_status_code(http_client);
    if (http_status != 101) {
        ESP_LOGE(TAG, "Didn't get HTTP 101; got %d", http_status);
        err = ESP_FAIL;
        goto done;
    }
    // Note: Even if we sent Sec-WebSocket-Protocol, it's acceptable (and even meaningful) for the
    // server to not send it.
    if (!ctx.got_upgrade || !ctx.got_connection || !ctx.got_sec_websocket_accept) {
        ESP_LOGE(TAG, "Didn't get expected headers");
        err = ESP_FAIL;
        goto done;
    }
    client->response_subprotocols = ctx.got_sec_websocket_protocol;
    ctx.got_sec_websocket_protocol = NULL;

    int overread_size;
    const void* overread_data = NULL;
    err = esp_http_client_get_overread_data(http_client, &overread_size, &overread_data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get overread data");
        err = ESP_FAIL;
        goto done;
    }
    if (overread_size > 0) {
        client->overread_size = overread_size;
        client->overread_data = malloc((size_t)overread_size);
        memcpy(client->overread_data, overread_data, (size_t)overread_size);
    }

    // Our final success is determined by this.
    err = esp_http_client_extract_transport(http_client, &client->transport);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to extract transport");
        if (client->transport) {
            // This shouldn't happen.
            ESP_LOGE(TAG, "... but still have transport!!!");
            esp_transport_destroy(client->transport);  // Ignore any error.
            client->transport = NULL;
        }
        goto done;
    }
    client->state = XSP_WS_CLIENT_STATE_OK;

done:
    if (http_client)
        esp_http_client_cleanup(http_client);  // Ignore any error.
    free(ctx.got_sec_websocket_protocol);

    return err;
}

esp_err_t xsp_ws_client_close(xsp_ws_client_handle_t client) {
    if (!client)
        return ESP_ERR_INVALID_ARG;
    client->state = XSP_WS_CLIENT_STATE_CLOSED;
    free(client->response_subprotocols);
    client->response_subprotocols = NULL;
    if (client->transport) {
        // TODO(vtl): This delay is to try to ensure that everything we wrote gets sent out. Surely
        // there's a better way to do this....
        vTaskDelay((CONFIG_XSP_WS_CLIENT_CLOSE_DELAY_MS + (portTICK_PERIOD_MS - 1)) /
                   portTICK_PERIOD_MS);
        esp_transport_close(client->transport);    // Ignore any error.
        esp_transport_destroy(client->transport);  // Ignore any error.
        client->transport = NULL;
    }
    if (client->overread_data) {
        free(client->overread_data);
        client->overread_data = NULL;
    }
    return ESP_OK;
}

xsp_ws_client_state_t xsp_ws_client_get_state(xsp_ws_client_handle_t client) {
    return client->state;
}

const char* xsp_ws_client_get_response_subprotocols(xsp_ws_client_handle_t client) {
    return client->response_subprotocols;
}

int xsp_ws_client_get_select_fd(xsp_ws_client_handle_t client) {
    if (!client)
        return -1;
    if (!client->transport)
        return -1;
    if (!can_read(client->state) || !can_write(client->state))
        return -1;
    return esp_transport_get_select_fd(client->transport);
}

esp_err_t xsp_ws_client_poll_write(xsp_ws_client_handle_t client, int timeout_ms) {
    if (!client || timeout_ms < 0)
        return ESP_ERR_INVALID_ARG;
    if (!client->transport)
        return ESP_ERR_INVALID_STATE;
    if (!can_write(client->state))
        return ESP_FAIL;

    esp_err_t err = poll_result_to_esp_err(esp_transport_poll_write(client->transport, timeout_ms));
    if (!is_ok_or_timeout(err))
        client->state = XSP_WS_CLIENT_STATE_FAILED_NO_CLOSE;
    return err;
}

static bool write_frame_header(esp_transport_handle_t transport,
                               bool fin,
                               xsp_ws_frame_opcode_t opcode,
                               int payload_size,
                               unsigned char masking_key[4],
                               int timeout_ms) {
    // See RFC6455.
    // Max size = (flags) + (mask flag/payload size) + (ext. payload size) + (masking key)
    //          = 1 + 1 + 8 + 4 = 14.
    unsigned char header[14];
    int size = 0;

    header[size++] = (unsigned char)((fin ? 0x80 : 0) | opcode);
    if (payload_size <= 125) {
        // Always masked.
        header[size++] = (unsigned char)(0x80 | payload_size);
    } else if (payload_size <= 0xffff) {
        // Always masked.
        header[size++] = (unsigned char)(0x80 | 126);
        // Network byte order (big endian) 16-bit value.
        header[size++] = (unsigned char)((payload_size >> 8) & 0xff);
        header[size++] = (unsigned char)(payload_size & 0xff);
    } else {
        // Always masked.
        header[size++] = (unsigned char)(0x80 | 127);
        // Network byte order (big endian) 64-bit value.
        header[size++] = 0;
        header[size++] = 0;
        header[size++] = 0;
        header[size++] = 0;
        header[size++] = (unsigned char)((payload_size >> 24) & 0xff);
        header[size++] = (unsigned char)((payload_size >> 16) & 0xff);
        header[size++] = (unsigned char)((payload_size >> 8) & 0xff);
        header[size++] = (unsigned char)(payload_size & 0xff);
    }
    // Always masked, so always have masking key.
    memcpy(&header[size], masking_key, 4);
    size += 4;

    // NOTE: If esp_transport_write() returns 0, we can't tell if it's due to timeout or due to some
    // other failure.
    return esp_transport_write(transport, (const char*)header, size, timeout_ms) == size;
}

esp_err_t xsp_ws_client_write_frame(xsp_ws_client_handle_t client,
                                    bool fin,
                                    xsp_ws_frame_opcode_t opcode,
                                    int payload_size,
                                    const void* payload,
                                    int timeout_ms) {
    if (!client || payload_size < 0 || (payload_size > 0 && !payload) || timeout_ms < 0)
        return ESP_ERR_INVALID_ARG;
    if (!client->transport)
        return ESP_ERR_INVALID_STATE;
    if (!can_write(client->state))
        return ESP_FAIL;

    unsigned char masking_key[4];
    // This shouldn't fail.
    getrandom(masking_key, sizeof(masking_key), 0);

    if (!write_frame_header(client->transport, fin, opcode, payload_size, masking_key,
                            timeout_ms)) {
        // We don't know why it failed, so we have to assume that the transport is bad.
        client->state = XSP_WS_CLIENT_STATE_FAILED_NO_CLOSE;
        return ESP_FAIL;
    }

    unsigned char write_buf[CONFIG_XSP_WS_CLIENT_WRITE_FRAME_BUFFER_SIZE];
    const unsigned char* src = (const unsigned char*)payload;
    while (payload_size > 0) {
        int write_size =
                (payload_size > (int)sizeof(write_buf)) ? (int)sizeof(write_buf) : payload_size;
        // TODO(vtl): This could be more efficient; the optimizer probably isn't smart enough to
        // "vectorize".
        for (int i = 0; i < write_size; i++, src++)
            write_buf[i] = *src ^ masking_key[i % 4];

        if (esp_transport_write(client->transport, (const char*)write_buf, write_size,
                                timeout_ms) != write_size) {
            client->state = XSP_WS_CLIENT_STATE_FAILED_NO_CLOSE;
            return ESP_FAIL;
        }

        payload_size -= write_size;
    }

    return ESP_OK;
}

esp_err_t xsp_ws_client_write_close_frame(xsp_ws_client_handle_t client,
                                          int status,
                                          const char* reason,
                                          int timeout_ms) {
    size_t reason_size = reason ? strlen(reason) : 0;
    if (status) {
        if (!xsp_ws_is_valid_close_frame_status(status))
            return ESP_ERR_INVALID_ARG;
        if (reason_size > 123)
            return ESP_ERR_INVALID_ARG;
    } else {
        // No payload, so can't send a reason.
        if (reason_size > 0)
            return ESP_ERR_INVALID_ARG;
    }

    unsigned char buf[125];
    int size = 0;
    if (status) {
        buf[size++] = (unsigned char)((status >> 8) & 0xff);
        buf[size++] = (unsigned char)(status & 0xff);
        if (reason_size > 0) {
            memcpy(buf + size, reason, reason_size);
            size += reason_size;
        }
    }
    return xsp_ws_client_write_frame(client, true, XSP_WS_FRAME_OPCODE_CONNECTION_CLOSE, size, buf,
                                     timeout_ms);
}

esp_err_t xsp_ws_client_poll_read(xsp_ws_client_handle_t client, int timeout_ms) {
    if (!client || timeout_ms < 0)
        return ESP_ERR_INVALID_ARG;
    if (!client->transport)
        return ESP_ERR_INVALID_STATE;
    if (!can_read(client->state))
        return ESP_FAIL;

    if (client->overread_consumed < client->overread_size)
        return ESP_OK;
    return poll_result_to_esp_err(esp_transport_poll_read(client->transport, timeout_ms));
}

static int read_data(xsp_ws_client_handle_t client, char* data, int size, int timeout_ms) {
    int size_read = 0;
    if (client->overread_data) {
        int overread_left = client->overread_size - client->overread_consumed;
        size_read = (size <= overread_left) ? size : overread_left;
        memcpy(data, client->overread_data + client->overread_consumed, (size_t)size_read);
        client->overread_consumed += size_read;
        if (client->overread_consumed == client->overread_size) {
            free(client->overread_data);
            client->overread_data = NULL;
        }
    }

    // Note: This also handles the size == 0 case.
    while (size_read < size) {
        int result = esp_transport_read(client->transport, data + size_read, size - size_read,
                                        timeout_ms);
        if (result <= 0) {
            if (size_read > 0)
                break;
            return -1;
        }
        size_read += result;
    }
    return size_read;
}

esp_err_t xsp_ws_client_read_frame(xsp_ws_client_handle_t client,
                                   bool* fin,
                                   xsp_ws_frame_opcode_t* opcode,
                                   int payload_buffer_size,
                                   void* payload_buffer,
                                   int* payload_size,
                                   int timeout_ms) {
    if (!client || !fin || !opcode || payload_buffer_size < 0 ||
        (payload_buffer_size > 0 && !payload_buffer) || !payload_size || timeout_ms < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!client->transport)
        return ESP_ERR_INVALID_STATE;
    if (!can_read(client->state))
        return ESP_FAIL;

    unsigned char header1[2];
    if (read_data(client, (char*)&header1, (int)sizeof(header1), timeout_ms) != sizeof(header1)) {
        // We don't know why it failed, so we have to assume that the transport is bad.
        client->state = XSP_WS_CLIENT_STATE_FAILED_NO_CLOSE;
        return ESP_FAIL;
    }
    if ((header1[0] & 0x70) != 0)                                 // Reserved bits are set.
        client->state = XSP_WS_CLIENT_STATE_FAILED;               // But keep going.
    if (!xsp_ws_is_valid_frame_opcode((int)(header1[0] & 0x0f)))  // Invalid opcode.
        client->state = XSP_WS_CLIENT_STATE_FAILED;               // But keep going.
    xsp_ws_frame_opcode_t opcode_value = (xsp_ws_frame_opcode_t)(header1[0] & 0x0f);
    bool is_control_frame = xsp_ws_is_control_frame_opcode(opcode_value);
    bool fin_value = (header1[0] & 0x80) != 0;
    if (is_control_frame && !fin_value)              // Control frames must not be fragmented.
        client->state = XSP_WS_CLIENT_STATE_FAILED;  // But keep going.
    *fin = fin_value;
    *opcode = opcode_value;
    if ((header1[1] & 0x80) != 0) {  // Frames from server must not be masked.
        // Don't keep going since we don't know how to unmask.
        client->state = XSP_WS_CLIENT_STATE_FAILED_NO_CLOSE;
        return ESP_FAIL;
    }
    int size = header1[1];

    if (size == 126) {
        if (is_control_frame)                            // Control frames are at most 125 bytes.
            client->state = XSP_WS_CLIENT_STATE_FAILED;  // But keep going.

        unsigned char header2[2];
        if (read_data(client, (char*)&header2, (int)sizeof(header2), timeout_ms) !=
            sizeof(header2)) {
            client->state = XSP_WS_CLIENT_STATE_FAILED_NO_CLOSE;
            return ESP_FAIL;
        }
        size = ((int)header2[0] << 8) | (int)header2[1];
        if (size <= 125)                                 // Not minimal encoding of size.
            client->state = XSP_WS_CLIENT_STATE_FAILED;  // But keep going.
    } else if (size == 127) {
        if (is_control_frame)                            // Control frames are at most 125 bytes.
            client->state = XSP_WS_CLIENT_STATE_FAILED;  // But keep going.

        unsigned char header2[8];
        if (read_data(client, (char*)&header2, (int)sizeof(header2), timeout_ms) !=
            sizeof(header2)) {
            client->state = XSP_WS_CLIENT_STATE_FAILED_NO_CLOSE;
            return ESP_FAIL;
        }
        if (header2[0] != 0 || header2[1] != 0 || header2[2] != 0 || header2[3] != 0 ||
            (header2[4] & 0x80) != 0) {  // Too big!
            client->state = XSP_WS_CLIENT_STATE_FAILED_NO_CLOSE;
            return ESP_FAIL;
        }
        size = ((int)header2[4] << 24) | ((int)header2[5] << 16) | ((int)header2[6] << 8) |
               (int)header2[7];
        if (size <= 0xffff)                              // Not minimal encoding of size.
            client->state = XSP_WS_CLIENT_STATE_FAILED;  // But keep going.
    }
    *payload_size = size;
    if (size > payload_buffer_size) {
        // TODO(vtl): Possibly, we should just read/discard the remaining data, and report that the
        // frame was too big. (Then we'd be able to close properly, with status 1009, I guess.)
        client->state = XSP_WS_CLIENT_STATE_FAILED_NO_CLOSE;
        return ESP_FAIL;
    }

    if (read_data(client, (char*)payload_buffer, size, timeout_ms) != size) {
        client->state = XSP_WS_CLIENT_STATE_FAILED_NO_CLOSE;
        return ESP_FAIL;
    }

    if (client->state != XSP_WS_CLIENT_STATE_OK)
        return ESP_FAIL;

    return ESP_OK;
}
