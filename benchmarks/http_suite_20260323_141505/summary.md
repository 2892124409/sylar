# HTTP Benchmark Summary

| Tool | Case | Endpoint | Body | Conn | IO Threads | Session | Client Threads | Connections | Duration/Requests | Total Requests | RPS | Avg Latency (ms) | P99 (ms) | Transfer/s | Errors |
| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- | --- |
| wrk | wrk_ping_io1_c256 | /ping | none | keepalive | 1 | 0 | 8 | 256 | 5s | 122688 | 24056.90 | 10.530 | 13.910 | 2.64MB | 0 |
| wrk | wrk_ping_io4_c256 | /ping | none | keepalive | 4 | 0 | 8 | 256 | 5s | 238633 | 46792.05 | 5.390 | 10.160 | 5.13MB | 0 |
| wrk | wrk_ping_io8_c256 | /ping | none | keepalive | 8 | 0 | 8 | 256 | 5s | 284737 | 56718.74 | 4.500 | 9.860 | 6.22MB | 0 |
| wrk | wrk_ping_io16_c256 | /ping | none | keepalive | 16 | 0 | 8 | 256 | 5s | 224183 | 44068.42 | 6.140 | 24.380 | 4.83MB | 0 |
| wrk | wrk_ping_c64 | /ping | none | keepalive | 8 | 0 | 8 | 64 | 5s | 243998 | 47844.84 | 1.340 | 3.600 | 5.25MB | 0 |
| wrk | wrk_ping_c256 | /ping | none | keepalive | 8 | 0 | 8 | 256 | 5s | 287206 | 56874.20 | 4.450 | 9.540 | 6.24MB | 0 |
| wrk | wrk_ping_c1024 | /ping | none | keepalive | 8 | 0 | 8 | 1024 | 5s | 274127 | 53859.17 | 18.560 | 35.490 | 5.91MB | 0 |
| wrk | wrk_echo_content_length_c256 | /echo | content-length | keepalive | 8 | 0 | 8 | 256 | 5s | 266422 | 53040.90 | 4.780 | 9.580 | 6.68MB | 0 |
| wrk | wrk_echo_chunked_c256 | /echo | chunked | keepalive | 8 | 0 | 8 | 256 | 5s | 238549 | 46793.70 | 5.370 | 10.420 | 5.49MB | 0 |
| wrk | wrk_ping_session_on_c256 | /ping | none | keepalive | 8 | 1 | 8 | 256 | 5s | 213573 | 41878.70 | 6.100 | 14.490 | 6.81MB | 0 |
| wrk | wrk_ping_short_c256 | /ping | none | short | 8 | 0 | 8 | 256 | 5s | 52270 | 10368.57 | 21.500 | 29.150 | 0.86MB | connect 0, read 5304, write 0, timeout 0 |
| wrk | wrk_echo_short_c256 | /echo | content-length | short | 8 | 0 | 8 | 256 | 5s | 52690 | 10472.25 | 21.340 | 27.200 | 1.04MB | connect 0, read 5312, write 0, timeout 0 |
