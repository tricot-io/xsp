# xsp_ws_client test

This is a WebSocket client used to test `xsp_ws_client`.

It connects to a Autobahn WebSocket Fuzzing Server ("wstest -m fuzzingserver"),
from the Autobahn|Testsuite, gets the number of test cases, runs through all the
test cases, and then requests that reports be generated.

Note that for each test case, it simply connects to the appropriate URL and
echoes messages (also otherwise behaving as required by the WebSocket
specification) until the connection is closed (by it or by the server).

`run_fuzzingserver.sh` gives an easy way to run the server. Its configuration is
in the `config` subdirectory; it will generate reports under `reports/clients`.
(Hint: You may also want to open up its HTTP port, by passing a `-p 8080:8080`
argument. This allows you to easily view the results, and also to run the tests
on your browser's WebSocket client implementation.)
