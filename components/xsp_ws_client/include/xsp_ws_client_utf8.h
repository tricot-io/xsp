// Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
// Modifications copyright 2019 Tricot Inc.

#ifndef XSP_WS_CLIENT_UTF8_H_
#define XSP_WS_CLIENT_UTF8_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XSP_WS_CLIENT_UTF8_ACCEPT 0
#define XSP_WS_CLIENT_UTF8_REJECT 12

// Validates data (possibly in multiple fragments) to be UTF-8 from a given start state; the initial
// state should be XSP_WS_CLIENT_UTF8_ACCEPT. Returns the new state: the data (from all runs of data
// thus far, concatenated) is valid UTF-8 if the state is XSP_WS_CLIENT_UTF8_ACCEPT, while the data
// is (and will always be, even with subsequent data) not valid UTF-8 if the state is
// XSP_WS_CLIENT_UTF8_REJECT; otherwise some other state is returned.
int xsp_ws_client_utf8_validate_state(int start_state, int size, const void* data);

// TODO(vtl): Possibly we should add "static" when compiling C++.
inline bool xsp_ws_client_utf8_validate(int size, const void* data) {
    return xsp_ws_client_utf8_validate_state(XSP_WS_CLIENT_UTF8_ACCEPT, size, data) ==
            XSP_WS_CLIENT_UTF8_ACCEPT;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // XSP_WS_CLIENT_UTF8_H_
