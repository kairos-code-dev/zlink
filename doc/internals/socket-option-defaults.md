[English](socket-option-defaults.md) | [한국어](socket-option-defaults.ko.md)

# Socket Option Defaults (Code Source)

This page summarizes effective socket option defaults from implementation code,
not from examples:

- `core/src/core/options.cpp` (`options_t` constructor)
- `core/src/sockets/socket_base.cpp` (context-inherited defaults)
- socket-specific constructors in `core/src/sockets/*.cpp`

## 1. Common Defaults (All Sockets Unless Overridden)

| Option | Default | Notes |
|---|---:|---|
| `ZLINK_AFFINITY` | `0` | No I/O thread affinity mask |
| `ZLINK_ROUTING_ID` | auto | 16-byte random ID per socket |
| `ZLINK_SNDHWM` | `1000` | Send queue HWM |
| `ZLINK_RCVHWM` | `1000` | Receive queue HWM |
| `ZLINK_RATE` | `100` | Multicast rate (kb/s) |
| `ZLINK_RECOVERY_IVL` | `10000` | Multicast recovery interval (ms) |
| `ZLINK_SNDBUF` | `-1` | Do not force `SO_SNDBUF` |
| `ZLINK_RCVBUF` | `-1` | Do not force `SO_RCVBUF` |
| `ZLINK_TOS` | `0` | No ToS/DSCP override |
| `ZLINK_LINGER` | context-derived | `-1` if `ZLINK_BLOCKY=1` (default), else `0` |
| `ZLINK_CONNECT_TIMEOUT` | `0` | Disabled |
| `ZLINK_TCP_MAXRT` | `0` | Disabled |
| `ZLINK_RECONNECT_IVL` | `100` | Initial reconnect interval (ms) |
| `ZLINK_RECONNECT_IVL_MAX` | `0` | Max interval disabled |
| `ZLINK_BACKLOG` | `100` | Listener backlog |
| `ZLINK_MAXMSGSIZE` | `-1` | Unlimited |
| `ZLINK_MULTICAST_HOPS` | `1` | Multicast TTL |
| `ZLINK_MULTICAST_MAXTPDU` | `1500` | Multicast max TPDU |
| `ZLINK_RCVTIMEO` | `-1` | Infinite receive timeout |
| `ZLINK_SNDTIMEO` | `-1` | Infinite send timeout |
| `ZLINK_IPV6` | context-derived | Inherits `zlink_ctx_get(ctx, ZLINK_IPV6)` (default `0`) |
| `ZLINK_IMMEDIATE` | `0` | Attach connecting pipes immediately |
| `ZLINK_CONFLATE` | `0` | Disabled |
| `ZLINK_INVERT_MATCHING` | `0` | Disabled |
| `ZLINK_STREAM_NOTIFY` | `0` | Disabled |
| `ZLINK_HEARTBEAT_IVL` | `0` | Disabled |
| `ZLINK_HEARTBEAT_TTL` | `0` | Disabled |
| `ZLINK_HEARTBEAT_TIMEOUT` | `-1` | Engine treats `-1` as interval-based fallback when heartbeat is enabled |
| `ZLINK_TCP_KEEPALIVE` | `-1` | OS default |
| `ZLINK_TCP_KEEPALIVE_CNT` | `-1` | OS default |
| `ZLINK_TCP_KEEPALIVE_IDLE` | `-1` | OS default |
| `ZLINK_TCP_KEEPALIVE_INTVL` | `-1` | OS default |
| `ZLINK_TCP_NODELAY` | `1` | Enabled by default |
| `ZLINK_BINDTODEVICE` | empty string | No device binding |
| `ZLINK_ZMP_METADATA` | `0` | Disabled |

## 2. Socket-Type Specific Defaults and Overrides

| Socket Type | Option/Behavior | Default |
|---|---|---|
| `ZLINK_DEALER` | `ZLINK_PROBE_ROUTER` | `0` |
| `ZLINK_ROUTER` | `ZLINK_ROUTER_MANDATORY` | `0` |
| `ZLINK_ROUTER` | `ZLINK_PROBE_ROUTER` | `0` |
| `ZLINK_ROUTER` | `ZLINK_ROUTER_HANDOVER` | `0` |
| `ZLINK_XPUB` | `ZLINK_XPUB_VERBOSE` | `0` |
| `ZLINK_XPUB` | `ZLINK_XPUB_VERBOSER` | `0` |
| `ZLINK_XPUB` | `ZLINK_XPUB_NODROP` | `0` (`_lossy=true`) |
| `ZLINK_XPUB` | `ZLINK_XPUB_MANUAL` | `0` |
| `ZLINK_XPUB` | `ZLINK_XPUB_MANUAL_LAST_VALUE` | `0` |
| `ZLINK_XPUB` | `ZLINK_ONLY_FIRST_SUBSCRIBE` | `0` |
| `ZLINK_XPUB` | `ZLINK_XPUB_WELCOME_MSG` | empty |
| `ZLINK_XSUB` | `ZLINK_ONLY_FIRST_SUBSCRIBE` | `0` |
| `ZLINK_XSUB` | `ZLINK_LINGER` override | forced to `0` |
| `ZLINK_SUB` | `ZLINK_LINGER` override | forced to `0` (inherits XSUB constructor) |
| `ZLINK_SUB` | Subscription set | empty at creation |
| `ZLINK_STREAM` | `ZLINK_BACKLOG` override | `65536` |
| `ZLINK_STREAM` | `ZLINK_SNDBUF` override | `262144` if unset (`<0`) |
| `ZLINK_STREAM` | `ZLINK_RCVBUF` override | `262144` if unset (`<0`) |
| `ZLINK_STREAM` | `ZLINK_CONNECT_ROUTING_ID` | not supported (`EOPNOTSUPP`) |

## 3. Read-Only Initial State Values

| Option | Initial Value |
|---|---|
| `ZLINK_LAST_ENDPOINT` | empty string |
| `ZLINK_RCVMORE` | `0` |
| `ZLINK_TOPICS_COUNT` (XPUB/XSUB) | `0` |

## 4. TLS Option Defaults (When Built with TLS)

| Option | Default |
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

- `ZLINK_BLOCKY` is a context option (`zlink_ctx_set/get`), not a per-socket
  `zlink_setsockopt` option.
- `ZLINK_CONFLATE` is only effective for `DEALER`, `PUB`, and `SUB`.
- STREAM has additional runtime tuning knobs documented in
  `stream-socket.md`, but those are not generic socket options.
