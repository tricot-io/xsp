// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp_eventfd.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"

#include "sdkconfig.h"

//FIXME make config
#define MAX_NUM_EVENTFD 4

#define LOCK(mux) portENTER_CRITICAL(mux)
#define UNLOCK(mux) portEXIT_CRITICAL(mux)

typedef struct xsp_eventfd_struct {
    uint64_t value;
//FIXME
} xsp_eventfd_t;

typedef struct xsp_eventfd_ctx {
    portMUX_TYPE mux;
    size_t num_eventfd;
    struct {
        int fd;  // Should be initialized to -1.
        xsp_eventfd_t* eventfd;
    } eventfds[MAX_NUM_EVENTFD];
} xsp_eventfd_ctx_t;

static esp_vfs_id_t g_eventfd_vfs_id = -1;
static xsp_eventfd_ctx_t* g_eventfd_ctx = NULL;

static xsp_eventfd_t* eventfd_lookup_no_lock(xsp_eventfd_ctx_t* ctx, int fd, size_t* idx) {
    if (fd < 0)
        return NULL;

    for (size_t i = 0; i < MAX_NUM_EVENTFD; i++) {
        if (ctx->eventfds[i].fd == fd) {
            if (idx)
                *idx = i;
            return ctx->eventfds[i].eventfd;
        }
    }

    return NULL;
}

static xsp_eventfd_t* eventfd_lookup(void* raw_ctx, int fd) {
    xsp_eventfd_ctx_t* ctx = (xsp_eventfd_ctx_t*)raw_ctx;
    LOCK(&ctx->mux);

    xsp_eventfd_t* rv = eventfd_lookup_no_lock(raw_ctx, fd, NULL);
    if (!rv) {
        UNLOCK(&ctx->mux);
        errno = EBADF;  // TODO(vtl): This shouldn't happen, I think?
        return NULL;
    }

//FIXME take specific mutex
    UNLOCK(&ctx->mux);
    return rv;
}

static ssize_t eventfd_write_p(void* raw_ctx, int fd, const void * data, size_t size) {
    xsp_eventfd_t* efd = eventfd_lookup(raw_ctx, fd);
    if (!efd)
        return -1;

//FIXME release specific mutex
//FIXME
    return -1;
}

static ssize_t eventfd_read_p(void* raw_ctx, int fd, void * dst, size_t size) {
    xsp_eventfd_t* efd = eventfd_lookup(raw_ctx, fd);
    if (!efd)
        return -1;

//FIXME release specific mutex
//FIXME
    return -1;
}

int eventfd_close_p(void* raw_ctx, int fd) {
    xsp_eventfd_ctx_t* ctx = (xsp_eventfd_ctx_t*)raw_ctx;
    LOCK(&ctx->mux);

    size_t idx;
    xsp_eventfd_t* efd = eventfd_lookup_no_lock(ctx, fd, &idx);
    if (!efd) {
        UNLOCK(&ctx->mux);
        errno = EBADF;  // TODO(vtl): This shouldn't happen, I think?
        return -1;
    }

//FIXME remove entry
//FIXME take specific mutex
    UNLOCK(&ctx->mux);

//FIXME wakeup all waiters?

//FIXME
    return -1;
}

void xsp_eventfd_register() {
    static const esp_vfs_t vfs = {
            .flags = ESP_VFS_FLAG_CONTEXT_PTR,
            .write_p = &eventfd_write_p,
            .read_p = &eventfd_read_p,
            .close_p = &eventfd_close_p,
    };

    ESP_ERROR_CHECK(!g_eventfd_ctx ? ESP_OK : ESP_FAIL); 

    xsp_eventfd_ctx_t* g_eventfd_ctx = (xsp_eventfd_ctx_t*)malloc(sizeof(xsp_eventfd_ctx_t));
    ESP_ERROR_CHECK(g_eventfd_ctx ? ESP_OK : ESP_ERR_NO_MEM);
    vPortCPUInitializeMutex(&g_eventfd_ctx->mux);
    g_eventfd_ctx->num_eventfd = 0;
    for (size_t i = 0; i < MAX_NUM_EVENTFD; i++) {
        g_eventfd_ctx->eventfds[i].fd = -1;
        g_eventfd_ctx->eventfds[i].eventfd = NULL;
    }

    ESP_ERROR_CHECK(esp_vfs_register_with_id(&vfs, g_eventfd_ctx, &g_eventfd_vfs_id));
}

int xsp_eventfd(unsigned initval, int flags) {
    if (!g_eventfd_ctx) {
        errno = EFAULT;
        return -1;
    }

    LOCK(&g_eventfd_ctx->mux);

    size_t idx = 0;
    for (; idx < MAX_NUM_EVENTFD; idx++) {
        if (g_eventfd_ctx->eventfds[idx].fd == -1)
            break;
    }
    if (idx == MAX_NUM_EVENTFD) {
        UNLOCK(&g_eventfd_ctx->mux);
        errno = ENFILE;  // TODO(vtl): Not sure about this.
        return -1;
    }

//FIXME register; set entry

    UNLOCK(&g_eventfd_ctx->mux);
//FIXME
    return -1;
}
