# Copyright 2019 Tricot Inc.
# Use of this source code is governed by the license in the LICENSE file.

set(COMPONENT_SRCS
    main.c
    verify_stdatomic.c
    verify_cxx_atomic.cc
)

set(COMPONENT_ADD_INCLUDEDIRS ".")

register_component()
