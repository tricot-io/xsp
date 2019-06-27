#!/bin/bash
# Copyright 2019 Tricot Inc.
# Use of this source code is governed by the license in the LICENSE file.

# Note: This script assumes that the environment has been set up.

TARGET_DIRS=(
    examples/atomic8_test
    examples/ws_client
    examples/ws_client_test
)

set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE}")/.."; pwd)"

echo "*** Root dir: ${ROOT_DIR}"

for TARGET_DIR in "${TARGET_DIRS[@]}"; do
    echo
    echo "*** Building: ${TARGET_DIR}"
    echo

    cd "${ROOT_DIR}"
    cd "${TARGET_DIR}"

    idf.py build

    echo
    echo "*** Finished building: ${TARGET_DIR}"
done
