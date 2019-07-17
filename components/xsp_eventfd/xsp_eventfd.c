// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_eventfd.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/lock.h>
#include <sys/select.h>

#include "esp_err.h"
#include "esp_log.h"
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

    bool select_active;
} xsp_eventfd_t;

typedef struct xsp_eventfd_ctx {
    LOCK_TYPE lock;
    size_t num_eventfd;
    struct {
        int fd;  // Should be initialized to -1.
        xsp_eventfd_t* efd;
    } eventfds[MAX_NUM_EVENTFD];
    // Note: Only one select() at a time, which is unfortunate, but other VFSes have the same
    // limitation.
    SemaphoreHandle_t* select_signal;
    fd_set select_readfds_in;     // Only meaningful when select_signal is set.
    fd_set select_writefds_in;    // Only meaningful when select_signal is set.
    fd_set* select_readfds_out;   // Only meaningful when select_signal is set.
    fd_set* select_writefds_out;  // Only meaningful when select_signal is set.
} xsp_eventfd_ctx_t;

static const char TAG[] = "EVENTFD";

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
        errno = EBADF;  // TODO(vtl): This can't happen, I think?
        return NULL;
    }

    LOCK(&efd->lock);
    UNLOCK(&ctx->lock);
    return efd;
}

static void maybe_signal_select_locked(xsp_eventfd_ctx_t* ctx) {
    if (!ctx->select_signal)
        return;

    bool should_signal = false;
    for (size_t i = 0; i < MAX_NUM_EVENTFD; i++) {
        int fd = g_eventfd_ctx->eventfds[i].fd;
        if (fd == -1)
            continue;

        bool select_readable = !!FD_ISSET(fd, &g_eventfd_ctx->select_readfds_in);
        bool select_writable = !!FD_ISSET(fd, &g_eventfd_ctx->select_writefds_in);
        if (select_readable || select_writable) {
            xsp_eventfd_t* efd = g_eventfd_ctx->eventfds[i].efd;
            LOCK(&efd->lock);
            uint64_t value = efd->value;
            UNLOCK(&efd->lock);

            if ((select_readable && value > 0) || (select_writable && value < (uint64_t)-1)) {
                should_signal = true;
                break;
            }
        }
    }

    if (should_signal) {
        // TODO(vtl): Calling esp_vfs_select_triggered() under ctx->lock is slightly dodgy.
        // TODO(vtl): There's also esp_vfs_select_triggered_isr()....
        esp_vfs_select_triggered(ctx->select_signal);
    }
}

// Signals select() if still appropriate.
static void maybe_signal_select(void* raw_ctx) {
    xsp_eventfd_ctx_t* ctx = (xsp_eventfd_ctx_t*)raw_ctx;
    LOCK(&ctx->lock);
    maybe_signal_select_locked(ctx);
    UNLOCK(&ctx->lock);
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
            ESP_LOGE(TAG, "FD closed while blocked in write()");
            errno = EBADF;  // TODO(vtl): Not sure about this error code.
            return -1;
        }
        if (efd->dec_waiters == 0)
            xEventGroupClearBits(efd->events, EFD_EVENT_DEC_BIT);
    }

    efd->value += to_add;

    if (efd->inc_waiters > 0)
        xEventGroupSetBits(efd->events, EFD_EVENT_INC_BIT);

    // We'll need to grab the global lock, so we need to release the EFD lock.
    bool select_active = efd->select_active;
    efd_unref_locked(efd);

    if (select_active)
        maybe_signal_select(raw_ctx);

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
    efd_ref_locked(efd);

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
            ESP_LOGE(TAG, "FD closed while blocked in read()");
            errno = EBADF;  // TODO(vtl): Not sure about this error code.
            return -1;
        }
        if (efd->inc_waiters == 0)
            xEventGroupClearBits(efd->events, EFD_EVENT_INC_BIT);
    }

    memcpy(buf, &efd->value, 8);
    efd->value = 0;

    if (efd->dec_waiters > 0)
        xEventGroupSetBits(efd->events, EFD_EVENT_DEC_BIT);

    // We'll need to grab the global lock, so we need to release the EFD lock.
    bool select_active = efd->select_active;
    efd_unref_locked(efd);

    if (select_active)
        maybe_signal_select(raw_ctx);

    return 8;
}

int efd_close_p(void* raw_ctx, int fd) {
    xsp_eventfd_ctx_t* ctx = (xsp_eventfd_ctx_t*)raw_ctx;
    LOCK(&ctx->lock);

    size_t idx;
    xsp_eventfd_t* efd = efd_lookup_locked(ctx, fd, &idx);
    if (!efd) {
        UNLOCK(&ctx->lock);
        errno = EBADF;  // TODO(vtl): This can't happen, I think?
        return -1;
    }

    ctx->eventfds[idx].fd = -1;
    ctx->eventfds[idx].efd = NULL;
    LOCK(&efd->lock);
    UNLOCK(&ctx->lock);

    efd->closed = true;
    xEventGroupSetBits(efd->events, EFD_EVENT_DEC_BIT | EFD_EVENT_INC_BIT);

    if (efd->select_active)
        ESP_LOGE(TAG, "Closing FD while it's being used in select()");  // TODO(vtl): Panic instead?
    efd->select_active = false;

    efd_unref_locked(efd);
    return 0;
}

static esp_err_t efd_start_select(int nfds,
                                  fd_set* readfds,
                                  fd_set* writefds,
                                  fd_set* exceptfds,
                                  SemaphoreHandle_t* signal_sem) {
    if (!g_eventfd_ctx)
        return ESP_ERR_INVALID_STATE;  // TODO(vtl): This can't happen, I think?
    if (!signal_sem)
        return ESP_ERR_INVALID_ARG;

    LOCK(&g_eventfd_ctx->lock);

    if (g_eventfd_ctx->select_signal) {
        UNLOCK(&g_eventfd_ctx->lock);
        return ESP_ERR_INVALID_STATE;
    }
    g_eventfd_ctx->select_signal = signal_sem;
    if (readfds)
        g_eventfd_ctx->select_readfds_in = *readfds;
    else
        FD_ZERO(&g_eventfd_ctx->select_readfds_in);
    if (writefds)
        g_eventfd_ctx->select_writefds_in = *writefds;
    else
        FD_ZERO(&g_eventfd_ctx->select_writefds_in);
    g_eventfd_ctx->select_readfds_out = readfds;
    g_eventfd_ctx->select_writefds_out = writefds;

    for (size_t i = 0; i < MAX_NUM_EVENTFD; i++) {
        int fd = g_eventfd_ctx->eventfds[i].fd;
        if (fd == -1)
            continue;

        bool select_readable = !!FD_ISSET(fd, &g_eventfd_ctx->select_readfds_in);
        bool select_writable = !!FD_ISSET(fd, &g_eventfd_ctx->select_writefds_in);
        if (select_readable || select_writable) {
            xsp_eventfd_t* efd = g_eventfd_ctx->eventfds[i].efd;
            LOCK(&efd->lock);
            efd->select_active = true;
            UNLOCK(&efd->lock);
        }
    }

    maybe_signal_select_locked(g_eventfd_ctx);

    UNLOCK(&g_eventfd_ctx->lock);
    return ESP_OK;
}

static void efd_end_select() {
    assert(g_eventfd_ctx);

    LOCK(&g_eventfd_ctx->lock);

    assert(g_eventfd_ctx->select_signal);

    for (size_t i = 0; i < MAX_NUM_EVENTFD; i++) {
        int fd = g_eventfd_ctx->eventfds[i].fd;
        if (fd == -1)
            continue;

        bool select_readable = !!FD_ISSET(fd, &g_eventfd_ctx->select_readfds_in);
        bool select_writable = !!FD_ISSET(fd, &g_eventfd_ctx->select_writefds_in);
        if (select_readable || select_writable) {
            xsp_eventfd_t* efd = g_eventfd_ctx->eventfds[i].efd;
            LOCK(&efd->lock);
            uint64_t value = efd->value;
            efd->select_active = false;
            UNLOCK(&efd->lock);

            if (select_readable && value > 0)
                FD_SET(fd, g_eventfd_ctx->select_readfds_out);

            if (select_writable && value < (uint64_t)-1)
                FD_SET(fd, g_eventfd_ctx->select_writefds_out);
        }
    }

    g_eventfd_ctx->select_signal = NULL;
    FD_ZERO(&g_eventfd_ctx->select_readfds_in);
    FD_ZERO(&g_eventfd_ctx->select_writefds_in);
    g_eventfd_ctx->select_readfds_out = NULL;
    g_eventfd_ctx->select_writefds_out = NULL;

    UNLOCK(&g_eventfd_ctx->lock);
}

void xsp_eventfd_register() {
    static const esp_vfs_t vfs = {
            .flags = ESP_VFS_FLAG_CONTEXT_PTR,
            .write_p = &efd_write_p,
            .read_p = &efd_read_p,
            .close_p = &efd_close_p,
            .start_select = &efd_start_select,
            .end_select = &efd_end_select,
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
    g_eventfd_ctx->select_signal = NULL;
    FD_ZERO(&g_eventfd_ctx->select_readfds_in);
    FD_ZERO(&g_eventfd_ctx->select_writefds_in);
    g_eventfd_ctx->select_readfds_out = NULL;
    g_eventfd_ctx->select_writefds_out = NULL;

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
    efd->select_active = false;

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
        errno = ENFILE;  // TODO(vtl): Not sure about this error code.
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
