# Copyright 2019 Tricot Inc.
# Use of this source code is governed by the license in the LICENSE file.

# This file should be sourced.

TOOLCHAIN_DIR="${HOME}/xtensa-esp32-elf"
TOOLCHAIN_BIN_DIR="${TOOLCHAIN_DIR}/bin"

ESPIDF_DIR="${HOME}/esp-idf"
ESPIDF_TOOLS_DIR="${ESPIDF_DIR}/tools"

export IDF_PATH="${ESPIDF_DIR}"
export PATH="${ESPIDF_TOOLS_DIR}:${TOOLCHAIN_BIN_DIR}:${PATH}"
