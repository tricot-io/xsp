# Copyright 2019 Tricot Inc.
# Use of this source code is governed by the license in the LICENSE file.

set(COMPONENT_SRCS
    xsp_ws_client.c
    xsp_ws_client_defrag.c
    xsp_ws_client_handler.c
    xsp_ws_client_types.c
    xsp_ws_client_utf8.c
)

set(COMPONENT_REQUIRES
    esp_http_client
    tcp_transport
    xsp_loop
)

set(COMPONENT_ADD_INCLUDEDIRS include)

register_component()
