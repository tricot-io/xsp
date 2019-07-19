# tricot-io/xsp

Additional components for ESP-IDF (Espressif IoT Development Framework, for the
ESP32 chip).

## Requirements

This currently requires a slightly modified ESP-IDF v3.2.2 from
https://github.com/tricot-io/esp-idf:

*   branch: `release/v3.2.2-tricot`
*   commit: `e0ce353f55387fad9de3c4325ba149cc93106674
*   tag: `v3.2.2-tricot1.2.0`

## Components

*   `xsp_atomic8`: supports 64-bit "atomic" operations.
*   `xsp_cxx`: C++ wrappers for some of the other components.
*   `xsp_loop`: an event loop (in development).
*   `xsp_ws_client`: a WebSocket client.
