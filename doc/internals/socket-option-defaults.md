[English](socket-option-defaults.md) | [한국어](socket-option-defaults.ko.md)

# Socket Option Defaults (Code Source)

This document lists the default values for each socket option, broken down by socket type. Some socket types override the global defaults to match their specific semantics.

This page summarizes effective socket option defaults from implementation code,
not from examples:

- `core/src/core/options.cpp` (`options_t` constructor)
- `core/src/core/options_core_socket.cpp` (core socket option dispatch)
- `core/src/core/options_transport_network.cpp` (transport/network option dispatch)
- `core/src/core/options_protocol_metadata.cpp` (protocol/metadata option dispatch)
- `core/src/sockets/socket_base_lifecycle.cpp` (context-inherited defaults)
- socket-specific constructors in `core/src/sockets/*.cpp`

For detailed behavior and scope of each option, see the
[Socket Options Detailed Guide](../guide/12-socket-options.md).

## 1. Common Defaults (All Sockets Unless Overridden)

| Option | Default | Notes |
|---|---:|---|
| `ZLINK_OPT_AFFINITY` | `0` | No I/O thread affinity mask |
| `zlink_get_routing_id()` | auto | 16-byte random ID per socket (set via `zlink_set_routing_id()`) |
| `ZLINK_OPT_SNDHWM` | `1000` | Send queue HWM |
| `ZLINK_OPT_RCVHWM` | `1000` | Receive queue HWM |
| `ZLINK_OPT_RATE` | `100` | Multicast rate (kb/s) |
| `ZLINK_OPT_RECOVERY_IVL` | `10000` | Multicast recovery interval (ms) |
| `ZLINK_OPT_SNDBUF` | `-1` | Do not force `SO_SNDBUF` |
| `ZLINK_OPT_RCVBUF` | `-1` | Do not force `SO_RCVBUF` |
| `ZLINK_OPT_TOS` | `0` | No ToS/DSCP override |
| `ZLINK_OPT_LINGER` | context-derived | `-1` if `ZLINK_OPT_BLOCKY=1` (default), else `0` |
| `ZLINK_OPT_CONNECT_TIMEOUT` | `0` | Disabled |
| `ZLINK_OPT_TCP_MAXRT` | `0` | Disabled |
| `ZLINK_OPT_RECONNECT_IVL` | `100` | Initial reconnect interval (ms) |
| `ZLINK_OPT_RECONNECT_IVL_MAX` | `0` | Max interval disabled |
| `ZLINK_OPT_BACKLOG` | `100` | Listener backlog |
| `ZLINK_OPT_MAXMSGSIZE` | `-1` | Unlimited |
| `ZLINK_OPT_MULTICAST_HOPS` | `1` | Multicast TTL |
| `ZLINK_OPT_MULTICAST_MAXTPDU` | `1500` | Multicast max TPDU |
| `ZLINK_OPT_RCVTIMEO` | `-1` | Infinite receive timeout |
| `ZLINK_OPT_SNDTIMEO` | `-1` | Infinite send timeout |
| `ZLINK_OPT_IPV6` | context-derived | Inherits `zlink_ctx_get(ctx, ZLINK_OPT_IPV6)` (default `0`) |
| `ZLINK_OPT_IMMEDIATE` | `0` | Attach connecting pipes immediately |
| `ZLINK_OPT_CONFLATE` | `0` | Disabled |
| `ZLINK_OPT_INVERT_MATCHING` | `0` | Disabled |
| `ZLINK_STREAM_OPT_NOTIFY` | `0` | Disabled |
| `ZLINK_OPT_HEARTBEAT_IVL` | `0` | Disabled |
| `ZLINK_OPT_HEARTBEAT_TTL` | `0` | Disabled |
| `ZLINK_OPT_HEARTBEAT_TIMEOUT` | `-1` | Engine treats `-1` as interval-based fallback when heartbeat is enabled |
| `ZLINK_OPT_TCP_KEEPALIVE` | `-1` | OS default |
| `ZLINK_OPT_TCP_KEEPALIVE_CNT` | `-1` | OS default |
| `ZLINK_OPT_TCP_KEEPALIVE_IDLE` | `-1` | OS default |
| `ZLINK_OPT_TCP_KEEPALIVE_INTVL` | `-1` | OS default |
| `ZLINK_OPT_TCP_NODELAY` | `1` | Enabled by default |
| `ZLINK_OPT_BINDTODEVICE` | empty string | No device binding |
| `ZLINK_OPT_HANDSHAKE_IVL` | `30000` | ZMP handshake timeout (ms, 0 = disabled) |
| `ZLINK_OPT_PRIORITY` | `0` | Socket priority |
| `ZLINK_OPT_BUSY_POLL` | `0` | Busy-poll disabled |
| `ZLINK_OPT_MONITOR_EVENT_VERSION` | `1` | Monitor event version |
| `ZLINK_OPT_IN_BATCH_SIZE` | `8192` | Inbound batch size (bytes) |
| `ZLINK_OPT_OUT_BATCH_SIZE` | `8192` | Outbound batch size (bytes) |
| `ZLINK_OPT_ZERO_COPY` | `1` | Zero-copy enabled |
| `ZLINK_OPT_ZMP_METADATA` | `0` | Disabled |

## 2. Socket-Type Specific Defaults and Overrides

| Socket Type | Option/Behavior | Default |
|---|---|---|
| `ZLINK_DEALER` | `ZLINK_DEALER_OPT_PROBE` | `0` |
| `ZLINK_ROUTER` | `ZLINK_ROUTER_OPT_MANDATORY` | `1` (set via `zlink_set_router_option()`) |
| `ZLINK_ROUTER` | `ZLINK_ROUTER_OPT_PROBE` | `0` |
| `ZLINK_ROUTER` | `ZLINK_ROUTER_OPT_HANDOVER` | `1` |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_VERBOSE` | `0` (set via `zlink_set_pub_option()`) |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_VERBOSER` | `0` |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_NODROP` | `1` (`_lossy=false`) |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_MANUAL` | `0` |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_MANUAL_LAST_VALUE` | `0` |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_WELCOME_MSG` | empty |
| `ZLINK_XSUB` | `ZLINK_OPT_LINGER` override | forced to `0` |
| `ZLINK_SUB` | `ZLINK_OPT_LINGER` override | forced to `0` (inherits XSUB constructor) |
| `ZLINK_SUB` | Subscription set | empty at creation |
| `ZLINK_STREAM` | `ZLINK_OPT_BACKLOG` override | `65536` |
| `ZLINK_STREAM` | `ZLINK_OPT_SNDBUF` override | `262144` if unset (`<0`) |
| `ZLINK_STREAM` | `ZLINK_OPT_RCVBUF` override | `262144` if unset (`<0`) |
| `ZLINK_STREAM` | `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` | not supported (`EOPNOTSUPP`) |

## 3. Read-Only Initial State Values

| Option | Initial Value |
|---|---|
| `ZLINK_OPT_LAST_ENDPOINT` | empty string |
| `ZLINK_PUB_OPT_TOPICS_COUNT` (XPUB) | `0` |
| `ZLINK_SUB_OPT_TOPICS_COUNT` (XSUB/SUB) | `0` |

## 4. TLS Option Defaults (When Built with TLS)

> **Note:** TLS is configured via `zlink_set_tls_server()` / `zlink_set_tls_client()`
> in the public API. The constants below are the underlying internal default values.

| Option (internal) | Default |
|---|---:|
| `ZLINK_TLS_VERIFY` | `1` |
| `ZLINK_TLS_REQUIRE_CLIENT_CERT` | `0` |
| `ZLINK_TLS_TRUST_SYSTEM` | `1` |
| `ZLINK_TLS_CERT` | empty string |
| `ZLINK_TLS_KEY` | empty string |
| `ZLINK_TLS_CA` | empty string |
| `ZLINK_TLS_HOSTNAME` | empty string |
| `ZLINK_TLS_PASSWORD` | empty string |

## 5. Important Notes

- `ZLINK_OPT_BLOCKY` is a context option (`zlink_ctx_set/get`), not a per-socket
  `zlink_set_option` option.
- `ZLINK_OPT_CONFLATE` is only effective for `DEALER`, `PUB`, and `SUB`.
- STREAM has additional runtime tuning knobs documented in
  `stream-socket.md`, but those are not generic socket options.
