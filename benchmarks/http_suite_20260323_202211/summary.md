# HTTP Benchmark Summary

| Tool | Case | Endpoint | Body | Conn | IO Threads | Session | Client Threads | Connections | Duration/Requests | Total Requests | RPS | Avg Latency (ms) | P99 (ms) | Transfer/s | Errors |
| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- | --- |
| wrk | wrk_ping_io1_c256 | /ping | none | keepalive | 1 | 0 | 8 | 256 | 5s | 128595 | 25531.30 | 9.940 | 13.090 | 2.80MB | 0 |
| wrk | wrk_ping_io4_c256 | /ping | none | keepalive | 4 | 0 | 8 | 256 | 5s | 258259 | 50638.82 | 5.000 | 9.790 | 5.55MB | 0 |
| wrk | wrk_ping_io8_c256 | /ping | none | keepalive | 8 | 0 | 8 | 256 | 5s | 302160 | 59251.46 | 4.260 | 9.140 | 6.50MB | 0 |
| wrk | wrk_ping_io16_c256 | /ping | none | keepalive | 16 | 0 | 8 | 256 | 5s | 264920 | 52332.57 | 5.040 | 16.120 | 5.74MB | 0 |
| wrk | wrk_ping_c64 | /ping | none | keepalive | 8 | 0 | 8 | 64 | 5s | 273310 | 53593.26 | 1.180 | 2.860 | 5.88MB | 0 |
| wrk | wrk_ping_c256 | /ping | none | keepalive | 8 | 0 | 8 | 256 | 5s | 307741 | 61189.23 | 4.140 | 8.870 | 6.71MB | 0 |
| wrk | wrk_ping_c1024 | /ping | none | keepalive | 8 | 0 | 8 | 1024 | 5s | 255648 | 50220.85 | 19.910 | 38.490 | 5.51MB | 0 |
| wrk | wrk_echo_content_length_c256 | /echo | content-length | keepalive | 8 | 0 | 8 | 256 | 5s | 252517 | 50205.84 | 5.050 | 10.080 | 5.89MB | 0 |
| wrk | wrk_echo_chunked_c256 | /echo | chunked | keepalive | 8 | 0 | 8 | 256 | 5s | 261570 | 51975.62 | 4.880 | 9.900 | 6.10MB | 0 |
| wrk | wrk_ping_session_on_c256 | /ping | none | keepalive | 8 | 1 | 8 | 256 | 5s | 223890 | 43897.93 | 5.790 | 13.310 | 7.14MB | 0 |
| wrk | wrk_ping_short_c256 | /ping | none | short | 8 | 0 | 8 | 256 | 5s | 56021 | 11092.05 | 21.000 | 32.500 | 0.92MB | 0 |
| wrk | wrk_echo_short_c256 | /echo | content-length | short | 8 | 0 | 8 | 256 | 5s | 55183 | 10837.04 | 20.930 | 28.770 | 0.98MB | 0 |
