// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_ws_client_defrag.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "xsp_ws_client_utf8.h"

typedef struct xsp_ws_client_defrag {
    xsp_ws_client_defrag_config_t config;

    esp_err_t message_error;
    xsp_ws_frame_opcode_t message_opcode;  // Opcode of first frame.
    int message_utf8_state;
    int message_size;
    void* message_data;
} xsp_ws_client_defrag_t;

static const char TAG[] = "WS_CLIENT_DEFRAG";

xsp_ws_client_defrag_handle_t xsp_ws_client_defrag_init(
        const xsp_ws_client_defrag_config_t* config) {
    if (!config) {
        ESP_LOGE(TAG, "Invalid argument");
        return NULL;
    }

    if (config->max_message_size < 1) {
        ESP_LOGE(TAG, "Invalid config");
        return NULL;
    }

    xsp_ws_client_defrag_handle_t defrag =
            (xsp_ws_client_defrag_handle_t)malloc(sizeof(xsp_ws_client_defrag_t));
    if (!defrag) {
        ESP_LOGE(TAG, "Allocation failed");
        return NULL;
    }

    defrag->config = *config;

    defrag->message_error = ESP_OK;
    defrag->message_opcode = XSP_WS_FRAME_OPCODE_CONTINUATION;
    defrag->message_utf8_state = XSP_WS_CLIENT_UTF8_ACCEPT;
    defrag->message_size = 0;
    defrag->message_data = NULL;

    return defrag;
}

esp_err_t xsp_ws_client_defrag_cleanup(xsp_ws_client_defrag_handle_t defrag) {
    if (!defrag)
        return ESP_FAIL;

    free(defrag->message_data);
    free(defrag);
    return ESP_OK;
}

esp_err_t xsp_ws_client_defrag_on_data_frame(xsp_ws_client_defrag_handle_t defrag,
                                             bool fin,
                                             xsp_ws_frame_opcode_t opcode,
                                             int payload_size,
                                             const void* payload,
                                             bool* done,
                                             xsp_ws_frame_opcode_t* message_opcode,
                                             int* message_size,
                                             void** message_data) {
    if (!defrag || !xsp_ws_is_data_frame_opcode(opcode) || payload_size < 0 || (payload_size != 0 &&
            !payload) || !done || !message_opcode || !message_size || !message_data)
        return ESP_ERR_INVALID_ARG;

    *done = fin;
    *message_size = 0;
    *message_data = NULL;

    if (defrag->message_error != ESP_OK)  // Error state;
        goto fail;  // Continue returning error until fin.

    if (defrag->message_opcode == XSP_WS_FRAME_OPCODE_CONTINUATION) {  // First frame.
        if (opcode == XSP_WS_FRAME_OPCODE_CONTINUATION) {
            defrag->message_error = ESP_FAIL;
            goto fail;
        }
        defrag->message_opcode = opcode;
        defrag->message_utf8_state = XSP_WS_CLIENT_UTF8_ACCEPT;
    } else {  // Continuation frame.
        if (opcode != XSP_WS_FRAME_OPCODE_CONTINUATION) {
            defrag->message_error = ESP_FAIL;
            goto fail;
        }
    }

    if (defrag->message_opcode == XSP_WS_FRAME_OPCODE_TEXT) {
        defrag->message_utf8_state = xsp_ws_client_utf8_validate_state(defrag->message_utf8_state ,
                                                                       payload_size, payload);
        if (defrag->message_utf8_state == XSP_WS_CLIENT_UTF8_REJECT ||
                (fin && defrag->message_utf8_state != XSP_WS_CLIENT_UTF8_ACCEPT)) {
            defrag->message_error = ESP_ERR_INVALID_RESPONSE;
            goto fail;
        }
    }

    // TODO(vtl): Write this to avoid possible overflow, though in practice overflow won't happen.
    int new_size = defrag->message_size + payload_size;
    if (new_size > defrag->config.max_message_size) {  // Message too big!
        defrag->message_error = ESP_ERR_INVALID_SIZE;
        goto fail;
    }

    char* new_data = NULL;
    if (new_size > 0) {
        new_data = (char*)realloc(defrag->message_data, new_size);
        if (!new_data) {  // Allocation failed.
            defrag->message_error = ESP_ERR_NO_MEM;
            goto fail;
        }
    }

    // Success!
    if (payload_size > 0)
        memcpy(new_data + defrag->message_size, payload, payload_size);
    defrag->message_size = new_size;
    defrag->message_data = (void*)new_data;

    if (fin) {
        *message_opcode = defrag->message_opcode;
        *message_size = defrag->message_size;
        *message_data = defrag->message_data;
        defrag->message_opcode = XSP_WS_FRAME_OPCODE_CONTINUATION;
        defrag->message_size = 0;
        defrag->message_data = NULL;
    }

    return ESP_OK;

fail:
    defrag->message_opcode = XSP_WS_FRAME_OPCODE_CONTINUATION;
    defrag->message_size = 0;
    free(defrag->message_data);
    defrag->message_data = NULL;

    esp_err_t rv = defrag->message_error;  // defrag->message_error may be reset below.
    if (fin)
        defrag->message_error = ESP_OK;
    return rv;
}
