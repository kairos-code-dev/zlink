[English](socket-option-defaults.md) | [한국어](socket-option-defaults.ko.md)

# Socket Option Defaults (Code Source)

This document lists the default values for each socket option, broken down by socket type. Some socket types override the global defaults to match their specific semantics.

This page summarizes effective socket option defaults from implementation code,
not from examples:

- `core/src/runtime/core/options.cpp` (`options_t` constructor)
- `core/src/runtime/core/options_core_socket.cpp` (core socket option dispatch)
- `core/src/runtime/core/options_transport_network.cpp` (transport/network option dispatch)
- `core/src/runtime/core/options_protocol_metadata.cpp` (protocol/metadata option dispatch)
- `core/src/runtime/sockets/common/socket_base_lifecycle.cpp` (context-inherited defaults)
- socket-specific constructors in `core/src/runtime/sockets/*/*.cpp`

For detailed behavior and scope of each option, see the
[Socket Options Detailed Guide](../guide/12-socket-options.md).

## 1. Common Defaults (All Sockets Unless Overridden)

| Option | Default | Notes |
|---|---:|---|
| `ZLINK_OPT_AFFINITY` | `0` | No I/O thread affinity mask |
| `zlink_get_routing_id()` | auto | 16-byte random ID per socket (set via `zlink_set_routing_id()`) |
| `ZLINK_OPT_SNDHWM` | auto-HWM balanced value | Calculated from profile, policy class, and message unit. If context auto-HWM is disabled, the legacy default is `1000`. |
| `ZLINK_OPT_RCVHWM` | auto-HWM balanced value | Calculated from profile, policy class, and message unit. If context auto-HWM is disabled, the legacy default is `1000`. |
| `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` | `0` | Raw `0` means socket-type default: `1024` for STREAM, `4096` for other sockets |
| `ZLINK_OPT_RATE` | `100` | Multicast rate (kb/s) |
| `ZLINK_OPT_RECOVERY_IVL` | `10000` | Multicast recovery interval (ms) |
| `ZLINK_OPT_SNDBUF` | `-1` | Do not force `SO_SNDBUF` |
| `ZLINK_OPT_RCVBUF` | `-1` | Do not force `SO_RCVBUF` |
| `ZLINK_OPT_TOS` | `0` | No ToS/DSCP override |
| `ZLINK_OPT_LINGER` | context-derived | `-1` if the context blocky mode is enabled by default, else `0` |
| `ZLINK_OPT_CONNECT_TIMEOUT` | `0` | Disabled |
| `ZLINK_OPT_TCP_MAXRT` | `0` | Disabled |
| `ZLINK_OPT_RECONNECT_IVL` | `100` | Initial reconnect interval (ms) |
| `ZLINK_OPT_RECONNECT_IVL_MAX` | `0` | Max interval disabled |
| `ZLINK_OPT_BACKLOG` | `100` | Listener backlog |
| `ZLINK_OPT_MAXMSGSIZE` | `-1` | Unlimited |
| `ZLINK_OPT_MULTICAST_HOPS` | `1` | Multicast TTL |
| `ZLINK_OPT_MULTICAST_MAXTPDU` | `1500` | Multicast max TPDU |
| `ZLINK_OPT_RCVTIMEO` | `1000` | Default receive timeout in ms |
| `ZLINK_OPT_SNDTIMEO` | `1000` | Default send timeout in ms |
| `ZLINK_OPT_IPV6` | context-derived | Inherits the context IPv6 default (default `0`) |
| `ZLINK_OPT_IMMEDIATE` | `0` | Attach connecting pipes immediately |
| `ZLINK_OPT_CONFLATE` | `0` | Disabled |
| `ZLINK_OPT_INVERT_MATCHING` | `0` | Disabled |
| `ZLINK_STREAM_OPT_NOTIFY` | `0` | Disabled |
| `ZLINK_OPT_TCP_KEEPALIVE` | `-1` | OS default |
| `ZLINK_OPT_TCP_KEEPALIVE_CNT` | `-1` | OS default |
| `ZLINK_OPT_TCP_KEEPALIVE_IDLE` | `-1` | OS default |
| `ZLINK_OPT_TCP_KEEPALIVE_INTVL` | `-1` | OS default |
| `ZLINK_OPT_TCP_NODELAY` | `1` | Enabled by default |
| `ZLINK_OPT_BINDTODEVICE` | empty string | No device binding |
| `ZLINK_OPT_HANDSHAKE_IVL` | `30000` | ZMP handshake timeout (ms, 0 = disabled) |
| `ZLINK_OPT_ZMP_METADATA` | `0` | Disabled |

## 2. Socket-Type Specific Defaults and Overrides

| Socket Type | Option/Behavior | Default |
|---|---|---|
| `ZLINK_SOCKET_DEALER` | `ZLINK_DEALER_OPT_PROBE` | `0` |
| `ZLINK_SOCKET_ROUTER` | `ZLINK_ROUTER_OPT_MANDATORY` | `1` |
| `ZLINK_SOCKET_ROUTER` | `ZLINK_ROUTER_OPT_PROBE` | `0` |
| Common socket | `ZLINK_OPT_RID_DUPLICATE_POLICY` | `ZLINK_RID_DUPLICATE_REJECT` |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_VERBOSE` | `0` |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_VERBOSER` | `0` |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_NODROP` | `1` (`_lossy=false`) |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_MANUAL` | `0` |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_MANUAL_LAST_VALUE` | `0` |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_WELCOME_MSG` | empty |
| `ZLINK_SOCKET_XSUB` | `ZLINK_OPT_LINGER` override | forced to `0` |
| `ZLINK_SOCKET_SUB` | `ZLINK_OPT_LINGER` override | forced to `0` (inherits the XSUB constructor path) |
| `ZLINK_SOCKET_SUB` | Subscription set | empty at creation |
| `ZLINK_SOCKET_STREAM` | `ZLINK_OPT_BACKLOG` override | `65536` |
| `ZLINK_SOCKET_STREAM` | `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` | not supported (`EOPNOTSUPP`) |

## 3. Read-Only Initial State Values

| Option | Initial Value |
|---|---|
| `ZLINK_OPT_LAST_ENDPOINT` | empty string |
| `ZLINK_PUB_OPT_TOPICS_COUNT` (XPUB) | `0` |
| `ZLINK_SUB_OPT_TOPICS_COUNT` (XSUB/SUB) | `0` |

## 4. TLS Helper Notes

TLS is configured through the public helper APIs
`zlink_set_tls_server()` and `zlink_set_tls_client()`.
This document does not list a separate TLS constant table because the
public headers do not expose `ZLINK_TLS_*` option names.

Until those helper APIs are called, certificate and CA paths remain unset.
The helper call itself supplies policy values such as
`require_client_cert` and `trust_system`.

## 5. Important Notes

- The default `ZLINK_OPT_LINGER` value comes from the context's blocky mode.
- The default `ZLINK_OPT_SNDHWM` / `ZLINK_OPT_RCVHWM` values come from
  context auto-HWM with the balanced profile. Disabling context auto-HWM uses
  the legacy fixed value `1000`. Manual settings override the automatic policy.
- `ZLINK_CTX_OPT_AUTO_HWM_PROFILE` selects the planner's profile. The public
  values are `COMPACT`, `LOW_LATENCY`, `BALANCED`, and `THROUGHPUT`; the
  default is `BALANCED`.
- The deprecated context memory-budget and bootstrap context options are
  no-op compatibility fields. They do not influence socket defaults or HWM.
- `auto_hwm_effective_message_bytes` is socket-specific. It uses a positive
  raw socket `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` override first, then a positive
  context `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES`, otherwise the STREAM default
  `1024` or non-STREAM default `4096`.
- The planner chooses a policy class (`fanout`, `spot_data`, `routed`,
  `peer_queue`, `stream`, `recv_ingress`, or `control`), a profile-specific
  per-connection unit budget, and a message-size cap. The final HWM is clamped
  to at least `1` and at most that size cap.
- SPOT publish planning keeps per-connection HWM independent of total spot
  count and connection count.
- `ZLINK_OPT_CONFLATE` is only effective for `ZLINK_SOCKET_DEALER`,
  `ZLINK_SOCKET_PUB`, and `ZLINK_SOCKET_SUB`.
- STREAM has additional runtime tuning knobs documented in
  `stream-socket.md`, but those are not generic socket options.
