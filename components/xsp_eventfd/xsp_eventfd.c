// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_eventfd.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"

#include "sdkconfig.h"

//FIXME make config
#define MAX_NUM_EVENTFD 4

#define LOCK(mux) portENTER_CRITICAL(mux)
#define UNLOCK(mux) portEXIT_CRITICAL(mux)

// NOTE: xsp_eventfd_ctx_t's mux precedes xsp_eventfd_t's mux in the (acquisition) order.

typedef struct xsp_eventfd_struct {
    portMUX_TYPE mux;
    unsigned refcount;
    uint64_t value;
    bool nonblock;
} xsp_eventfd_t;

typedef struct xsp_eventfd_ctx {
    portMUX_TYPE mux;
    size_t num_eventfd;
    struct {
        int fd;  // Should be initialized to -1.
        xsp_eventfd_t* efd;
    } eventfds[MAX_NUM_EVENTFD];
} xsp_eventfd_ctx_t;

static esp_vfs_id_t g_eventfd_vfs_id = -1;
static xsp_eventfd_ctx_t* g_eventfd_ctx = NULL;

#if 0
static void efd_ref_locked(xsp_eventfd_t* efd) {
    efd->refcount++;
}
#endif

// Note: `efd` should be locked to call this, but it will be unlocked afterwards.
static void efd_unref_locked(xsp_eventfd_t* efd) {
    if (efd->refcount == 1) {
        UNLOCK(&efd->mux);
        free(efd);
    } else {
        efd->refcount--;
        UNLOCK(&efd->mux);
    }
}

static xsp_eventfd_t* efd_lookup_locked(xsp_eventfd_ctx_t* ctx, int fd, size_t* idx) {
    if (fd < 0)
        return NULL;

    for (size_t i = 0; i < MAX_NUM_EVENTFD; i++) {
        if (ctx->eventfds[i].fd == fd) {
            if (idx)
                *idx = i;
            return ctx->eventfds[i].efd;
        }
    }

    return NULL;
}

// Looks up the given FD and returns its `xsp_eventfd_t` with its mux acquired (but without
// incrementing the refcount -- if the caller needs to persist the pointer after unlocking, it must
// increment the refcount). On failure, sets errno and returns null.
static xsp_eventfd_t* efd_lookup(void* raw_ctx, int fd) {
    xsp_eventfd_ctx_t* ctx = (xsp_eventfd_ctx_t*)raw_ctx;
    LOCK(&ctx->mux);

    xsp_eventfd_t* efd = efd_lookup_locked(raw_ctx, fd, NULL);
    if (!efd) {
        UNLOCK(&ctx->mux);
        errno = EBADF;  // TODO(vtl): This shouldn't happen, I think?
        return NULL;
    }

    LOCK(&efd->mux);
    UNLOCK(&ctx->mux);
    return efd;
}

static ssize_t efd_write_p(void* raw_ctx, int fd, const void* buf, size_t count) {
    if (count < 8) {
        errno = EINVAL;
        return -1;
    }

    uint64_t to_add;
    memcpy(&to_add, buf, 8);
    if (to_add == (uint64_t)-1) {
        errno = EINVAL;
        return -1;
    }

    xsp_eventfd_t* efd = efd_lookup(raw_ctx, fd);
    if (!efd)
        return -1;  // errno already set.

    while (efd->value + to_add < efd->value) {
        // Overflow. If nonblocking, fail; else block.
        if (efd->nonblock) {
            UNLOCK(&efd->mux);
            errno = EAGAIN;
            return -1;
        }

//FIXME take ref, release mux, block, take mux, release ref
    }

    efd->value += to_add;
//FIXME unblock

    UNLOCK(&efd->mux);
//FIXME
    return -1;
}

static ssize_t efd_read_p(void* raw_ctx, int fd, void* buf, size_t count) {
    xsp_eventfd_t* efd = efd_lookup(raw_ctx, fd);
    if (!efd)
        return -1;  // errno already set.

//FIXME

    UNLOCK(&efd->mux);

//FIXME
    return -1;
}

int efd_close_p(void* raw_ctx, int fd) {
    xsp_eventfd_ctx_t* ctx = (xsp_eventfd_ctx_t*)raw_ctx;
    LOCK(&ctx->mux);

    size_t idx;
    xsp_eventfd_t* efd = efd_lookup_locked(ctx, fd, &idx);
    if (!efd) {
        UNLOCK(&ctx->mux);
        errno = EBADF;  // TODO(vtl): This shouldn't happen, I think?
        return -1;
    }

    ctx->eventfds[idx].fd = -1;
    ctx->eventfds[idx].efd = NULL;
    LOCK(&efd->mux);
    UNLOCK(&ctx->mux);

//FIXME wake up all waiters?

    efd_unref_locked(efd);
    return 0;
}

void xsp_eventfd_register() {
    static const esp_vfs_t vfs = {
            .flags = ESP_VFS_FLAG_CONTEXT_PTR,
            .write_p = &efd_write_p,
            .read_p = &efd_read_p,
            .close_p = &efd_close_p,
    };

    ESP_ERROR_CHECK(!g_eventfd_ctx ? ESP_OK : ESP_FAIL); 

    xsp_eventfd_ctx_t* g_eventfd_ctx = (xsp_eventfd_ctx_t*)malloc(sizeof(xsp_eventfd_ctx_t));
    ESP_ERROR_CHECK(g_eventfd_ctx ? ESP_OK : ESP_ERR_NO_MEM);
    vPortCPUInitializeMutex(&g_eventfd_ctx->mux);
    g_eventfd_ctx->num_eventfd = 0;
    for (size_t i = 0; i < MAX_NUM_EVENTFD; i++) {
        g_eventfd_ctx->eventfds[i].fd = -1;
        g_eventfd_ctx->eventfds[i].efd = NULL;
    }

    ESP_ERROR_CHECK(esp_vfs_register_with_id(&vfs, g_eventfd_ctx, &g_eventfd_vfs_id));
}

int xsp_eventfd(unsigned initval, int flags) {
    if (!g_eventfd_ctx) {
        errno = EFAULT;
        return -1;
    }

    xsp_eventfd_t* efd = (xsp_eventfd_t*)malloc(sizeof(xsp_eventfd_t));
    if (!efd) {
        errno = ENOMEM;
        return -1;
    }
    vPortCPUInitializeMutex(&efd->mux);
    efd->refcount = 1;
    efd->value = initval;
    efd->nonblock = !!(flags & XSP_EVENTFD_NONBLOCK);

    LOCK(&g_eventfd_ctx->mux);

    size_t idx = 0;
    for (; idx < MAX_NUM_EVENTFD; idx++) {
        if (g_eventfd_ctx->eventfds[idx].fd == -1)
            break;
    }
    if (idx == MAX_NUM_EVENTFD) {
        UNLOCK(&g_eventfd_ctx->mux);
        free(efd);
        errno = ENFILE;  // TODO(vtl): Not sure about this.
        return -1;
    }

    int fd;
    esp_err_t err = esp_vfs_register_fd(g_eventfd_vfs_id, &fd);
    if (err != ESP_OK) {
        UNLOCK(&g_eventfd_ctx->mux);
        free(efd);
        switch (err) {
        case ESP_ERR_INVALID_ARG:
            errno = EINVAL;
            break;
        case ESP_ERR_NO_MEM:
            errno = ENFILE;
            break;
        default:
            errno = EFAULT;  // TODO(vtl): ???
            break;
        }
        return -1;
    }

    g_eventfd_ctx->eventfds[idx].fd = fd;
    g_eventfd_ctx->eventfds[idx].efd = efd;
    UNLOCK(&g_eventfd_ctx->mux);
    return fd;
}
