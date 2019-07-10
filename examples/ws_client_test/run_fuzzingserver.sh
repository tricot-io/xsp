#!/bin/sh
# Copyright 2019 Tricot Inc.
# Use of this source code is governed by the license in the LICENSE file.

docker run -it --rm \
    -v "${PWD}/config:/config" \
    -v "${PWD}/reports:/reports" \
    -p 9001:9001 \
    --name fuzzingserver \
    $* \
    crossbario/autobahn-testsuite
