# HTTP Benchmark Summary

| Tool | Case | Endpoint | Body | Conn | IO Threads | Session | Client Threads | Connections | Duration/Requests | Total Requests | RPS | Avg Latency (ms) | P99 (ms) | Transfer/s | Errors |
| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- | --- |
| wrk | wrk_ping_io1_c256 | /ping | none | keepalive | 1 | 0 | 8 | 256 | 5s | 132588 | 25998.28 | 9.720 | 13.800 | 2.85MB | 0 |
| wrk | wrk_ping_io4_c256 | /ping | none | keepalive | 4 | 0 | 8 | 256 | 5s | 252611 | 49535.80 | 5.080 | 9.800 | 5.43MB | 0 |
| wrk | wrk_ping_io8_c256 | /ping | none | keepalive | 8 | 0 | 8 | 256 | 5s | 314327 | 62395.60 | 4.060 | 8.690 | 6.84MB | 0 |
| wrk | wrk_ping_io16_c256 | /ping | none | keepalive | 16 | 0 | 8 | 256 | 5s | 238566 | 47054.52 | 5.810 | 23.290 | 5.16MB | 0 |
| wrk | wrk_ping_c64 | /ping | none | keepalive | 8 | 0 | 8 | 64 | 5s | 254488 | 49913.20 | 1.270 | 2.910 | 5.47MB | 0 |
| wrk | wrk_ping_c256 | /ping | none | keepalive | 8 | 0 | 8 | 256 | 5s | 291147 | 58010.41 | 4.380 | 9.260 | 6.36MB | 0 |
| wrk | wrk_ping_c1024 | /ping | none | keepalive | 8 | 0 | 8 | 1024 | 5s | 296724 | 58277.61 | 17.200 | 33.920 | 6.39MB | 0 |
| wrk | wrk_echo_content_length_c256 | /echo | content-length | keepalive | 8 | 0 | 8 | 256 | 5s | 212099 | 42195.32 | 6.050 | 13.910 | 5.31MB | 0 |
| wrk | wrk_echo_chunked_c256 | /echo | chunked | keepalive | 8 | 0 | 8 | 256 | 5s | 99896 | 19609.36 | 13.390 | 41.830 | 2.30MB | 0 |
| wrk | wrk_ping_session_on_c256 | /ping | none | keepalive | 8 | 1 | 8 | 256 | 5s | 204840 | 40171.25 | 6.610 | 22.730 | 6.53MB | 0 |
| wrk | wrk_ping_short_c256 | /ping | none | short | 8 | 0 | 8 | 256 | 5s | 54854 | 10856.68 | 21.360 | 28.530 | 0.90MB | 0 |
| wrk | wrk_echo_short_c256 | /echo | content-length | short | 8 | 0 | 8 | 256 | 5s | 50819 | 9966.67 | 22.410 | 41.760 | 0.99MB | 0 |
