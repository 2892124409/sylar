# HTTP Benchmark Summary

| Tool | Case | Endpoint | Body | Conn | IO Threads | Session | Client Threads | Connections | Duration/Requests | Total Requests | RPS | Avg Latency (ms) | P99 (ms) | Transfer/s | Errors |
| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- | --- |
| wrk | wrk_ping_io1_c256 | /ping | none | keepalive | 1 | 0 | 8 | 256 | 5s | 122894 | 24095.61 | 10.480 | 14.530 | 2.64MB | 0 |
| wrk | wrk_ping_io4_c256 | /ping | none | keepalive | 4 | 0 | 8 | 256 | 5s | 233052 | 45692.67 | 5.520 | 11.090 | 5.01MB | 0 |
| wrk | wrk_ping_io8_c256 | /ping | none | keepalive | 8 | 0 | 8 | 256 | 5s | 284326 | 55765.37 | 4.500 | 9.750 | 6.12MB | 0 |
| wrk | wrk_ping_io16_c256 | /ping | none | keepalive | 16 | 0 | 8 | 256 | 5s | 205785 | 40697.78 | 6.640 | 22.780 | 4.46MB | 0 |
| wrk | wrk_ping_c64 | /ping | none | keepalive | 8 | 0 | 8 | 64 | 5s | 233112 | 45705.91 | 1.400 | 3.290 | 5.01MB | 0 |
| wrk | wrk_ping_c256 | /ping | none | keepalive | 8 | 0 | 8 | 256 | 5s | 276182 | 54166.26 | 4.640 | 9.990 | 5.94MB | 0 |
| wrk | wrk_ping_c1024 | /ping | none | keepalive | 8 | 0 | 8 | 1024 | 5s | 277595 | 54575.53 | 18.360 | 37.130 | 5.99MB | 0 |
| wrk | wrk_echo_content_length_c256 | /echo | content-length | keepalive | 8 | 0 | 8 | 256 | 5s | 268073 | 53140.26 | 4.780 | 10.810 | 6.69MB | 0 |
| wrk | wrk_echo_chunked_c256 | /echo | chunked | keepalive | 8 | 0 | 8 | 256 | 5s | 250530 | 49710.44 | 5.090 | 10.440 | 5.83MB | 0 |
| wrk | wrk_ping_session_on_c256 | /ping | none | keepalive | 8 | 1 | 8 | 256 | 5s | 210933 | 41354.83 | 6.210 | 14.090 | 6.72MB | 0 |
