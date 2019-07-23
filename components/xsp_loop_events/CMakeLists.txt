# Copyright 2019 Tricot Inc.
# Use of this source code is governed by the license in the LICENSE file.

set(COMPONENT_SRCS
    xsp_loop_events.c
)

set(COMPONENT_REQUIRES
    xsp_eventfd
    xsp_loop
)

set(COMPONENT_ADD_INCLUDEDIRS include)

register_component()
