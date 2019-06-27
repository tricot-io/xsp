// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include <stdatomic.h>
#include <stdio.h>

#define VERIFY(cond, text) printf("  [%s] %s\n", (cond) ? "pass" : "FAIL", (text))

void verify_stdatomic(void) {
    printf("C <stdatomic.h>:\n");

    // TODO(vtl): This doesn't work, but somehow we have stdatomic.h anyway.
    // VERIFY(__STDC_VERSION__ >= 201112L, "C11");

    VERIFY(ATOMIC_LLONG_LOCK_FREE == 0 || ATOMIC_LLONG_LOCK_FREE == 1, "ATOMIC_LLONG_LOCK_FREE");

    atomic_uint_least64_t x;

    // TODO(vtl): This doesn't build, possibly because we're not C11.
    // VERIFY(!atomic_is_lock_free(&x), "atomic_is_lock_free");

    atomic_init(&x, 123);
    VERIFY(atomic_load(&x) == 123, "atomic_init, atomic_load");

    atomic_store(&x, 456);
    VERIFY(atomic_load(&x) == 456, "atomic_store, atomic_load");

    VERIFY(atomic_exchange(&x, 789) == 456 && atomic_load(&x) == 789,
           "atomic_exchange, atomic_load");

    uint64_t expected = 789;
    VERIFY(atomic_compare_exchange_strong(&x, &expected, 1011) && atomic_load(&x) == 1011,
           "atomic_compare_exchange_strong (success), atomic_load");
    VERIFY(!atomic_compare_exchange_strong(&x, &expected, 1213) && expected == 1011 &&
                   atomic_load(&x) == 1011,
           "atomic_compare_exchange_strong (failure), atomic_load");

    expected = 1011;
    VERIFY(atomic_compare_exchange_weak(&x, &expected, 1213) && atomic_load(&x) == 1213,
           "atomic_compare_exchange_weak (success), atomic_load");
    VERIFY(!atomic_compare_exchange_weak(&x, &expected, 1415) && expected == 1213 &&
                   atomic_load(&x) == 1213,
           "atomic_compare_exchange_weak (failure), atomic_load");

    VERIFY(atomic_fetch_add(&x, 202) == 1213 && atomic_load(&x) == 1415,
           "atomic_fetch_add, atomic_load");
    VERIFY(atomic_fetch_sub(&x, 101) == 1415 && atomic_load(&x) == 1314,
           "atomic_fetch_sub, atomic_load");
    VERIFY(atomic_fetch_or(&x, 7) == 1314 && atomic_load(&x) == 1319,
           "atomic_fetch_or, atomic_load");
    VERIFY(atomic_fetch_xor(&x, 9) == 1319 && atomic_load(&x) == 1326,
           "atomic_fetch_xor, atomic_load");
    VERIFY(atomic_fetch_and(&x, 1079) == 1326 && atomic_load(&x) == 1062,
           "atomic_fetch_and, atomic_load");

    // Note: This test doesn't hit: __atomic_test_and_set_8, __atomic_fetch_nand_8, and the
    // __atomic_<op>_fetch_8.
}
