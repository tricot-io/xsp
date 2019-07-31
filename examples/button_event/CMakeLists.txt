# Copyright 2019 Tricot Inc.
# Use of this source code is governed by the license in the LICENSE file.

cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include("$ENV{IDF_PATH}/tools/cmake/project.cmake")
project(xsp-loop-events-example)
