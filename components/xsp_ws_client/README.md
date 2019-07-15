# xsp_ws_client

`xsp_ws_client` is a WebSocket implementation for ESP32-IDF.

Currently, it relies on `esp_http_client` for the HTTP/HTTPS part of the
connection establishment (before the protocol is switched). It also currently
relies on `esp_transport` (a.k.a. `tcp_transport`) for the transport layer.

Note that it currently requires a patched version of the ESP32-IDF SDK; see the
top-level README.md.

## Features, capabilities, and compliance

*   `xsp_ws_client` supports the requirements of the basic WebSocket
    specification, RFC6455.
*   It supports TCP and SSL transports (i.e., `ws://...` and `wss://...` URLs).
*   It allows for application-level subprotocols to be negotiated.
*   It supports WebSocket message fragmentation and defragmentation (into/from
    WebSocket frames).
*   It supports asynchronous (within its own event loop) message transmission
    and reception.
*   It passes 1.\* through 7.\* (inclusive) of the Autobahn WebSocket Fuzzing
    Server (`wstest -m fuzzingserver`) tests from the Autobahn|Testsuite.

## Overview

The `xsp_ws_client` consists of several layers (starting with the lowest level):

*   `xsp_ws_client` proper provides connection establishment (and low-level
    shutdown), and writing and reading of WebSocket *frames*.
*   `xsp_ws_client_loop` provides a WebSocket event loop, and implements most of
    the WebSocket protocol. It supports asynchronously sending of WebSocket
    *messages* (possibly fragmented across multiple frames) and receiving
    WebSocket data *frames* (as events).
*   `xsp_wd_client_defrag` is an additional utility that supports defragmenting
    received data frames into messages.

As previously mentioned, the above are built on top of the following layers:

*   `esp_http_client` provides HTTP client functionality.
*   `esp_transport` (a.k.a. `tcp_transport`) provides a lower-level transport
    layer, supporting and TCP or SSL (over TCP) transports.

## The xsp_ws_client layer

This layer supports WebSocket connection establishment (i.e., the WebSocket
opening handshake), writing and receiving WebSocket frames, and shutting down
the connection at the transport level (this layer does not implement the
WebSocket closing handshake per se, since it is largely stateless).

Its basic primitives are all synchronous, and consist of:

*   opening a connection;
*   polling write (i.e., checking if a WebSocket frame write can be started
    without blocking immediately);
*   writing a WebSocket frame;
*   polling read (i.e., if a WebSocket frame read can be started without
    blocking immediately);
*   reading a WebSocket frame; and
*   shutting down the transport.

## The xsp_ws_client_handler layer

This layer maintains WebSocket state and implements most of the WebSocket
protocol (apart from the basics provided by `xsp_ws_client`) in conjunction with
the `xsp_loop` event loop.

Its operation proceeds as follows:

*   Before initializing the handler, the `xsp_ws_client` connection should be
    established. The `xsp_loop` should also be created. Both must remain valid
    for the lifetime of the handler.
*   Once the handler is initialized it will watch the connection using the
    loop's FD watcher API. It will appropriately handle will-select, can-read,
    and can-write events.
*   In turn, it will provide the application with its own events (see below).
    *   It provides an API for use while handling events.
    *   It will automatically read frames (generating events as appropriate),
        handle the close handshake, and also automatically sending pongs for any
        pings received.
*   After the handler is shut down, the `xsp_ws_client` should be shut down (at
    the transport level).

### Events

The handler provides the following events:

*   Data-frame-received: This is sent when a data frame is received.
*   Ping-received: This is sent when a ping frame is received (after a pong is
    automatically sent).
*   Pong-received: This is sent when a pong frame is received.
*   Message-sent: This is sent when the sending of a message (see below) is
    complete, whether successful or unsuccessful. Note that for each message
    sent, a corresponding message-sent event will be generated; this allows the
    application to reclaim the message buffer.

### Event API

The following are available while handling an event:

*   Send message: Schedules a message to be sent. Currently, this is strictly
    asynchronous (and no data is sent immediately). If the function indicates
    success (i.e., that the message was scheduled successfully), the message
    buffer must remain valid until the corresponding message-sent event. No
    queueing is done, and another message may not be (scheduled to be) sent
    until that event.
*   Close: Closes the connection.
*   Ping: Sends a ping frame.

## The xsp_ws_client_defrag layer

This layer tracks, validates, and defragments received WebSocket data frames. It
provides a single primary function, typically called within the loop from the
event handler for the data-frame-received event.

The input of this function is a data frame; its output of this function is one
of:

*   Success, message complete: The frame was accepted and a complete message has
    been defragmented. The message is passed to the caller, which takes
    ownership of it.
*   No error: The frame was accepted without error, but more frames are required
    to complete a message.
*   Error, message too big or allocation failure: The caller may choose to close
    the connection, or it may continue and discard the current message. (The
    function will continue to return this error until a new message is
    received.)
*   Error, protocol error: The caller should close the connection with the
    "protocol error" close status.
*   Error, invalid data: The caller should close the connection with the
    "invalid data" close status.

Note that the use of this layer is highly optional, and that the application may
choose to defragment messages in its own way. (E.g., for those treating the
WebSocket as a stream, defragmentation may not be necessary.) In that case, the
application must ensure protocol compliance.
