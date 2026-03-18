[English](10-performance.md) | [한국어](10-performance.ko.md)

# Performance Characteristics and Tuning Guide

## 1. Performance Characteristics by Transport

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

## 2. I/O Thread Count Configuration Guide

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

## 3. HWM (High Water Mark) Configuration Guide

HWM limits the **per-connection queue size**. In zlink, each connection (pipe) has its own independent send and receive queue; HWM sets the maximum number of messages each queue can hold.

```c
int hwm = 100;
zlink_setsockopt(socket, ZLINK_SNDHWM, &hwm, sizeof(hwm));
zlink_setsockopt(socket, ZLINK_RCVHWM, &hwm, sizeof(hwm));
```

| Setting | Default | Description |
|------|--------|------|
| `ZLINK_SNDHWM` | 1000 | Maximum messages in each connection's send queue |
| `ZLINK_RCVHWM` | 1000 | Maximum messages in each connection's receive queue |

### Backpressure Behavior

When HWM is reached, behavior depends on the socket type and send flags:

- **Blocking send** (`flags=0`): `zlink_send()` blocks until space becomes available in the send queue. Use `ZLINK_SNDTIMEO` to limit the wait.
- **Non-blocking send** (`ZLINK_DONTWAIT`): Returns `EAGAIN` immediately. The application decides whether to retry, drop, or buffer externally.

> For detailed flow control patterns (DONTWAIT + send-ready handler), see
> [Send and Receive Flow Control](#4-send-and-receive-flow-control) below.

### Recovery Mechanism (LWM)

When the send queue reaches HWM, the connection's pipe becomes non-writable. Once the receiver consumes enough messages to drain the queue to or below the **Low Water Mark (LWM)**, an `activate_write` signal fires and the pipe becomes writable again.

LWM formula: **`(HWM + 1) / 2`**

At this point:
- Blocking `zlink_send()` calls resume.
- The send-ready handler fires (if installed).

This hysteresis prevents rapid oscillation between writable and non-writable states.

```
Example: HWM = 100
    → LWM = (100 + 1) / 2 = 50
    → Queue blocks when it reaches 100
    → Receiver consumes messages, queue drops to ≤50 → resumes
```

### Practical HWM Recommendations

| Scenario | Recommended HWM | Rationale |
|----------|-----------------|-----------|
| Regular sockets/services | ~100 | Limits per-connection memory while absorbing bursts |
| STREAM (1000+ CCU) | ~10 | Caps total memory proportional to connection count |
| Default | 1000 | Sufficient for small-scale connections; adjust as connections grow |

### HWM Behavior by Socket Type

| Socket | Behavior When HWM Exceeded |
|------|-----------------|
| PUB | Messages **dropped** (slow subscriber protection) |
| DEALER | **Blocks** (default) or `EAGAIN` (`ZLINK_DONTWAIT`) |
| ROUTER | `EHOSTUNREACH` with `ROUTER_MANDATORY`, otherwise drops |
| PAIR | **Blocks** (default) or `EAGAIN` |

### Memory Calculation

Since HWM is per-connection, total memory is HWM × message size × connection count.

```
Estimated memory = SNDHWM × average_message_size × connection_count

Example 1: Regular service — HWM=100, message=1KB, connections=1000
           = 100 × 1KB × 1000 = ~100MB

Example 2: STREAM at scale — HWM=10, message=1KB, connections=10000
           = 10 × 1KB × 10000 = ~100MB
```

## 4. Send and Receive Flow Control

### 4.1 Send Backpressure

When a sender produces messages faster than the receiver can consume them,
messages accumulate in the send queue. The High Water Mark (HWM) limits
queue depth. What happens when HWM is reached depends on the socket type
and send flags (see [HWM Behavior by Socket Type](#hwm-behavior-by-socket-type) above).

#### Blocking Send (Default)

With `flags=0`, `zlink_send()` blocks until space becomes available in the
send queue. Use `ZLINK_SNDTIMEO` to limit how long the call blocks.

| SNDTIMEO | Behavior |
|---|---|
| -1 (default) | Block indefinitely |
| 0 | Return `EAGAIN` immediately (same as `ZLINK_DONTWAIT`) |
| N (ms) | Block up to N milliseconds, then return `EAGAIN` |

```c
/* Block for at most 1 second */
int timeout = 1000;
zlink_setsockopt(socket, ZLINK_SNDTIMEO, &timeout, sizeof(timeout));

zlink_msg_t part;
zlink_msg_init_size(&part, size);
memcpy(zlink_msg_data(&part), data, size);
int rc = zlink_send(socket, &part, 1, 0);
if (rc == -1 && zlink_errno() == EAGAIN) {
    /* Timed out — queue is still full */
    zlink_msg_close(&part);
}
```

#### Non-Blocking Send (DONTWAIT)

Pass `ZLINK_DONTWAIT` to return immediately with `EAGAIN` when the HWM is
reached. The application decides whether to retry, drop, or buffer
externally.

```c
zlink_msg_t part;
zlink_msg_init_size(&part, size);
memcpy(zlink_msg_data(&part), data, size);
int rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
if (rc == -1 && zlink_errno() == EAGAIN) {
    /* HWM reached — handle backpressure */
    zlink_msg_close(&part);
}
```

#### Send-Ready Handler (Event-Driven Backpressure)

`zlink_socket_send_ready_handler()` installs a callback that fires
when the socket transitions from non-writable to writable. Combined with
`ZLINK_DONTWAIT`, this enables reactive flow control:

1. Send with `ZLINK_DONTWAIT`.
2. On `EAGAIN`, pause sending.
3. When the send-ready callback fires, resume sending.

The same API is available on Gateway, SPOT, and SPOT Node handles.

**Constraints:**
- Replace-only: passing `NULL` is invalid.
- Cannot be replaced from within its own callback (`EDEADLK`).

```c
typedef struct {
    void *socket;
    const char *pending_data;
    size_t pending_size;
} app_state_t;

void on_send_ready(void *subject, void *userdata)
{
    app_state_t *state = (app_state_t *)userdata;
    if (state->pending_data) {
        zlink_msg_t part;
        zlink_msg_init_size(&part, state->pending_size);
        memcpy(zlink_msg_data(&part), state->pending_data, state->pending_size);
        int rc = zlink_send(state->socket, &part, 1, ZLINK_DONTWAIT);
        if (rc >= 0)
            state->pending_data = NULL;
        else
            zlink_msg_close(&part);
        /* If still EAGAIN, callback will fire again on next transition */
    }
}

/* Install the handler */
app_state_t state = { .socket = socket };
zlink_socket_send_ready_handler(socket, on_send_ready, &state);

/* Send loop */
zlink_msg_t part;
zlink_msg_init_size(&part, size);
memcpy(zlink_msg_data(&part), data, size);
int rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
if (rc == -1 && zlink_errno() == EAGAIN) {
    zlink_msg_close(&part);
    /* Buffer for retry when send-ready fires */
    state.pending_data = data;
    state.pending_size = size;
}
```

### 4.2 Low Water Mark and Wake-Up

When the send queue reaches HWM, the socket becomes non-writable. It
transitions back to writable when the queue drains to the **low water
mark**, which is `(HWM + 1) / 2`. At this point:

- Blocking `zlink_send()` calls resume.
- The send-ready handler fires (if installed and armed).

This hysteresis prevents rapid oscillation between writable and
non-writable states.

### 4.3 Receive-Side Flow Control

The receive queue holds at most `ZLINK_RCVHWM` messages. When the
receiver's queue is full, pipe-level backpressure is applied to the
sender.

```c
int hwm = 500;
zlink_setsockopt(socket, ZLINK_RCVHWM, &hwm, sizeof(hwm));
```

In callback mode, a slow callback blocks the I/O thread, which causes the
receive queue to fill up. To avoid this, offload heavy work to a separate
thread:

```c
void on_message(const zlink_routing_id_t *rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* BAD: slow processing blocks I/O thread */
    // heavy_computation(parts);

    /* GOOD: enqueue and return quickly */
    work_queue_push(userdata, parts, part_count);
}
```

> For thread-safe work queue patterns, see
> [Thread-Safety Guide](11-thread-safety.md) section 6.

### 4.4 Callback vs Pull Mode

zlink sockets support two receive modes. The choice affects threading
and flow control behavior.

| | Callback Mode | Pull Mode |
|---|---|---|
| Trigger | Automatic on message arrival | Explicit `zlink_recv()` call |
| Execution thread | I/O thread | Application thread |
| Transition | One-way (permanent) | Default; unavailable after handler attach |
| DONTWAIT | N/A (always async) | Returns `EAGAIN` if no message |
| Multipart | All parts delivered as `parts[]` array | All parts returned via `parts_out` + `part_count_out` |

### 4.5 Complete Backpressure Example

A full example combining `ZLINK_DONTWAIT`, a send-ready handler, and an
application-level buffer:

```c
#include <zlink.h>
#include <string.h>
#include <stdio.h>

#define MAX_PENDING 1024

typedef struct {
    void *socket;
    char *queue[MAX_PENDING];
    size_t sizes[MAX_PENDING];
    int head, tail, count;
} sender_t;

static void flush_queue(sender_t *s)
{
    while (s->count > 0) {
        zlink_msg_t part;
        zlink_msg_init_size(&part, s->sizes[s->head]);
        memcpy(zlink_msg_data(&part), s->queue[s->head], s->sizes[s->head]);
        int rc = zlink_send(s->socket, &part, 1, ZLINK_DONTWAIT);
        if (rc == -1) {
            zlink_msg_close(&part);
            break; /* Still full — wait for next send-ready */
        }
        free(s->queue[s->head]);
        s->head = (s->head + 1) % MAX_PENDING;
        s->count--;
    }
}

static void on_send_ready(void *subject, void *userdata)
{
    flush_queue((sender_t *)userdata);
}

int main(void)
{
    void *ctx = zlink_ctx_new();
    void *socket = zlink_socket(ctx, ZLINK_DEALER);
    zlink_connect(socket, "tcp://127.0.0.1:5555");

    sender_t sender = { .socket = socket };
    zlink_socket_send_ready_handler(socket, on_send_ready, &sender);

    for (int i = 0; i < 100000; i++) {
        char msg[64];
        int len = snprintf(msg, sizeof(msg), "msg-%d", i);

        zlink_msg_t part;
        zlink_msg_init_size(&part, len);
        memcpy(zlink_msg_data(&part), msg, len);
        int rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
        if (rc == -1 && zlink_errno() == EAGAIN) {
            zlink_msg_close(&part);
            /* Enqueue for later delivery */
            if (sender.count < MAX_PENDING) {
                int idx = (sender.head + sender.count) % MAX_PENDING;
                sender.queue[idx] = strdup(msg);
                sender.sizes[idx] = len;
                sender.count++;
            } else {
                printf("Application buffer full — dropping message\n");
            }
        }
    }

    zlink_close(socket);
    zlink_ctx_term(ctx);
    return 0;
}
```

## 5. Socket Option Tuning Checklist

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

## 6. How to Measure Performance

### Basic Throughput Measurement

```c
#include <time.h>

int count = 100000;
struct timespec start, end;
clock_gettime(CLOCK_MONOTONIC, &start);

for (int i = 0; i < count; i++) {
    zlink_msg_t part;
    zlink_msg_init_size(&part, size);
    memcpy(zlink_msg_data(&part), data, size);
    zlink_send(socket, &part, 1, 0);
}

clock_gettime(CLOCK_MONOTONIC, &end);
double elapsed = (end.tv_sec - start.tv_sec) +
                 (end.tv_nsec - start.tv_nsec) / 1e9;

printf("Throughput: %.2f msg/s\n", count / elapsed);
printf("Throughput: %.2f MB/s\n", (count * size) / elapsed / 1e6);
```

### Latency Measurement (Ping-Pong)

```c
/* Client: send ping, measure until pong arrives in callback */
clock_gettime(CLOCK_MONOTONIC, &start);
zlink_msg_t ping;
zlink_msg_init_size(&ping, 4);
memcpy(zlink_msg_data(&ping), "ping", 4);
zlink_send(socket, &ping, 1, 0);

/* Handler callback receives "pong" reply and records end time */
void on_pong(const zlink_routing_id_t *source_rid,
             zlink_msg_t *parts, size_t part_count)
{
    clock_gettime(CLOCK_MONOTONIC, &end);
    double rtt_us = ((end.tv_sec - start.tv_sec) * 1e6 +
                     (end.tv_nsec - start.tv_nsec) / 1e3);
    printf("RTT: %.1f us\n", rtt_us);
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

## 7. Performance Checklist

### Basic Configuration

- [ ] Set I/O thread count to match workload
- [ ] Adjust HWM to match expected throughput
- [ ] Set LINGER appropriately (testing: 0, production: timeout)

### Message Optimization

- [ ] Leverage VSM for small messages (≤33B) (inline storage)
- [ ] Use zero-copy (`zlink_msg_init_data`) for large messages
- [ ] For constant/static payloads, use `zlink_msg_init_data(..., NULL, NULL)` carefully
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
[← Message API](09-message-api.md) | [Thread-Safety →](11-thread-safety.md)
