// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include <stdio.h>
#include <atomic>

#define VERIFY(cond, text) printf("  [%s] %s\n", (cond) ? "pass" : "FAIL", (text))

extern "C" void verify_cxx_atomic(void) {
    printf("C++ <atomic>:\n");

    std::atomic_uint_least64_t x;

    // TODO(vtl): This doesn't build.
    // VERIFY(!x.is_lock_free(), "is_lock_free");

    std::atomic_init(&x, 123ULL);
    VERIFY(x.load() == 123, "std::atomic_init, load");

    x.store(456);
    VERIFY(x.load() == 456, "store, load");

    VERIFY(x.exchange(789) == 456 && x.load() == 789, "exchange, load");

    uint64_t expected = 789;
    VERIFY(x.compare_exchange_strong(expected, 1011, std::memory_order_seq_cst,
                                     std::memory_order_seq_cst) &&
                   x.load() == 1011,
           "compare_exchange_strong (success), load");
    VERIFY(!x.compare_exchange_strong(expected, 1213, std::memory_order_seq_cst,
                                      std::memory_order_seq_cst) &&
                   expected == 1011 && x.load() == 1011,
           "atomic_compare_exchange_strong (failure), atomic_load");

    expected = 1011;
    VERIFY(x.compare_exchange_weak(expected, 1213, std::memory_order_seq_cst,
                                   std::memory_order_seq_cst) &&
                   x.load() == 1213,
           "compare_exchange_weak (success), load");
    VERIFY(!x.compare_exchange_weak(expected, 1415, std::memory_order_seq_cst,
                                    std::memory_order_seq_cst) &&
                   expected == 1213 && x.load() == 1213,
           "compare_exchange_weak (failure), load");

    VERIFY(x.fetch_add(202) == 1213 && x.load() == 1415, "fetch_add, load");
    VERIFY(x.fetch_sub(101) == 1415 && x.load() == 1314, "fetch_sub, load");
    VERIFY(x.fetch_or(7) == 1314 && x.load() == 1319, "fetch_or, load");
    VERIFY(x.fetch_xor(9) == 1319 && x.load() == 1326, "fetch_xor, load");
    VERIFY(x.fetch_and(1079) == 1326 && x.load() == 1062, "fetch_and, load");

    x.store(1617);

    VERIFY(++x == 1618 && x.load() == 1618, "operator++ (pre-increment), load");
    VERIFY(x++ == 1618 && x.load() == 1619, "operator++ (post-increment), load");
    VERIFY(--x == 1618 && x.load() == 1618, "operator-- (pre-decrement), load");
    VERIFY(x-- == 1618 && x.load() == 1617, "operator-- (post-decrement), load");

    VERIFY((x += 101) == 1718 && x.load() == 1718, "operator+=, load");
    VERIFY((x -= 101) == 1617 && x.load() == 1617, "operator-=, load");
    VERIFY((x |= 3) == 1619 && x.load() == 1619, "operator|=, load");
    VERIFY((x ^= 5) == 1622 && x.load() == 1622, "operator^=, load");
    VERIFY((x &= 1140) == 1108 && x.load() == 1108, "operator&=, load");

    // Note: This test doesn't hit: __atomic_test_and_set_8, __atomic_fetch_nand_8, and the
    // __atomic_<op>_fetch_8.
}
