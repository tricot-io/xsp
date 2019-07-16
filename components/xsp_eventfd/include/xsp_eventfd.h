// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_EVENTFD_H_
#define XSP_EVENTFD_H_

#ifdef __cplusplus
extern "C" {
#endif

void xsp_eventfd_register();

int xsp_eventfd(unsigned initval, int flags);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // XSP_WS_CLIENT_EVENTFD_H_