[English](10-performance.md) | [한국어](10-performance.ko.md)

# Performance Characteristics and Tuning Guide

## 1. Benchmark Results

### Small Message (64B) Throughput

| Pattern | TCP | IPC | inproc |
|------|-----|-----|--------|
| DEALER↔DEALER | 6.03 M/s | 5.96 M/s | 5.96 M/s |
| PAIR | 5.78 M/s | 5.65 M/s | 6.09 M/s |
| PUB/SUB | 5.76 M/s | 5.70 M/s | 5.71 M/s |
| DEALER↔ROUTER | 5.40 M/s | 5.55 M/s | 5.40 M/s |
| ROUTER↔ROUTER | 5.03 M/s | 5.12 M/s | 4.71 M/s |

> **~99% throughput parity** compared to standard libzmq.

## 2. WS/WSS Optimization Results

| Message Size | WS Improvement | WSS Improvement |
|------------|-----------|-----------|
| 64B | +11% | +13% |
| 1KB | +50% | +37% |
| 64KB | +97% | +54% |
| 262KB | +139% | +62% |

### Compared to Beast Library Standalone

| Transport | Beast | zlink | Ratio |
|-----------|-------|-------|------|
| tcp | 1416 MB/s | 1493 MB/s | 105% |
| ws | 540 MB/s | 696 MB/s | 129% |

> For details on WS/WSS internal optimizations (copy elimination, gather write), see [STREAM Socket Optimization](../internals/stream-socket.md).

## 3. Throughput Guidelines by Message Size

| Message Size | Characteristics | Recommendation |
|------------|------|-----------|
| ≤33B | VSM (inline) | No memory allocation. Maximum throughput |
| 34B~1KB | LMSG (small) | Normal performance. Negligible copy overhead |
| 1KB~64KB | LMSG (medium) | Consider zero-copy (`zlink_msg_init_data`) |
| >64KB | LMSG (large) | Zero-copy essential. WS/WSS leverages gather write |

### Leveraging VSM

Messages of 33 bytes or less are stored directly inside the `msg_t` structure, processed **without malloc**.

```c
/* VSM: ≤33B → inline storage, maximum efficiency */
zlink_send(socket, "small msg", 9, 0);

/* LMSG: ≥34B → heap allocation */
char large[1024];
zlink_send(socket, large, sizeof(large), 0);
```

When designing protocols, keeping frequently exchanged messages within 33B maximizes throughput.

## 4. Performance Characteristics by Transport

| Transport | Relative Performance | Latency | Overhead | Recommended Use |
|-----------|-----------|---------|----------|-----------|
| inproc | ★★★★★ | Lowest | None | Inter-thread communication |
| ipc | ★★★★☆ | Low | System calls | Local inter-process |
| tcp | ★★★★☆ | Network | TCP stack | Server-to-server communication |
| ws | ★★★☆☆ | Network | WebSocket framing | Web clients |
| tls/wss | ★★★☆☆ | Network | Encryption + framing | When security is required |

### Overhead Analysis by Transport

```
inproc:  Lock-free pipe direct connection. No system calls.
ipc:     Unix domain socket. Bypasses TCP stack.
tcp:     TCP/IP stack. Nagle disabled to minimize latency.
ws:      tcp + WebSocket framing (2~14B header). Binary mode.
wss/tls: ws/tcp + TLS encryption. Handshake + record overhead.
```

## 5. I/O Thread Count Configuration Guide

```c
void *ctx = zlink_ctx_new();
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);
```

| I/O Threads | Recommended Use Case | Guideline |
|------------|---------------|------|
| 1 | Small-scale connections (<100), simple patterns | Uses 1 CPU core |
| 2 (default) | General use | Suitable for most scenarios |
| 4 | Large-scale connections, high throughput | 4+ CPU cores |
| Core count | Maximum throughput | Dedicated server |

### When to Increase I/O Threads

- When sockets x average message rate exceeds single-thread throughput
- When handling many concurrent network connections (>100)
- When heavily using transports with high framing overhead such as WS/WSS

### Notes

- I/O threads must be configured after context creation but **before** socket creation
- inproc transport does not use I/O threads (direct pipe connection)
- Excessively increasing I/O threads causes context switching overhead

## 6. HWM (High Water Mark) Configuration Guide

```c
int hwm = 10000;
zlink_setsockopt(socket, ZLINK_SNDHWM, &hwm, sizeof(hwm));
zlink_setsockopt(socket, ZLINK_RCVHWM, &hwm, sizeof(hwm));
```

| Setting | Default | Description |
|------|--------|------|
| `ZLINK_SNDHWM` | 1000 | Maximum messages in the send queue |
| `ZLINK_RCVHWM` | 1000 | Maximum messages in the receive queue |

### Memory vs Throughput Trade-off

| HWM Value | Memory Usage | Throughput | Message Loss |
|--------|-----------|--------|:----------:|
| 100 | Low | Low (frequent blocking) | PUB: frequent drops |
| 1000 (default) | Moderate | Moderate | Balanced |
| 10000 | High | High (absorbs bursts) | PUB: fewer drops |
| 100000 | Very high | Maximum | Watch memory usage |

### HWM Behavior by Socket Type

| Socket | Behavior When HWM Exceeded |
|------|-----------------|
| PUB | Messages **dropped** (slow subscriber protection) |
| DEALER | **Blocks** (default) or `EAGAIN` (`ZLINK_DONTWAIT`) |
| ROUTER | `EHOSTUNREACH` with `ROUTER_MANDATORY`, otherwise drops |
| PAIR | **Blocks** (default) or `EAGAIN` |

### Memory Calculation

```
Estimated memory = SNDHWM × average_message_size × connection_count

Example: HWM=10000, message=1KB, connections=100
    = 10000 × 1KB × 100 = ~1GB
```

## 7. Socket Option Tuning Checklist

| Option | Default | Tuning Point |
|------|--------|-------------|
| `ZLINK_LINGER` | -1 (infinite) | Testing: 0, Production: 1000~5000ms |
| `ZLINK_SNDTIMEO` | -1 (infinite) | Set according to response time requirements |
| `ZLINK_RCVTIMEO` | -1 (infinite) | Set when used in polling loops |
| `ZLINK_SNDHWM` | 1000 | Adjust to match throughput |
| `ZLINK_RCVHWM` | 1000 | Adjust to match throughput |
| `ZLINK_MAXMSGSIZE` | -1 (unlimited) | Set for security on STREAM sockets |

### LINGER Setting

```c
/* Test environment: terminate immediately */
int linger = 0;
zlink_setsockopt(socket, ZLINK_LINGER, &linger, sizeof(linger));

/* Production: wait for unsent messages */
int linger = 3000;  /* 3 seconds */
zlink_setsockopt(socket, ZLINK_LINGER, &linger, sizeof(linger));
```

### Timeout Settings

```c
/* Send timeout: EAGAIN after 1 second */
int timeout = 1000;
zlink_setsockopt(socket, ZLINK_SNDTIMEO, &timeout, sizeof(timeout));

/* Receive timeout: EAGAIN after 500ms */
int timeout = 500;
zlink_setsockopt(socket, ZLINK_RCVTIMEO, &timeout, sizeof(timeout));
```

## 8. How to Measure Performance

### Basic Throughput Measurement

```c
#include <time.h>

int count = 100000;
struct timespec start, end;
clock_gettime(CLOCK_MONOTONIC, &start);

for (int i = 0; i < count; i++) {
    zlink_send(socket, data, size, 0);
}

clock_gettime(CLOCK_MONOTONIC, &end);
double elapsed = (end.tv_sec - start.tv_sec) +
                 (end.tv_nsec - start.tv_nsec) / 1e9;

printf("Throughput: %.2f msg/s\n", count / elapsed);
printf("Throughput: %.2f MB/s\n", (count * size) / elapsed / 1e6);
```

### Latency Measurement (Ping-Pong)

```c
/* Client */
clock_gettime(CLOCK_MONOTONIC, &start);
zlink_send(socket, "ping", 4, 0);
zlink_recv(socket, buf, sizeof(buf), 0);
clock_gettime(CLOCK_MONOTONIC, &end);

double rtt_us = ((end.tv_sec - start.tv_sec) * 1e6 +
                 (end.tv_nsec - start.tv_nsec) / 1e3);
printf("RTT: %.1f us\n", rtt_us);
```

## 9. Performance Checklist

### Basic Configuration

- [ ] Set I/O thread count to match workload
- [ ] Adjust HWM to match expected throughput
- [ ] Set LINGER appropriately (testing: 0, production: timeout)

### Message Optimization

- [ ] Leverage VSM for small messages (≤33B) (inline storage)
- [ ] Use zero-copy (`zlink_msg_init_data`) for large messages
- [ ] Use `zlink_send_const()` for constant data
- [ ] Avoid unnecessary `zlink_msg_copy()` calls

### Transport Optimization

- [ ] Use inproc/ipc for local communication
- [ ] Use tcp for internal communication that does not require encryption
- [ ] Consider performance characteristics by message size when using WS/WSS

### Monitoring

- [ ] Use the monitoring API to check connection status during performance bottlenecks
- [ ] Detect slow subscribers (in PUB/SUB environments)
- [ ] Observe HWM saturation frequency

> For details on internal optimization mechanisms such as speculative I/O and gather write, see [architecture.md](../internals/architecture.md).

---
[← Message API](09-message-api.md)
