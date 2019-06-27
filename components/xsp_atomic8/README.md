# xsp_atomic8

`xsp_atomic8` supports 64-bit "atomic" operations.

The ESP32-IDF SDK (and the associated toolchain) is missing library support for
64-bit atomics; this results in link errors. This component implements the
required builtins (`__atomic_..._8()`), and makes 64-bit atomic types/functions
from the C `<stdatomic.h>` and the C++ `<atomic>` mostly work (see below).

A few things do not work, however. In particular, neither the C
`atomic_is_lock_free()` function nor the C++ `is_lock_free()` method works.

This has been tested (in a cursory way) with the
`xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0` toolchain (on Linux).

## Implementation details

This uses a single global lock to protect accesses to all 64-bit "atomics". (I
believe this masks interrupts and has a spinlock, for other cores.)
