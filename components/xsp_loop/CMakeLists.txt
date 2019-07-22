# Copyright 2019 Tricot Inc.
# Use of this source code is governed by the license in the LICENSE file.

set(COMPONENT_SRCS
    xsp_loop.c
)

set(COMPONENT_REQUIRES
    # TODO(vtl): This is a bit of an odd dependency.
    tcp_transport
    xsp_eventfd
)

set(COMPONENT_ADD_INCLUDEDIRS include)

register_component()
