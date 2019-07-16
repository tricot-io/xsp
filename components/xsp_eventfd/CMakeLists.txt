# Copyright 2019 Tricot Inc.
# Use of this source code is governed by the license in the LICENSE file.

set(COMPONENT_SRCS
    xsp_eventfd.c
)

set(COMPONENT_REQUIRES
    vfs
)

set(COMPONENT_ADD_INCLUDEDIRS include)

register_component()
