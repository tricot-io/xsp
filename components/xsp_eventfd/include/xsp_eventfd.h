// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_EVENTFD_H_
#define XSP_EVENTFD_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// TODO(vtl)
// #define XSP_EVENTFD_SEMAPHORE 1
#define XSP_EVENTFD_NONBLOCK 2

#define XSP_EVENTFD_IOCTL_BASE (0x45464400)  // (Big-endian) 'E', 'F', 'D', ....

// Gets a "handle" for the event FD, which remains valid so long as the FD remains open. The first
// (and only) ioctl argument is the out parameter, an xsp_eventfd_handle_t*.
#define XSP_EVENTFD_IOCTL_GET_HANDLE (XSP_EVENTFD_IOCTL_BASE + 0)

void xsp_eventfd_register();

int xsp_eventfd(unsigned initval, int flags);

typedef struct xsp_eventfd_struct* xsp_eventfd_handle_t;

// Like using `write()`, but may be called from an ISR and never blocks. Returns true on success
// (false if it would block).
bool xsp_eventfd_write(xsp_eventfd_handle_t efd, uint64_t to_add);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // XSP_EVENTFD_H_
