// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_eventfd.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/lock.h>

#include "esp_err.h"
#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "sdkconfig.h"

// TODO(vtl): Make this a Kconfig value.
#define MAX_NUM_EVENTFD 4

#define LOCK_TYPE _lock_t

#define INIT_LOCK(l) _lock_init(l)
#define DEINIT_LOCK(l) _lock_close(l)

#define LOCK(l) _lock_acquire(l)
#define UNLOCK(l) _lock_release(l)

// NOTE: xsp_eventfd_ctx_t's lock precedes xsp_eventfd_t's lock in the (acquisition) order.

#define EFD_EVENT_DEC_BIT 1  // Used to wait for the value to decrease.
#define EFD_EVENT_INC_BIT 2  // Used to wait for the value to increase.

typedef struct xsp_eventfd_struct {
    LOCK_TYPE lock;
    unsigned refcount;
    uint64_t value;
    bool nonblock;
    bool closed;

    EventGroupHandle_t events;
    unsigned dec_waiters;  // Number of things waiting for the value to decrease.
    unsigned inc_waiters;  // Number of things waiting for the value to increase.
} xsp_eventfd_t;

typedef struct xsp_eventfd_ctx {
    LOCK_TYPE lock;
    size_t num_eventfd;
    struct {
        int fd;  // Should be initialized to -1.
        xsp_eventfd_t* efd;
    } eventfds[MAX_NUM_EVENTFD];
} xsp_eventfd_ctx_t;

static esp_vfs_id_t g_eventfd_vfs_id = -1;
static xsp_eventfd_ctx_t* g_eventfd_ctx = NULL;

static void efd_ref_locked(xsp_eventfd_t* efd) {
    efd->refcount++;
}

// Note: `efd` should be locked to call this, but it will be unlocked afterwards.
static void efd_unref_locked(xsp_eventfd_t* efd) {
    if (efd->refcount == 1) {
        UNLOCK(&efd->lock);
        vEventGroupDelete(efd->events);
        DEINIT_LOCK(&efd->lock);
        free(efd);
    } else {
        efd->refcount--;
        UNLOCK(&efd->lock);
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

// Looks up the given FD and returns its `xsp_eventfd_t` with its lock acquired (but without
// incrementing the refcount -- if the caller needs to persist the pointer after unlocking, it must
// increment the refcount). On failure, sets errno and returns null.
static xsp_eventfd_t* efd_lookup(void* raw_ctx, int fd) {
    xsp_eventfd_ctx_t* ctx = (xsp_eventfd_ctx_t*)raw_ctx;
    LOCK(&ctx->lock);

    xsp_eventfd_t* efd = efd_lookup_locked(raw_ctx, fd, NULL);
    if (!efd) {
        UNLOCK(&ctx->lock);
        errno = EBADF;  // TODO(vtl): This shouldn't happen, I think?
        return NULL;
    }

    LOCK(&efd->lock);
    UNLOCK(&ctx->lock);
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

    // Assume everything is kosher if `to_add` is 0.
    if (to_add == 0)
        return 8;

    xsp_eventfd_t* efd = efd_lookup(raw_ctx, fd);
    if (!efd)
        return -1;  // errno already set.
    efd_ref_locked(efd);

    while (efd->value + to_add < efd->value) {
        // Overflow. If nonblocking, fail; else block.
        if (efd->nonblock) {
            efd_unref_locked(efd);
            errno = EAGAIN;
            return -1;
        }

        // TODO(vtl): Check for ISR context; presumably blocking probably not allowed there.

        efd->dec_waiters++;
        UNLOCK(&efd->lock);
        xEventGroupWaitBits(efd->events, EFD_EVENT_DEC_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        LOCK(&efd->lock);
        efd->dec_waiters--;
        if (efd->closed) {
            efd_unref_locked(efd);
            errno = EBADF;  // TODO(vtl): Not sure about this error.
            return -1;
        }
        if (efd->dec_waiters == 0)
            xEventGroupClearBits(efd->events, EFD_EVENT_DEC_BIT);
    }

    efd->value += to_add;

    if (efd->inc_waiters > 0)
        xEventGroupSetBits(efd->events, EFD_EVENT_INC_BIT);

    efd_unref_locked(efd);
    return 8;
}

static ssize_t efd_read_p(void* raw_ctx, int fd, void* buf, size_t count) {
    if (count < 8) {
        errno = EINVAL;
        return -1;
    }

    xsp_eventfd_t* efd = efd_lookup(raw_ctx, fd);
    if (!efd)
        return -1;  // errno already set.

    while (efd->value == 0) {
        // If nonblocking, fail; else block.
        if (efd->nonblock) {
            efd_unref_locked(efd);
            errno = EAGAIN;
            return -1;
        }

        // TODO(vtl): Check for ISR context; presumably blocking probably not allowed there.

        efd->inc_waiters++;
        UNLOCK(&efd->lock);
        xEventGroupWaitBits(efd->events, EFD_EVENT_INC_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        LOCK(&efd->lock);
        efd->inc_waiters--;
        if (efd->closed) {
            efd_unref_locked(efd);
            errno = EBADF;  // TODO(vtl): Not sure about this error.
            return -1;
        }
        if (efd->inc_waiters == 0)
            xEventGroupClearBits(efd->events, EFD_EVENT_INC_BIT);
    }

    memcpy(buf, &efd->value, 8);
    efd->value = 0;

    if (efd->dec_waiters > 0)
        xEventGroupSetBits(efd->events, EFD_EVENT_DEC_BIT);

    efd_unref_locked(efd);
    return 8;
}

int efd_close_p(void* raw_ctx, int fd) {
    xsp_eventfd_ctx_t* ctx = (xsp_eventfd_ctx_t*)raw_ctx;
    LOCK(&ctx->lock);

    size_t idx;
    xsp_eventfd_t* efd = efd_lookup_locked(ctx, fd, &idx);
    if (!efd) {
        UNLOCK(&ctx->lock);
        errno = EBADF;  // TODO(vtl): This shouldn't happen, I think?
        return -1;
    }

    ctx->eventfds[idx].fd = -1;
    ctx->eventfds[idx].efd = NULL;
    LOCK(&efd->lock);
    UNLOCK(&ctx->lock);

    efd->closed = true;
    xEventGroupSetBits(efd->events, EFD_EVENT_DEC_BIT | EFD_EVENT_INC_BIT);

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

    g_eventfd_ctx = (xsp_eventfd_ctx_t*)malloc(sizeof(xsp_eventfd_ctx_t));
    ESP_ERROR_CHECK(g_eventfd_ctx ? ESP_OK : ESP_ERR_NO_MEM);
    INIT_LOCK(&g_eventfd_ctx->lock);
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
    efd->events = xEventGroupCreate();
    if (!efd->events) {
        free(efd);
        errno = ENOMEM;
        return -1;
    }
    INIT_LOCK(&efd->lock);
    efd->refcount = 1;
    efd->value = initval;
    efd->nonblock = !!(flags & XSP_EVENTFD_NONBLOCK);
    efd->closed = false;
    efd->dec_waiters = 0;
    efd->inc_waiters = 0;

    LOCK(&g_eventfd_ctx->lock);

    size_t idx = 0;
    for (; idx < MAX_NUM_EVENTFD; idx++) {
        if (g_eventfd_ctx->eventfds[idx].fd == -1)
            break;
    }
    if (idx == MAX_NUM_EVENTFD) {
        UNLOCK(&g_eventfd_ctx->lock);
        vEventGroupDelete(efd->events);
        DEINIT_LOCK(&efd->lock);
        free(efd);
        errno = ENFILE;  // TODO(vtl): Not sure about this.
        return -1;
    }

    int fd;
    esp_err_t err = esp_vfs_register_fd(g_eventfd_vfs_id, &fd);
    if (err != ESP_OK) {
        UNLOCK(&g_eventfd_ctx->lock);
        vEventGroupDelete(efd->events);
        DEINIT_LOCK(&efd->lock);
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
    UNLOCK(&g_eventfd_ctx->lock);
    return fd;
}
