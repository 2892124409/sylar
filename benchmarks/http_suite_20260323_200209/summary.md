# HTTP Benchmark Summary

| Tool | Case | Endpoint | Body | Conn | IO Threads | Session | Client Threads | Connections | Duration/Requests | Total Requests | RPS | Avg Latency (ms) | P99 (ms) | Transfer/s | Errors |
| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- | --- |
| wrk | wrk_ping_io1_c256 | /ping | none | keepalive | 1 | 0 | 8 | 256 | 5s | 125280 | 24805.44 | 10.210 | 16.100 | 2.72MB | 0 |
| wrk | wrk_ping_io4_c256 | /ping | none | keepalive | 4 | 0 | 8 | 256 | 5s | 239684 | 46993.32 | 5.410 | 10.430 | 5.15MB | 0 |
| wrk | wrk_ping_io8_c256 | /ping | none | keepalive | 8 | 0 | 8 | 256 | 5s | 320576 | 63492.39 | 3.980 | 8.850 | 6.96MB | 0 |
| wrk | wrk_ping_io16_c256 | /ping | none | keepalive | 16 | 0 | 8 | 256 | 5s | 226886 | 44801.14 | 6.020 | 23.500 | 4.91MB | 0 |
| wrk | wrk_ping_c64 | /ping | none | keepalive | 8 | 0 | 8 | 64 | 5s | 246144 | 48279.61 | 1.320 | 3.690 | 5.29MB | 0 |
| wrk | wrk_ping_c256 | /ping | none | keepalive | 8 | 0 | 8 | 256 | 5s | 306054 | 60703.73 | 4.180 | 9.040 | 6.66MB | 0 |
| wrk | wrk_ping_c1024 | /ping | none | keepalive | 8 | 0 | 8 | 1024 | 5s | 284609 | 56184.36 | 17.820 | 34.550 | 6.16MB | 0 |
| wrk | wrk_echo_content_length_c256 | /echo | content-length | keepalive | 8 | 0 | 8 | 256 | 5s | 232912 | 45672.23 | 5.570 | 12.520 | 5.75MB | 0 |
| wrk | wrk_echo_chunked_c256 | /echo | chunked | keepalive | 8 | 0 | 8 | 256 | 5s | 285220 | 55921.76 | 4.530 | 9.820 | 6.56MB | 0 |
| wrk | wrk_ping_session_on_c256 | /ping | none | keepalive | 8 | 1 | 8 | 256 | 5s | 206573 | 40505.95 | 6.270 | 13.670 | 6.58MB | 0 |
| wrk | wrk_ping_short_c256 | /ping | none | short | 8 | 0 | 8 | 256 | 5s | 54960 | 12094.31 | 21.430 | 34.430 | 1.00MB | connect 0, read 0, write 0, timeout 252 |
| wrk | wrk_echo_short_c256 | /echo | content-length | short | 8 | 0 | 8 | 256 | 5s | 53946 | 10640.60 | 21.180 | 31.000 | 1.06MB | 0 |
