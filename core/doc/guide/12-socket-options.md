[English](12-socket-options.md) | [한국어](12-socket-options.ko.md)

<!-- zlink-nav:start -->
[← Thread Safety](11-thread-safety.md)
<!-- zlink-nav:end -->

# Socket Options Detailed Guide

This document describes the **behavior**, **scope of effect**, **defaults**,
and **per-socket-type differences** of each socket option set via
`zlink_set_option()` / `zlink_get_option()`. Unlike the
[socket API reference](../spec/core/socket/README.md) which covers API signatures,
this guide focuses on **what each option changes at runtime**.

## Option Ownership Categories

Internally, options are classified into three categories:

| Category | Description | Representative Options |
|----------|-------------|----------------------|
| **Core Socket** | Core socket behavior | SNDHWM, RCVHWM, LINGER, SNDTIMEO, RCVTIMEO |
| **Transport/Network** | Network/transport policies | SNDBUF, RCVBUF, TCP_*, TOS, CONNECT_TIMEOUT |
| **Protocol/Metadata** | Protocol-level metadata | ZMP_METADATA, HEARTBEAT_* |

---

## Peer Routing ID Duplicate Policy

`ZLINK_OPT_RID_DUPLICATE_POLICY` controls whether a socket keeps an existing
peer connection or lets a new connection replace it when the same peer routing
id appears again.

| Value | Behavior |
|-------|----------|
| `ZLINK_RID_DUPLICATE_REJECT` | Default. Keep the existing connection and do not register the duplicate connection. |
| `ZLINK_RID_DUPLICATE_HANDOVER` | A reconnect in the same direction replaces the existing connection. If opposite-direction connections collide, both peers compare their routing IDs and converge on the same single direction. |

This is a common socket option used with `zlink_set_option()`. It is the only
public option for duplicate peer takeover.

STREAM is not affected by this policy because the server assigns its own
4-byte routing id for each connection.

```c
int policy = ZLINK_RID_DUPLICATE_HANDOVER;
zlink_set_option(router, ZLINK_OPT_RID_DUPLICATE_POLICY,
                 &policy, sizeof(policy));
```

---

## 1. Message Queue — SNDHWM / RCVHWM

| | |
|---|---|
| **What it does** | Limits the maximum number of messages in the pipe's outbound/inbound direction |
| **Applied at** | `pipe_t::check_write()` — checks HWM when writing to pipe |
| **Default** | Automatic HWM with the balanced profile. If context auto-HWM is disabled, the socket uses `1000`. |
| **0** | Unlimited |
| **Effect** | When HWM is reached, `zlink_send()` blocks or returns `ZLINK_SUBMIT_BACKPRESSURED`. When the receiver consumes messages and the queue drops below LWM, writable state is restored |

**LWM (Low Water Mark) formula:** `(HWM + 1) / 2`

With HWM=100, LWM=50. Queue blocks at 100 and resumes only when drained to 50 or below.
This gap is the hysteresis that prevents writable/non-writable oscillation.

**Per-socket-type:** The meaning stays the same, but the automatic policy class
changes: `PAIR=control`, `DEALER=peer_queue`, `ROUTER=routed`,
`STREAM=stream`, `PUB/XPUB=fanout`, and `SUB/XSUB=recv_ingress`. SPOT
internal topic publishers use `spot_data`, peer/control sockets use `control`,
and SPOT routers use `routed`.

The context option `ZLINK_CTX_OPT_AUTO_HWM_PROFILE` selects one of four
profiles. The default is `ZLINK_AUTO_HWM_PROFILE_BALANCED`, and auto-HWM is
enabled by default. Set `ZLINK_CTX_OPT_AUTO_HWM_ENABLE` to `0` when a context
must keep the legacy fixed HWM default `1000`.

| Socket group | `compact` | `low_latency` | `balanced` | `throughput` |
|---|---:|---:|---:|---:|
| non-STREAM data sockets | 64 | 128 | 256 | 512 |
| STREAM | 8 | 16 | 64 | 256 |
| control | 8 | 16 | 16 | 32 |

For ordinary sockets, the planner treats HWM as a per-connection queue depth.
It does not divide a context memory budget by connection count. Instead, it
keeps the profile's byte envelope stable:

```text
scaled_hwm = ceil(basis_hwm * basis_message_unit / effective_message_unit)
```

The minimum automatic HWM is `1`, and the result is capped by the profile's
message-count cap.

SPOT mesh internal sockets `mesh-pub`, `mesh-xsub`, and `routed-router` first
apply a connection-count bucket when many peers are connected. Bucket values
are HWM counts normalized to 4 KiB messages, and the final HWM is calculated as:

```text
base_hwm_4k = min(profile_hwm_4k, bucket_hwm_4k)
unit_budget_bytes = base_hwm_4k * 4096
scaled_hwm = ceil(unit_budget_bytes / effective_message_unit)
```

Bucket boundaries use hysteresis. A socket currently in the `1-64` bucket moves
to the `65-128` bucket at `80` peers or more. A socket currently in the
`65-128` bucket moves back to `1-64` only at `48` peers or fewer. This gap keeps
HWM from changing repeatedly when peer count oscillates near a boundary. Profile
or message-unit changes force recalculation even when hysteresis would otherwise
retain the current bucket.

This adjustment is an auxiliary bound on SPOT data-plane socket queues. Public
publish and routed-send backpressure semantics are still owned by
`publish_ingress_queue` and `routed_send_queue` admission. Local fanout, pub
ingress, control sockets, and ordinary DEALER/PAIR/STREAM sockets do not use
this connection bucket.

Manual `SNDHWM` / `RCVHWM` settings always override the automatic values.

```c
int sndhwm = 5000;
zlink_set_option(socket, ZLINK_OPT_SNDHWM, &sndhwm, sizeof(sndhwm));
int rcvhwm = 5000;
zlink_set_option(socket, ZLINK_OPT_RCVHWM, &rcvhwm, sizeof(rcvhwm));
```

---

## 2. Automatic HWM Message Unit

`ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES` tells the automatic HWM policy how many
bytes one planned queue slot should represent for sockets in the context. It
is not a maximum message size; `ZLINK_OPT_MAXMSGSIZE` is the inbound size
limit.

Use this context option when the typical payload size for a workload is known
and differs from the default planning size. Leave it at `0` when the
socket-type default is a better description of the workload. The low-level C
socket option `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` is still available when one
raw socket must use a different message unit from the rest of its context.

| Socket type | Default message unit |
|-------------|----------------------|
| `STREAM` | `1024` bytes |
| all other socket types | `4096` bytes |

`zlink_ctx_get()` returns the raw context value. A returned value of `0` means
"use the socket-type default"; the actual value used by the current calculation
is visible in monitor snapshots as
`auto_hwm_effective_message_bytes`.

```c
int msg_unit = 8192;
zlink_ctx_set(ctx, ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES, msg_unit);
```

Negative values fail with `EINVAL` and do not change the existing setting.
When a fixed HWM is set manually with `ZLINK_OPT_SNDHWM` or `ZLINK_OPT_RCVHWM`,
that manual HWM remains in force. If both the context option and the raw socket
option are set, the raw socket option wins for that socket.

### Manual Recalculation Trigger

After changing the auto-HWM profile or message unit at runtime, trigger an
immediate recalculation across all sockets in the context:

```c
zlink_ctx_auto_hwm_recalculate(ctx);
```

This is a no-op when auto HWM is disabled (`ZLINK_CTX_OPT_AUTO_HWM_ENABLE = 0`).
The context-level `ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS` setting
controls how frequently automatic background recalculations run; calling
`zlink_ctx_auto_hwm_recalculate()` bypasses the debounce and runs
immediately.

---

## 3. Shutdown Delay — LINGER

| | |
|---|---|
| **What it does** | Determines how long to wait for pending messages when closing a socket/service |
| **Applied at** | `session_base_t::process_term()` — sets linger timer on pipe termination |
| **Default** | Inherited from context (`BLOCKY=1` → `-1`, otherwise `0`) |
| **-1** | Infinite wait — blocks until all messages are sent |
| **0** | Immediate close — discards pending messages |
| **>0** | Wait up to specified time (ms), then force close |

**Actual behavior:** If linger > 0, a timer is set; when it expires, the pipe is force-terminated. `pipe->terminate(linger != 0)` passes the delay flag.

**Per-socket-type:**
- `XSUB`, `SUB`: Linger is forced to `0` at creation (subscription sockets have nothing to drain on close)

```c
/* Wait up to 1 second for pending messages on close */
int linger = 1000;
zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
```

---

## 4. Timeouts — SNDTIMEO / RCVTIMEO

| | |
|---|---|
| **What it does** | Sets maximum wait time for send/recv operations |
| **Applied at** | `zlink_send()` / `zlink_recv()` blocking paths, logical multipart send module |
| **Default** | `1000` ms |
| **0** | Equivalent to non-blocking (immediate return) |
| **-1** | Infinite wait when explicitly set |
| **>0** | Wait up to specified time (ms), then send returns `ZLINK_SUBMIT_BACKPRESSURED`, recv returns `ZLINK_RECV_NO_DATA` |

**Service application:** Propagated to SPOT pub/sub internal sockets.

```c
/* Give up send/recv after 500 ms */
int sndtimeo = 500;
zlink_set_option(socket, ZLINK_OPT_SNDTIMEO, &sndtimeo, sizeof(sndtimeo));
int rcvtimeo = 500;
zlink_set_option(socket, ZLINK_OPT_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
```

---

## 5. Connection Timeout — CONNECT_TIMEOUT

| | |
|---|---|
| **What it does** | Sets a user-level timeout for async `connect()` attempts |
| **Applied at** | `asio_tcp_connecter`, `asio_tls_connecter`, `asio_ws_connecter` — `add_connect_timer()` |
| **Default** | `0` (disabled — relies on TCP stack default timeout) |
| **>0** | If connection not established within specified time (ms), close socket and trigger reconnect |

**Relation to OS timeout:** The TCP stack's own SYN retransmission timeout (typically ~2 min) is usually much longer. Setting CONNECT_TIMEOUT shorter enables faster failover.

```c
/* Fail connect attempts after 3 seconds */
int timeout = 3000;
zlink_set_option(socket, ZLINK_OPT_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
```

---

## 6. Reconnection — RECONNECT_IVL / RECONNECT_IVL_MAX

| | |
|---|---|
| **What it does** | Controls reconnection attempt intervals after connection failure/disconnection |
| **Applied at** | All connecters (`asio_tcp_connecter`, `asio_ipc_connecter`, etc.) — `get_new_reconnect_ivl()` |
| **RECONNECT_IVL default** | `100` ms |
| **RECONNECT_IVL_MAX default** | `0` (disabled — fixed interval) |

**Reconnection algorithm:**
- `RECONNECT_IVL_MAX == 0`: Fixed interval + random jitter (0 to `RECONNECT_IVL`)
- `RECONNECT_IVL_MAX > 0`: **Exponential backoff** — doubles interval each failure, capped at `RECONNECT_IVL_MAX`. (e.g., 100→200→400→...→max)

**Negative:** `RECONNECT_IVL < 0` disables automatic reconnection entirely.

```c
/* Exponential backoff: 200ms initial, cap at 30s */
int ivl = 200;
zlink_set_option(socket, ZLINK_OPT_RECONNECT_IVL, &ivl, sizeof(ivl));
int ivl_max = 30000;
zlink_set_option(socket, ZLINK_OPT_RECONNECT_IVL_MAX, &ivl_max, sizeof(ivl_max));
```

---

## 7. TCP Keepalive — TCP_KEEPALIVE / TCP_KEEPALIVE_CNT / TCP_KEEPALIVE_IDLE / TCP_KEEPALIVE_INTVL

| Option | What it does | Default |
|--------|-------------|---------|
| `TCP_KEEPALIVE` | Enable/disable SO_KEEPALIVE | `-1` (OS default) |
| `TCP_KEEPALIVE_CNT` | Max probe failures before disconnect | `-1` (OS default) |
| `TCP_KEEPALIVE_IDLE` | Idle time before first probe (seconds) | `-1` (OS default) |
| `TCP_KEEPALIVE_INTVL` | Interval between probes (seconds) | `-1` (OS default) |

**Applied at:** `tcp.cpp`'s `tune_tcp_keepalives()` — passed as OS socket options during socket setup.

**`-1` meaning:** "Do not change this value" — preserves OS default keepalive settings.

**TCP only.** Does not apply to IPC, inproc, or WebSocket.

**Recommended example:**
```c
// Probe after 60s idle, every 10s, 3 probes max, then disconnect
zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE, &(int){1}, sizeof(int));
zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE_IDLE, &(int){60}, sizeof(int));
zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE_INTVL, &(int){10}, sizeof(int));
zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE_CNT, &(int){3}, sizeof(int));
```

---

## 8. TCP Retransmission — TCP_MAXRT

| | |
|---|---|
| **What it does** | Sets maximum TCP segment retransmission timeout |
| **Applied at** | `tcp.cpp` — `setsockopt(TCP_USER_TIMEOUT)` |
| **Default** | `0` (disabled — uses OS TCP stack default) |
| **>0** | Give up retransmission after specified time (ms) |

Only works on systems with `TCP_USER_TIMEOUT` kernel support (Linux 2.6.37+). Useful for faster dead peer detection than keepalive.

---

## 9. Nagle's Algorithm — TCP_NODELAY

| | |
|---|---|
| **What it does** | Disables TCP Nagle's algorithm to send small data immediately |
| **Applied at** | `tcp.cpp` — `setsockopt(TCP_NODELAY)` |
| **Default** | `1` (TCP_NODELAY enabled = Nagle disabled) |

**`1` (default, recommended):** Small messages sent without delay. Optimal for messaging systems.
**`0`:** Nagle enabled — small packets are coalesced. Better bandwidth efficiency but higher latency.

---

## 10. ZMP Heartbeat — HEARTBEAT_IVL / HEARTBEAT_TTL / HEARTBEAT_TIMEOUT

| Option | What it does | Default |
|--------|-------------|---------|
| `HEARTBEAT_IVL` | PING message send interval (ms) | `0` (disabled) |
| `HEARTBEAT_TTL` | TTL transmitted to remote peer (ms; stored internally in 0.1s units) | `0` |
| `HEARTBEAT_TIMEOUT` | PONG response wait time (ms) | `-1` (uses IVL value) |

**Applied at:** `asio_zmp_engine` — ZMP protocol-level PING/PONG exchange.

**Flow:**
1. Send PING every `IVL` milliseconds (includes TTL value)
2. Remote peer closes connection if no message/PONG received within TTL
3. Locally, disconnection detected if no PONG within TIMEOUT

**vs. TCP Keepalive:** TCP keepalive is OS-level probing; ZMP heartbeat is application-protocol-level. If both are configured, the faster one detects failure first.

```c
/* PING every 5s, remote TTL 15s, local PONG timeout 10s */
int hb_ivl = 5000;
zlink_set_option(socket, ZLINK_OPT_HEARTBEAT_IVL, &hb_ivl, sizeof(hb_ivl));
int hb_ttl = 15000;  /* ms → 15s */
zlink_set_option(socket, ZLINK_OPT_HEARTBEAT_TTL, &hb_ttl, sizeof(hb_ttl));
int hb_timeout = 10000;
zlink_set_option(socket, ZLINK_OPT_HEARTBEAT_TIMEOUT, &hb_timeout, sizeof(hb_timeout));
```

---

## 11. Immediate Connect — IMMEDIATE

| | |
|---|---|
| **What it does** | Controls whether pipes are attached immediately or after connection completion |
| **Applied at** | `socket_base_endpoint.cpp` — at pipe creation time |
| **Default** | `0` (immediate attach) |

**`0` (default):** Pipe attached immediately on `connect()`. `send()` is possible before connection completes; messages queue up.

**`1`:** Pipe attached only after connection actually completes. `send()` before connection blocks or returns `ZLINK_SUBMIT_BACKPRESSURED`. Also, on hiccup (temporary disconnection), pipe is immediately removed.

---

## 12. Keep Latest Only — CONFLATE

| | |
|---|---|
| **What it does** | Keeps only the most recent message per pipe, discarding older ones |
| **Applied at** | `pipe.cpp` — uses `ypipe_conflate_t` |
| **Default** | `0` (disabled) |
| **Valid sockets** | `DEALER`, `PUB`, `SUB` only |

When enabled, HWM settings are ignored. Multipart messages cannot be received in conflate mode. Suitable for "only latest value matters" scenarios like sensor data.

---

## 13. OS Socket Buffers — SNDBUF / RCVBUF

| | |
|---|---|
| **What it does** | Sets kernel-level socket send/receive buffer sizes |
| **Applied at** | `tcp.cpp` — `setsockopt(SO_SNDBUF/SO_RCVBUF)` |
| **Default** | `-1` (keep OS default) |
| **>=0** | Request the specified size from the OS, in bytes |

Independent of HWM. HWM limits message count in the zlink pipe; SNDBUF/RCVBUF limits byte size in the OS kernel socket buffer.

Auto-HWM profiles and STREAM defaults do not change these values automatically.
For large meshes where connection count and memory ceilings matter, applications
or deployment settings can set smaller explicit values.

---

## 14. IP Quality of Service — TOS

| | |
|---|---|
| **What it does** | Sets the IP Type-of-Service (DSCP/ECN) field |
| **Applied at** | TCP socket setup via `setsockopt(IP_TOS)` |
| **Default** | `0` (best-effort) |

Used to set traffic priority in networks with QoS policies.

---

## 15. Connection Queue — BACKLOG

| | |
|---|---|
| **What it does** | Sets the maximum length of the `listen()` accept queue |
| **Applied at** | `asio_tcp_listener.cpp` — `acceptor.listen(backlog)` |
| **Default** | `100` |

**Per-socket-type:**
- `STREAM`: Auto-overridden to `65536` (for many external clients)

---

## 16. I/O Thread Affinity — AFFINITY

| | |
|---|---|
| **What it does** | Assigns socket I/O operations to specific I/O threads |
| **Applied at** | `socket_base` — `choose_io_thread(affinity)` |
| **Default** | `0` (all I/O threads available) |
| **Type** | `uint64_t` bitmask |

Bit N set to 1 means I/O thread N is available. `0` allows all threads. Useful for CPU affinity when using multiple I/O threads (`ZLINK_IO_THREADS > 1`).

---

## 17. Maximum Message Size — MAXMSGSIZE

| | |
|---|---|
| **What it does** | Limits the maximum size of incoming messages |
| **Applied at** | Session/engine level message size validation |
| **Default** | `-1` (unlimited) |
| **>0** | Reject messages exceeding specified size (bytes) |

Useful for preventing OOM attacks from untrusted peers. The default remains
unlimited for compatibility with existing large-message deployments, so
applications that accept traffic from outside their trust boundary should set a
positive limit explicitly.

Set this option before `bind` on listening sockets so newly accepted sessions
inherit the limit:

```c
int64_t max_msg_size = 1024 * 1024;  /* 1 MiB */
zlink_set_option(socket, ZLINK_OPT_MAXMSGSIZE,
                 &max_msg_size, sizeof(max_msg_size));
```

---

## 18. IPv6 — IPV6

| | |
|---|---|
| **What it does** | Enables IPv6 on the socket (dual-stack with IPv4) |
| **Applied at** | `asio_tcp_connecter`, `asio_tcp_listener` — address resolution and socket creation |
| **Default** | `0` (IPv4 only, inherited from context) |

Setting to `1` creates a dual-stack socket with `IPV6_V6ONLY=0`.

---

## 19. Multicast — MULTICAST_HOPS / MULTICAST_MAXTPDU

| Option | What it does | Default |
|--------|-------------|---------|
| `MULTICAST_HOPS` | Multicast packet TTL | `1` |
| `MULTICAST_MAXTPDU` | Maximum transport data unit size (bytes) | `1500` |

Only applies to PGM transport. PGM is currently disabled.

---

## 20. Invert Subscription Matching — INVERT_MATCHING

| | |
|---|---|
| **What it does** | Inverts the subscription filter matching result |
| **Applied at** | `xsub.cpp` — `matching ^ options.invert_matching` |
| **Default** | `0` (normal matching) |

When set to `1`, messages for non-subscribed topics are delivered, and subscribed topics are filtered out.

---

## 21. Network Interface Binding — BINDTODEVICE

| | |
|---|---|
| **What it does** | Binds the socket to a specific network interface (or VRF) |
| **Applied at** | `tcp.cpp` — `setsockopt(SO_BINDTODEVICE)` |
| **Default** | Empty string (no binding) |

Only works on Linux systems with `SO_BINDTODEVICE` support. Used to restrict traffic to a specific NIC on multi-homed servers.

---

## 22. Handshake Timeout — HANDSHAKE_IVL

| | |
|---|---|
| **What it does** | Sets the maximum time for ZMP protocol handshake |
| **Applied at** | ZMP engine — timer set at handshake start |
| **Default** | `30000` ms (30 seconds) |
| **0** | Handshake timeout disabled |

If the handshake is not completed within this time, the connection is closed.

---

## 23. ZMP Metadata — ZMP_METADATA

| | |
|---|---|
| **What it does** | Attaches ZMP protocol metadata properties to outgoing connections |
| **Applied at** | `asio_zmp_engine` — metadata included in READY frame |
| **Default** | `0` (disabled) |
| **Type** | binary |

---

## Per-Socket-Type Default Overrides

Some socket types override common defaults at creation time:

| Socket Type | Overridden Option | Value | Reason |
|-------------|-------------------|-------|--------|
| `SUB` / `XSUB` | `LINGER` | `0` | Subscription sockets have nothing to drain on close |
| `ROUTER` | `ROUTER_MANDATORY` | `1` | Surface failures to unconnected peers instead of silently dropping |
| `PUB` / `XPUB` | `PUB_NODROP` | `1` | Surface `BACKPRESSURED` on HWM instead of silently dropping |
| `STREAM` | `BACKLOG` | `65536` | Accommodate many external clients |

> **Defaults and observable behavior:**
>
> - `ROUTER_MANDATORY` defaults to `1`. An unset ROUTER returns
>   `ZLINK_SUBMIT_NOT_CONNECTED` for sends to unconnected peers instead
>   of silently dropping. Writable / `ZLINK_POLLOUT` observation also
>   surfaces readiness only while a reachable peer exists. Set the
>   option to `0` explicitly if silent-drop is required.
> - `ZLINK_OPT_RID_DUPLICATE_POLICY` defaults to
>   `ZLINK_RID_DUPLICATE_REJECT`. Keep this default to preserve the
>   existing pipe and reject a duplicate identity. Set it explicitly to
>   `ZLINK_RID_DUPLICATE_HANDOVER` when the newer connection should take over.
> - `PUB_NODROP` defaults to `1`. `zlink_publish()` returns
>   `ZLINK_SUBMIT_BACKPRESSURED` on HWM instead of silently dropping.
>   Loss-tolerant workloads that prefer dropping on HWM must set this
>   option to `0` explicitly.
>
> These defaults only govern the **default profile**; the option
> constants and their on/off semantics are unchanged.

## Per-Socket-Type Dedicated Options

Beyond common options, socket-type-specific options use dedicated APIs:

| Socket | API | Representative Options |
|--------|-----|----------------------|
| ROUTER | `zlink_set_router_option()` | `MANDATORY` (default `1`), `PROBE`, `CONNECT_ROUTING_ID`, `REQUEST_TIMEOUT_MS`, `WEIGHT` (default `100`) |
| DEALER | `zlink_set_dealer_option()` | `PROBE`, `REQUEST_TIMEOUT_MS`, `WEIGHT` (default `100`) |
| PUB/XPUB | `zlink_set_pub_option()` | `VERBOSE`, `VERBOSER`, `NODROP` (default `1`), `MANUAL`, `WELCOME_MSG`, `APPROVE_SUBSCRIBE`, `REJECT_SUBSCRIBE` |
| SUB/XSUB | `zlink_set_sub_option()` (`TOPICS_COUNT`); subscription filters via `zlink_set_subscription()` / `zlink_unset_subscription()` | `TOPICS_COUNT` |
| STREAM | `zlink_set_stream_option()` | `NOTIFY` |

---

## Socket Channel Name

Assign a logical channel name to any socket. Frameworks and applications can
use this metadata to keep route-channel configuration explicit without encoding
the channel into transport endpoints.

```c
/* Set the channel name */
zlink_socket_set_channel_name(socket, "price-feed");

/* Read it back */
char buf[256];
size_t len = 0;
zlink_socket_get_channel_name(socket, buf, sizeof(buf), &len);
```

The channel name is metadata only. It does not connect the socket.

---
<!-- zlink-nav:bottom:start -->
[← Thread Safety](11-thread-safety.md)
<!-- zlink-nav:bottom:end -->
