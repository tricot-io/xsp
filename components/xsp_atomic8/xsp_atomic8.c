// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#define CAT3(a, b, c) a##b##c

#define WEAK __attribute__((weak))

static portMUX_TYPE atomic8_mux = portMUX_INITIALIZER_UNLOCKED;

#define LOCK() portENTER_CRITICAL(&atomic8_mux)
#define UNLOCK() portEXIT_CRITICAL(&atomic8_mux)

WEAK uint64_t __atomic_load_8(uint64_t* ptr, int memorder) {
    LOCK();
    uint64_t tmp = *ptr;
    UNLOCK();
    return tmp;
}

WEAK void __atomic_store_8(uint64_t* ptr, uint64_t val, int memorder) {
    LOCK();
    *ptr = val;
    UNLOCK();
}

WEAK uint64_t __atomic_exchange_8(uint64_t* ptr, uint64_t val, int memorder) {
    LOCK();
    uint64_t tmp = *ptr;
    *ptr = val;
    UNLOCK();
    return tmp;
}

WEAK bool __atomic_compare_exchange_8(uint64_t* ptr,
                                      uint64_t* expected,
                                      uint64_t desired,
                                      bool weak,
                                      int success_memorder,
                                      int failure_memorder) {
    LOCK();
    bool result;
    if (*ptr == *expected) {
        *ptr = desired;
        result = true;
    } else {
        *expected = *ptr;
        result = false;
    }
    UNLOCK();
    return result;
}

WEAK bool __atomic_test_and_set_8(uint64_t* ptr, int memorder) {
    LOCK();
    bool result = !!*ptr;
    *ptr = 1;
    UNLOCK();
    return result;
}

#define DEFINE_ATOMIC_FETCH_OP(name, op_assign)                                                 \
    WEAK uint64_t CAT3(__atomic_fetch_, name, _8)(uint64_t * ptr, uint64_t val, int memorder) { \
        LOCK();                                                                                 \
        uint64_t tmp = *ptr;                                                                    \
        op_assign;                                                                              \
        UNLOCK();                                                                               \
        return tmp;                                                                             \
    }

DEFINE_ATOMIC_FETCH_OP(add, *ptr += val)
DEFINE_ATOMIC_FETCH_OP(sub, *ptr -= val)
DEFINE_ATOMIC_FETCH_OP(and, *ptr &= val)
DEFINE_ATOMIC_FETCH_OP(xor, *ptr ^= val)
DEFINE_ATOMIC_FETCH_OP(or, *ptr |= val)
DEFINE_ATOMIC_FETCH_OP(nand, *ptr = ~(*ptr & val))

#undef DEFINE_ATOMIC_FETCH_OP

#define DEFINE_ATOMIC_OP_FETCH(name, op_assign)                                                 \
    WEAK uint64_t CAT3(__atomic_, name, _fetch_8)(uint64_t * ptr, uint64_t val, int memorder) { \
        LOCK();                                                                                 \
        uint64_t tmp = (op_assign);                                                             \
        UNLOCK();                                                                               \
        return tmp;                                                                             \
    }

DEFINE_ATOMIC_OP_FETCH(add, *ptr += val)
DEFINE_ATOMIC_OP_FETCH(sub, *ptr -= val)
DEFINE_ATOMIC_OP_FETCH(and, *ptr &= val)
DEFINE_ATOMIC_OP_FETCH(xor, *ptr ^= val)
DEFINE_ATOMIC_OP_FETCH(or, *ptr |= val)
DEFINE_ATOMIC_OP_FETCH(nand, *ptr = ~(*ptr & val))

#undef DEFINE_ATOMIC_OP_FETCH
