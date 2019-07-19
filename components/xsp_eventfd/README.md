# xsp_eventfd

`xsp_eventfd` supports a Linux-like event file descriptor ("eventfd") mechanism.
In particular, this allows one to `select()` on socket file descriptors and
still be awoken by other tasks.

## Features and limitations

*   It should be analogous to Linux's eventfd, except that semaphore mode is not
    yet supported.
*   Note that there is the (severe) limitation that there may only be one
    concurrent `select()`. (This is due to the way that `esp_vfs`'s `select()`
    is written.)
*   Nonblocking mode is supported, but this can currently only be set on
    creation (`fcntl()` is not yet supported).

## Usage

*   First, the subsystem must be initialized using `xsp_eventfd_register()`;
    this should preferably be done before starting other tasks (in particular,
    before concurrently using the VFS subsystem, which typically includes
    serial/logging output).
*   Then `xsp_eventfd()` should be used to create event file descriptors, in the
    same way that `eventfd()` is used on Linux.
