# xsp_loop

`xsp_loop` is an (in development) event loop for ESP32-IDF.

Currently, it has the ability to watch file descriptors (FDs) via `select()`.

## Basic loop events

*   Start: This is sent before the first iteration of the loop.
*   Stop: This is sent after the last iteration of the loop, after
    `xsp_loop_stop()` is called.
*   Idle: This is called on every iteration of the loop in which no work is
    done; note that watching an FD (see below) does not count as work.

## FD watcher

There is the ability to add/remove FDs to be watched. The watchers are
identified by "handles"; multiple watchers on the same FD are supported, with
some caveats (*TODO*(vtl): currently, even if you don't request to watch for,
e.g., read, but another watcher does on the same handle, you might get the
can-read event; this is a bug).

Adding/removing an FD watcher may be done while the loop is not running (e.g.,
adding a watcher before the loop starts and removing it after it stops), or
inside handlers for basic loop events (above).

IMPORTANT CAVEAT: Currently, adding/removing FD watchers inside FD watcher
events (below) is not supported (*TODO*(vtl)).

### FD watcher events

*   Will-select: This is not an event per se, but is generated for each
    iteration. The handler (if any) should indicate what events (below) should
    be watched for. If there is no handler, the watched-for events will be
    determined by the existence of handlers for the events below.
*   Can-read: This is generated when a watched FD can be read from without
    blocking (according to `select()`).
*   Can-write: This is generated when a watched FD can be written to without
    blocking (according to `select()`).
