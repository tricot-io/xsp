# Copyright 2019 Tricot Inc.
# Use of this source code is governed by the license in the LICENSE file.

set(COMPONENT_SRCS
    loop.cc
    ws_client_handler.cc
)

set(COMPONENT_REQUIRES
    xsp_ws_client
)

set(COMPONENT_ADD_INCLUDEDIRS
    include
)

register_component()
