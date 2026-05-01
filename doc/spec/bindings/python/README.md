[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Python Binding Specification

This document defines the complete public API surface of the zlink Python binding.
Every class, method, and type listed here is part of the contract that the binding
must expose. Internal/private methods are omitted.

Only names re-exported from the public `zlink` package are part of the
contract. Modules such as `_core`, `_ffi`, `_native`, and other underscore
prefixed helpers are internal implementation details. Perf, samples, and tests
must import from `zlink` only and must not rely on private underscore modules.

---

## Current Core Alignment Overrides

The sections below still contain some older method lists. When they conflict
with the rules here, this section wins.

- `PairSocket`, `DealerSocket`, and `RouterSocket` are recv-only on the data
  plane. Remove `on_receive(...)` from their public contract.
- `SubSocket` and `XSubSocket` are recv-only. Remove `on_subscribe(...)` from
  their public contract.
- `StreamSocket` keeps `recv(...)` and exposes a packet callback surface
  mapped to `zlink_stream_packet_handler()`. Recommended canonical name:
  `on_packet(...)`.
- `SpotNode` must expose channel-aware attachment APIs:
  `attach_discovery(...)`,
  `attach_channel_dealer(...)`,
  `attach_channel_dealer_manual(...)`, and
  `attach_pub_ingress(...)`.
- `Spot` must expose channel-aware data-plane methods:
  `send_channel(...)`, `send_to_spot(...)`, `request_channel(...)`, and
  `publish(service_name, topic, ...)`.
- `Spot.subscribe(...)` returns a service-aware `TopicMessage`. `TopicMessage`
  therefore needs `service_name: str | None`, populated for SPOT subscribe
  results and `None` for raw `SUB` / `XSUB`.
- `Spot` must not expose `on_subscribe(...)`. Use
  `on_dispatch_event(...)` plus `subscribe(...)` / `recv_routed(...)` /
  timer recv.
- `SpotDispatchEvent.SUBSCRIBE_READABLE` and `.ROUTED_READABLE` are readiness
  notifications, not one-event-per-message delivery counters. Binding docs and
  samples must drain until the recv path reports `EAGAIN`.
- `Spot.on_routed_receive(...)` and `on_dispatch_event(...)` are mutually
  exclusive on the routed axis.
- Peer weight is exposed only on `RouterSocket` and `DealerSocket` through typed option/property surfaces. The value range is `0..100`, default `100`; `0` drains new outbound selection. Submit attempts to a weight-`0` peer raise `SubmitError` whose `code` is
  `SubmitResult.NOT_ADMITTED`.
- `POLLOUT` is a send-recovery readiness signal, shared with
  `on_send_ready(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header: `mandatory =
  True`, `handover = False`, `nodrop = True`.
- SPOT queue defaults follow the core header: local subscribe delivery target
  hard limit `100`, routed delivery target hard limit `500`. Binding packaged
  linux-x86_64 runtime libraries must be synchronized from `core/build` before
  validation.
- Internal pairing rule: when auto-connect pairs two same-service ROUTERs
  via Discovery, the library picks one initiator per pair by a total order
  on `(routing_id, advertise_endpoint)`. Users do not configure this.

## Core

### Context

```python
class Context:
    def __init__(self) -> None: ...
    @property
    def options(self) -> ContextOptions: ...  # Raises: ConfigError
    def shutdown(self) -> None: ...           # Raises: CloseError
    def close(self) -> None: ...              # Raises: CloseError
    # supports `with` and `async with` — __exit__ raises CloseError
```

### ContextOptions

All `ContextOptions` getters, setters, and mutator methods raise
`ConfigError` on failure.

```python
class ContextOptions:
    @property
    def io_threads(self) -> int: ...
    @io_threads.setter
    def io_threads(self, value: int) -> None: ...
    @property
    def max_sockets(self) -> int: ...
    @max_sockets.setter
    def max_sockets(self, value: int) -> None: ...
    @property
    def max_msg_size(self) -> int: ...
    @max_msg_size.setter
    def max_msg_size(self, value: int) -> None: ...
    @property
    def thread_priority(self) -> int: ...
    @thread_priority.setter
    def thread_priority(self, value: int) -> None: ...
    @property
    def thread_scheduling_policy(self) -> int: ...
    @thread_scheduling_policy.setter
    def thread_scheduling_policy(self, value: int) -> None: ...
    @property
    def blocky(self) -> bool: ...
    @blocky.setter
    def blocky(self, value: bool) -> None: ...
    def auto_hwm_profile(self) -> AutoHwmProfile: ...
    @auto_hwm_profile.setter
    def auto_hwm_profile(self, value: AutoHwmProfile) -> None: ...
    @property
    def socket_limit(self) -> int: ...       # read-only
    @property
    def msg_t_size(self) -> int: ...          # read-only
    def add_thread_affinity(self, cpu: int) -> None: ...
    def remove_thread_affinity(self, cpu: int) -> None: ...
```

The native context memory-budget and bootstrap auto-HWM options are
deprecated no-op compatibility options. The Python binding does not expose
typed properties for them.

```python
class AutoHwmProfile(IntEnum):
    LOW_LATENCY = 1
    BALANCED = 2
    THROUGHPUT = 3
```

---

## Socket Types

All sockets support `with` / `async with` context managers.

Python nonblocking data-plane helpers follow this rule:

- `send(...)` and `publish(...)` return `False` only for temporary
  backpressure when `flags` includes `DONTWAIT`.
- Blocking submit returns `True` on success. Route-not-ready and other submit
  failures still raise `SubmitError`.
- `recv(...)` and `subscribe(...)` return `None` when `flags` includes
  `DONTWAIT` and no message is currently available, and still raise
  `RecvError` for real recv failures.

Peer weight is not a common-socket accessor. Bindings expose it only on the
implemented weight-bearing handles (`RouterSocket` and `DealerSocket`) through their typed option/property surfaces.

### PairSocket

```python
class PairSocket:
    def __init__(self, context: Context) -> None: ...
    @property
    def options(self) -> CommonSocketOptions: ...                                # Raises: ConfigError
    def bind(self, endpoint: str) -> None: ...                                   # Raises: BindError
    def unbind(self, endpoint: str) -> None: ...                                 # Raises: ConnectError
    def connect(self, endpoint: str) -> None: ...                                # Raises: ConnectError
    def disconnect(self, endpoint: str) -> None: ...                             # Raises: ConnectError
    def send(self, payload: Message | bytes | list, *, flags: int = 0) -> bool: ...  # Raises: SubmitError
    def recv(self, *, flags: int = 0) -> Received | None: ...                    # Raises: RecvError
    def on_send_ready(self, handler: Callable[[PairSocket], None]) -> None: ...  # Raises: HandlerError
    def monitor_open(self, events: MonitorEventMask = MonitorEventMask.ALL) -> MonitorSocket: ...  # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### PubSocket

```python
class PubSocket:
    def __init__(self, context: Context) -> None: ...
    @property
    def options(self) -> CommonSocketOptions: ...                                # Raises: ConfigError
    @property
    def publisher_options(self) -> PubSocketOptions: ...                         # Raises: ConfigError
    def bind(self, endpoint: str) -> None: ...                                   # Raises: BindError
    def unbind(self, endpoint: str) -> None: ...                                 # Raises: ConnectError
    def connect(self, endpoint: str) -> None: ...                                # Raises: ConnectError
    def disconnect(self, endpoint: str) -> None: ...                             # Raises: ConnectError
    def publish(self, topic: bytes | str, payload: Message | bytes | list, *, flags: int = 0) -> bool: ...  # Raises: SubmitError
    def on_send_ready(self, handler: Callable[[PubSocket], None]) -> None: ...   # Raises: HandlerError
    def monitor_open(self, events: MonitorEventMask = MonitorEventMask.ALL) -> MonitorSocket: ...  # Raises: ConfigError
    def attach_discovery(self, discovery: Discovery) -> None: ...                # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### SubSocket

```python
class SubSocket:
    def __init__(self, context: Context) -> None: ...
    @property
    def options(self) -> CommonSocketOptions: ...                                # Raises: ConfigError
    @property
    def subscriber_options(self) -> SubSocketOptions: ...                        # Raises: ConfigError
    def bind(self, endpoint: str) -> None: ...                                   # Raises: BindError
    def unbind(self, endpoint: str) -> None: ...                                 # Raises: ConnectError
    def connect(self, endpoint: str) -> None: ...                                # Raises: ConnectError
    def disconnect(self, endpoint: str) -> None: ...                             # Raises: ConnectError
    def set_subscription(self, topic: bytes | str) -> None: ...                  # Raises: ConfigError
    def unset_subscription(self, topic: bytes | str) -> None: ...                # Raises: ConfigError
    def subscribe(self, *, flags: int = 0) -> TopicMessage | None: ...           # Raises: RecvError
    def monitor_open(self, events: MonitorEventMask = MonitorEventMask.ALL) -> MonitorSocket: ...  # Raises: ConfigError
    def attach_discovery(self, discovery: Discovery) -> None: ...                # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### DealerSocket

```python
class DealerSocket:
    def __init__(self, context: Context) -> None: ...
    @property
    def options(self) -> CommonSocketOptions: ...                                  # Raises: ConfigError
    @property
    def dealer_options(self) -> DealerSocketOptions: ...                           # Raises: ConfigError
    def bind(self, endpoint: str) -> None: ...                                     # Raises: BindError
    def unbind(self, endpoint: str) -> None: ...                                   # Raises: ConnectError
    def connect(self, endpoint: str) -> None: ...                                  # Raises: ConnectError
    def disconnect(self, endpoint: str) -> None: ...                               # Raises: ConnectError
    def set_routing_id(self, routing_id: RoutingId | bytes) -> None: ...           # Raises: ConfigError
    def get_routing_id(self) -> RoutingId | None: ...                              # Raises: ConfigError
    def set_channel_name(self, channel_name: str) -> None: ...                     # Raises: ConfigError
    def get_channel_name(self) -> str: ...                                         # Raises: ConfigError
    def send(self, payload: Message | bytes | list, *, flags: int = 0) -> bool: ...  # Raises: SubmitError
    def recv(self, *, flags: int = 0) -> Received | None: ...                    # Raises: RecvError
    def on_send_ready(self, handler: Callable[[DealerSocket], None]) -> None: ...  # Raises: HandlerError
    def monitor_open(self, events: MonitorEventMask = MonitorEventMask.ALL) -> MonitorSocket: ...  # Raises: ConfigError
    def attach_discovery(self, discovery: Discovery) -> None: ...                  # Raises: ConfigError

    # --- request (async) — no flags ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure; RequestError on request completion failure.
    async def request(self, payload: Message | bytes | list,
                      *, timeout: int = 0) -> list[Message]: ...

    # --- request (callback submit) ---
    # timeout = 0 uses the socket default timeout.
    # Returns False only for temporary backpressure when flags includes DONTWAIT.
    # Raises: SubmitError on submit failure other than temporary backpressure.
    # Callback receives RequestResult;
    #   non-OK indicates request-completion failure (RequestError semantics).
    # Callback receives an empty list on failure.
    def request(self, payload: Message | bytes | list,
                callback: Callable[[RequestResult, list[Message]], None],
                *, flags: int = 0, timeout: int = 0) -> bool: ...

    def close(self) -> None: ...                                                   # Raises: CloseError
```

### RouterSocket

```python
class RouterSocket:
    def __init__(self, context: Context) -> None: ...
    @property
    def options(self) -> CommonSocketOptions: ...                                # Raises: ConfigError
    @property
    def router_options(self) -> RouterSocketOptions: ...                         # Raises: ConfigError
    def bind(self, endpoint: str) -> None: ...                                   # Raises: BindError
    def unbind(self, endpoint: str) -> None: ...                                 # Raises: ConnectError
    def connect(self, endpoint: str) -> None: ...                                # Raises: ConnectError
    def disconnect(self, endpoint: str) -> None: ...                             # Raises: ConnectError
    def set_routing_id(self, routing_id: RoutingId | bytes) -> None: ...         # Raises: ConfigError
    def get_routing_id(self) -> RoutingId | None: ...                            # Raises: ConfigError
    def send(self, routing_id: RoutingId, payload: Message | bytes | list[Message], *, flags: int = 0) -> bool: ...  # Raises: SubmitError
    def recv(self, *, flags: int = 0) -> Received | None: ...                    # Raises: RecvError
    def on_send_ready(self, handler: Callable) -> None: ...                      # Raises: HandlerError
    def monitor_open(self, events: MonitorEventMask = MonitorEventMask.ALL) -> MonitorSocket: ...  # Raises: ConfigError
    def attach_discovery(self, discovery: Discovery) -> None: ...                # Raises: ConfigError

    # --- request (async) — no flags ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure; RequestError on request completion failure.
    async def request(self, peer_rid: RoutingId,
                      payload: Message | bytes | list,
                      *, timeout: int = 0) -> list[Message]: ...

    # --- request (callback submit) ---
    # timeout = 0 uses the socket default timeout.
    # Returns False only for temporary backpressure when flags includes DONTWAIT.
    # Raises: SubmitError on submit failure other than temporary backpressure.
    # Callback receives RequestResult;
    #   non-OK indicates request-completion failure (RequestError semantics).
    # Callback receives an empty list on failure.
    def request(self, peer_rid: RoutingId,
                payload: Message | bytes | list,
                callback: Callable[[RequestResult, list[Message]], None],
                *, flags: int = 0, timeout: int = 0) -> bool: ...

    # --- reply ---
    def reply(self, routing_id: RoutingId, request_seq: int,
              payload: Message | bytes | list, *, flags: int = 0) -> None: ...   # Raises: SubmitError

    # --- router → spot routed send ---
    def send_to_spot(self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
                     payload: Message | bytes | list, *, flags: int = 0) -> bool: ...  # Raises: SubmitError

    # --- router → spot routed request (async) — no flags ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure; RequestError on request completion failure.
    async def request_to_spot(self, dest_node_rid: RoutingId,
                              dest_spot_rid: RoutingId,
                              payload: Message | bytes | list,
                              *, timeout: int = 0) -> list[Message]: ...

    # --- router → spot routed request (callback submit) ---
    # timeout = 0 uses the socket default timeout.
    # Returns False only for temporary backpressure when flags includes DONTWAIT.
    # Raises: SubmitError on submit failure other than temporary backpressure.
    # Callback receives RequestResult;
    #   non-OK indicates request-completion failure (RequestError semantics).
    # Callback receives an empty list on failure.
    def request_to_spot(self, dest_node_rid: RoutingId,
                        dest_spot_rid: RoutingId,
                        payload: Message | bytes | list,
                        callback: Callable[[RequestResult, list[Message]], None],
                        *, flags: int = 0, timeout: int = 0) -> bool: ...

    # --- router → spot routed reply ---
    def reply_to_spot(self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
                      request_seq: int,
                      payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError

    # NOTE: RouterSocket 의 routed 수신 plane 은 단일 recv 표면이다. 일반
    # ROUTER 트래픽과 spot-origin routed 트래픽을 모두 recv 로 받는다.
    # `Received.routing_id` 는 source_node_rid, `Received.spot_rid` 는
    # spot-origin 트래픽에서만 값이 있다. data-plane callback install
    # surface (예: on_receive) 는 ROUTER 에 제공하지 않는다. request
    # completion 은 request() 경로에서만 유지된다.

    def close(self) -> None: ...                                                 # Raises: CloseError
```

### XPubSocket

```python
class XPubSocket:
    def __init__(self, context: Context) -> None: ...
    @property
    def options(self) -> CommonSocketOptions: ...                                # Raises: ConfigError
    @property
    def publisher_options(self) -> PubSocketOptions: ...                         # Raises: ConfigError
    def bind(self, endpoint: str) -> None: ...                                   # Raises: BindError
    def unbind(self, endpoint: str) -> None: ...                                 # Raises: ConnectError
    def connect(self, endpoint: str) -> None: ...                                # Raises: ConnectError
    def disconnect(self, endpoint: str) -> None: ...                             # Raises: ConnectError
    def publish(self, topic: bytes | str, payload: Message | bytes | list, *, flags: int = 0) -> bool: ...  # Raises: SubmitError
    def receive_subscription_event(self, *, flags: int = 0) -> SubscriptionEvent: ...  # Raises: RecvError
    def on_send_ready(self, handler: Callable) -> None: ...                      # Raises: HandlerError
    def monitor_open(self, events: MonitorEventMask = MonitorEventMask.ALL) -> MonitorSocket: ...  # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### XSubSocket

```python
class XSubSocket:
    def __init__(self, context: Context) -> None: ...
    @property
    def options(self) -> CommonSocketOptions: ...                                # Raises: ConfigError
    @property
    def subscriber_options(self) -> SubSocketOptions: ...                        # Raises: ConfigError
    def bind(self, endpoint: str) -> None: ...                                   # Raises: BindError
    def unbind(self, endpoint: str) -> None: ...                                 # Raises: ConnectError
    def connect(self, endpoint: str) -> None: ...                                # Raises: ConnectError
    def disconnect(self, endpoint: str) -> None: ...                             # Raises: ConnectError
    def set_subscription(self, topic: bytes | str) -> None: ...                  # Raises: ConfigError
    def unset_subscription(self, topic: bytes | str) -> None: ...                # Raises: ConfigError
    def subscribe(self, *, flags: int = 0) -> TopicMessage | None: ...           # Raises: RecvError
    def monitor_open(self, events: MonitorEventMask = MonitorEventMask.ALL) -> MonitorSocket: ...  # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### StreamSocket

```python
class StreamSocket:
    def __init__(self, context: Context) -> None: ...
    @property
    def options(self) -> CommonSocketOptions: ...                                # Raises: ConfigError
    @property
    def stream_options(self) -> StreamSocketOptions: ...                         # Raises: ConfigError
    def bind(self, endpoint: str) -> None: ...                                   # Raises: BindError
    def unbind(self, endpoint: str) -> None: ...                                 # Raises: ConnectError
    def set_routing_id(self, routing_id: RoutingId | bytes) -> None: ...         # Raises: ConfigError
    def get_routing_id(self) -> RoutingId | None: ...                            # Raises: ConfigError
    def send(self, routing_id: RoutingId, payload: Message | bytes | list[Message], *, flags: int = 0) -> bool: ...  # Raises: SubmitError
    # Two mutually-exclusive receive modes on the same StreamSocket:
    #   (1) recv(), (2) on_packet(handler). Second attach raises
    #   HandlerError(code=HandlerResult.BUSY).
    def recv(self, *, flags: int = 0) -> Received | None: ...                    # Raises: RecvError
    # Mode (3): framed packet callback mapped to
    # zlink_stream_packet_handler(). Wire frame is big-endian u16
    # header_size + u32 body_size + header + body. The handler receives
    # the source routing id, a header Message, and a body Message; both
    # messages transfer ownership to the handler.
    def on_packet(
        self,
        handler: Callable[[RoutingId, "Message", "Message"], None],
    ) -> None: ...                                                               # Raises: HandlerError
    def on_send_ready(self, handler: Callable) -> None: ...                      # Raises: HandlerError
    def monitor_open(self, events: MonitorEventMask = MonitorEventMask.ALL) -> MonitorSocket: ...  # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### Socket Option Classes

All option-class getters and setters raise `ConfigError` on failure.

```python
class CommonSocketOptions:
    @property
    def linger_ms(self) -> int: ...
    @linger_ms.setter
    def linger_ms(self, value: int) -> None: ...
    @property
    def send_high_water_mark(self) -> int: ...
    @send_high_water_mark.setter
    def send_high_water_mark(self, value: int) -> None: ...
    @property
    def receive_high_water_mark(self) -> int: ...
    @receive_high_water_mark.setter
    def receive_high_water_mark(self, value: int) -> None: ...
    @property
    def send_timeout_ms(self) -> int: ...
    @send_timeout_ms.setter
    def send_timeout_ms(self, value: int) -> None: ...
    @property
    def receive_timeout_ms(self) -> int: ...
    @receive_timeout_ms.setter
    def receive_timeout_ms(self, value: int) -> None: ...
    @property
    def immediate(self) -> bool: ...
    @immediate.setter
    def immediate(self, value: bool) -> None: ...

class DealerSocketOptions:
    @property
    def probe(self) -> bool: ...
    @probe.setter
    def probe(self, value: bool) -> None: ...

class RouterSocketOptions:
    @property
    def mandatory(self) -> bool: ...
    @mandatory.setter
    def mandatory(self, value: bool) -> None: ...
    @property
    def handover(self) -> bool: ...
    @handover.setter
    def handover(self, value: bool) -> None: ...
    @property
    def probe(self) -> bool: ...
    @probe.setter
    def probe(self, value: bool) -> None: ...
    @property
    def connect_routing_id(self) -> RoutingId: ...
    @connect_routing_id.setter
    def connect_routing_id(self, routing_id: RoutingId | bytes) -> None: ...

class PubSocketOptions:
    @property
    def verbose(self) -> bool: ...
    @verbose.setter
    def verbose(self, value: bool) -> None: ...
    @property
    def verboser(self) -> bool: ...
    @verboser.setter
    def verboser(self, value: bool) -> None: ...
    @property
    def manual(self) -> bool: ...
    @manual.setter
    def manual(self, value: bool) -> None: ...
    @property
    def no_drop(self) -> bool: ...
    @no_drop.setter
    def no_drop(self, value: bool) -> None: ...

class SubSocketOptions:
    @property
    def topics_count(self) -> int: ...   # read-only

class StreamSocketOptions:
    @property
    def notify(self) -> bool: ...
    @notify.setter
    def notify(self, value: bool) -> None: ...
```

---

## Send / Recv Flags

### SendFlags

```python
class SendFlags:
    NONE = 0
    DONT_WAIT = 1
```

### RecvFlags

```python
class RecvFlags:
    NONE = 0
    DONT_WAIT = 1
```

---

## Message / Domain

### Message

```python
class Message:
    def __init__(self, size: int | None = None) -> None: ...                 # Raises: ConfigError
    @classmethod
    def copy_from(cls, data: bytes | bytearray | memoryview) -> Message: ... # Raises: ConfigError
    @staticmethod
    def from_bytes(data: bytes | bytearray | memoryview) -> Message: ...     # Raises: ConfigError
    # Public input adapters are copy-based only; borrowed external-wrap APIs
    # are intentionally not exposed on managed bindings.
    def size(self) -> int: ...                                               # Raises: ConfigError
    @property
    def data(self) -> memoryview: ...                                        # Raises: ConfigError
    def to_bytes(self) -> bytes: ...                                         # Raises: ConfigError
    def get_property(self, name: str) -> str | None: ...                     # Raises: ConfigError
    def ref_count(self) -> int: ...                                          # Raises: ConfigError
    def send(self, socket) -> None: ...                                      # Raises: SubmitError
    def close(self) -> None: ...                                             # Raises: CloseError
    # supports `with` and `async with` — __exit__ raises CloseError
```

### Codec Extensions

The binding exposes separate codec extension packages. The distribution package
names and import module names are fixed to:

- PyPI `zlink-codec-protobuf` -> import `zlink_codec_protobuf`
- PyPI `zlink-codec-json` -> import `zlink_codec_json`
- PyPI `zlink-codec-messagepack` -> import `zlink_codec_messagepack`

These are separate public modules layered on top of the core `zlink` package.
They must not be merged into `zlink.__init__` as unconditional dependencies,
and they do not extend a shared `zlink.codec.*` namespace.

JSON codec baseline: stdlib `json`.
MessagePack codec baseline: `msgpack`.

```python
# zlink_codec_protobuf
from typing import Any, TypeVar

TProto = TypeVar("TProto")

def parse_proto(message: Message, message_type: type[TProto]) -> TProto: ...
def to_message(value: Any) -> Message: ...
```

```python
# zlink_codec_json
from typing import Any, TypeVar

TJson = TypeVar("TJson")

def parse_json(message: Message, cls: type[TJson]) -> TJson: ...
def to_message(value: Any) -> Message: ...
```

```python
# zlink_codec_messagepack
from typing import Any, TypeVar

TMessagePack = TypeVar("TMessagePack")

def parse_messagepack(message: Message, cls: type[TMessagePack]) -> TMessagePack: ...
def to_message(value: Any) -> Message: ...
```

### RoutingId

Binary-safe immutable value type (1-255 bytes). Construction from a raw
`str` is **not** supported by default; string conversion is exposed only
through convenience helpers (`to_hex()`, `__str__`).

```python
class RoutingId:
    def __init__(self, data: bytes | bytearray) -> None: ...      # Raises: ConfigError
    @classmethod
    def from_bytes(cls, data: bytes | bytearray) -> "RoutingId": ...  # Raises: ConfigError
    @classmethod
    def from_string(cls, value: str) -> "RoutingId": ...  # Parses to_hex(); max 510 hex chars; invalid or >255 decoded bytes raises ValueError

    def to_bytes(self) -> bytes: ...

    @property
    def size(self) -> int: ...       # byte length (1..255)

    def __bytes__(self) -> bytes: ...
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

    # Convenience only — not part of the canonical shape:
    def to_hex(self) -> str: ...
    def __str__(self) -> str: ...
    def __repr__(self) -> str: ...
```

### Received

`Received` is the PAIR / DEALER / ROUTER recv result. It mirrors the
canonical `Received` shape defined in the binding spec (no `topic` field,
but with `request_seq` populated when the message arrived as part of a
request-reply exchange).

```python
class Received:
    routing_id: RoutingId | None             # peer_rid (Router) / source_node_rid (Spot)
    spot_rid: RoutingId | None               # SPOT routed recv 에서만 설정
    request_seq: int | None                  # set when routed over request-reply; None otherwise
    parts: tuple[Message, ...]

    def is_single_part(self) -> bool: ...
    def first_part(self) -> Message: ...              # Raises: RecvError
    def single_part_or_throw(self) -> Message: ...    # Raises: RecvError

    # reply — request_seq 가 None 이 아니어야 함. None 또는 invalid reply
    # context 는 SubmitError.
    def reply(
        self,
        parts: Message | list[Message],
        *,
        flags: int = 0,
    ) -> None: ...                                    # Raises: SubmitError

    def close(self) -> None: ...                      # Raises: CloseError
    def __enter__(self) -> "Received": ...
    def __exit__(self, *args) -> None: ...            # Raises: CloseError
```

### TopicMessage

```python
class TopicMessage:
    routing_id: RoutingId | None             # None when transport carries no source id
    service_name: str | None                 # Spot subscribe only; None for raw SUB / XSUB
    topic: str                               # UTF-8
    parts: tuple[Message, ...]

    def is_single_part(self) -> bool: ...
    def first_part(self) -> Message: ...
    def single_part_or_throw(self) -> Message: ...
    def close(self) -> None: ...             # Raises: CloseError
    def __enter__(self) -> "TopicMessage": ...
    def __exit__(self, *args) -> None: ...
```

### SubscriptionEvent

Value object delivered by `XPubSocket.receive_subscription_event()` and
`Spot.receive_subscription_event()`.
Fields only — no `close()` / lifecycle methods.

```python
class SubscriptionEvent:
    routing_id: RoutingId | None    # subscriber routing id; None if transport carries none
    service_name: str | None        # Spot subscription event only; None for XPub
    topic: str                       # UTF-8 topic string (NOT bytes)
    subscribed: bool                 # True = subscribe, False = unsubscribe
```

### SubmitResult

Result codes for send/request/reply/publish operations.
All failures raise `SubmitError` (a `ZlinkError` subclass) with
`.result` exposing the typed enum and `.code` the globally unique int.

```python
class SubmitResult(IntEnum):
    OK = 0
    BACKPRESSURED = 1
    NOT_CONNECTED = 2
    NOT_FOUND = 3
    TERMINATED = 4
    INVALID_HANDLE = 5
    INVALID_ARGUMENT = 6
    NOT_SUPPORTED = 7
    INVALID_STATE = 8
    THREAD_VIOLATION = 9
    OUT_OF_MEMORY = 10
    SEQ_EXHAUSTED = 11
    INTERNAL_ERROR = 12
    NOT_ADMITTED = 13   # target peer has weight 0
```

### RequestResult

Result codes for request completion callbacks.

```python
class RequestResult(IntEnum):
    OK = 0
    TIMED_OUT = 101
    NOT_FOUND = 102
    TERMINATED = 103
    PROTOCOL_ERROR = 104
```

### RecvResult

Result codes for recv, subscribe, and subscription event operations.

```python
class RecvResult(IntEnum):
    OK = 0
    NO_DATA = 201
    BUSY = 202
    TERMINATED = 203
    INVALID_HANDLE = 204
    NOT_SUPPORTED = 205
```

### HandlerResult

Result codes for handler registration operations (`on_packet`,
`on_send_ready`, `on_routed_receive`, `on_dispatch_event`, etc.).

```python
class HandlerResult(IntEnum):
    OK = 0
    INVALID_ARGUMENT = 301
    BUSY = 302
    NOT_SUPPORTED = 303
    DEADLOCK = 304
    INVALID_HANDLE = 305
```

### CloseResult

Result codes for close and destroy operations.

```python
class CloseResult(IntEnum):
    OK = 0
    BUSY = 401
    SHUTDOWN = 402
    INVALID_HANDLE = 403
```

### BindResult

Result codes for bind operations.

```python
class BindResult(IntEnum):
    OK = 0
    INVALID_ARGUMENT = 501
    ADDR_IN_USE = 502
    NOT_SUPPORTED = 503
    INVALID_HANDLE = 504
```

### ConnectResult

Result codes for connect, disconnect, and unbind operations.

```python
class ConnectResult(IntEnum):
    OK = 0
    INVALID_ARGUMENT = 601
    NOT_SUPPORTED = 602
    INVALID_HANDLE = 603
```

### ConfigResult

Result codes for configuration, option, and snapshot operations.

```python
class ConfigResult(IntEnum):
    OK = 0
    INVALID_HANDLE = 701
    INVALID_ARGUMENT = 702
    NOT_SUPPORTED = 703
```

### ZlinkError

Common base class for all zlink exceptions. Per the binding-wide
**Per-Function Error Type Hierarchy** policy, `ZlinkError` is never
raised directly; each failing operation raises one of the **8
function-category subclasses** below. Callers that want to handle every
zlink failure uniformly can still catch `ZlinkError`; callers that need
discrimination catch the specific subclass.

Every subclass wraps the matching C-API result enum
(`SubmitResult` / `RequestResult` / `RecvResult` / `HandlerResult` /
`CloseResult` / `BindResult` / `ConnectResult` / `ConfigResult`) in its
`result` property and exposes the platform `internal_errno` captured
at failure site.

The `code` property is a globally unique `int` that spans all result
enum ranges (0-703). The code alone identifies the error without
needing to know which enum it belongs to; `result` provides the typed
enum view for code that prefers enum matching.

```python
class ZlinkError(Exception):
    def __init__(self, code: int, internal_errno: int = 0): ...

    @property
    def code(self) -> int: ...

    @property
    def internal_errno(self) -> int: ...
```

### SubmitError

Raised by send / publish / request submit / reply submit operations.
Wraps `SubmitResult`.

```python
class SubmitError(ZlinkError):
    def __init__(self, result: SubmitResult, internal_errno: int = 0) -> None: ...

    @property
    def result(self) -> SubmitResult: ...
```

### RequestError

Reported to request-completion callbacks (and raised by async request
variants) when a request fails after submit. Wraps `RequestResult`.

```python
class RequestError(ZlinkError):
    def __init__(self, result: RequestResult, internal_errno: int = 0) -> None: ...

    @property
    def result(self) -> RequestResult: ...
```

### RecvError

Raised by recv / subscribe / subscription-event / monitor recv / timer
recv operations. Wraps `RecvResult`.

```python
class RecvError(ZlinkError):
    def __init__(self, result: RecvResult, internal_errno: int = 0) -> None: ...

    @property
    def result(self) -> RecvResult: ...
```

### HandlerError

Raised by handler-registration operations (`on_packet`,
`on_send_ready`, `on_event`, `on_fire`, `on_routed_receive`,
`on_dispatch_event`, etc.).
Wraps `HandlerResult`.

```python
class HandlerError(ZlinkError):
    def __init__(self, result: HandlerResult, internal_errno: int = 0) -> None: ...

    @property
    def result(self) -> HandlerResult: ...
```

### CloseError

Raised by `close()` / `destroy()` / `__exit__` / `shutdown()`
operations. Wraps `CloseResult`.

```python
class CloseError(ZlinkError):
    def __init__(self, result: CloseResult, internal_errno: int = 0) -> None: ...

    @property
    def result(self) -> CloseResult: ...
```

### BindError

Raised by `bind()` operations. Wraps `BindResult`.

```python
class BindError(ZlinkError):
    def __init__(self, result: BindResult, internal_errno: int = 0) -> None: ...

    @property
    def result(self) -> BindResult: ...
```

### ConnectError

Raised by `connect()` / `disconnect()` / `unbind()` operations.
Wraps `ConnectResult`.

```python
class ConnectError(ZlinkError):
    def __init__(self, result: ConnectResult, internal_errno: int = 0) -> None: ...

    @property
    def result(self) -> ConnectResult: ...
```

### ConfigError

Raised by option set/get, snapshot, poller mutation, timer config,
`attach_discovery`, message lifecycle, and `set_tls_*` operations.
Wraps `ConfigResult`.

```python
class ConfigError(ZlinkError):
    def __init__(self, result: ConfigResult, internal_errno: int = 0) -> None: ...

    @property
    def result(self) -> ConfigResult: ...
```

---

## Monitoring

### MonitorSocket (SocketMonitor)

Starts in recv model. `on_event(...)` transitions one-way to callback-only
model; after that `recv()` raises a busy recv error and `snapshot()` still works.

```python
class MonitorSocket:
    # No-op callback for callback-only model. Pass to on_event() to keep a
    # valid handler when the application does not care about events; once
    # installed the monitor is in callback-only model and recv() raises a
    # busy recv error (snapshot() still works). To drive the monitor via
    # snapshot() / recv() instead, leave on_event unset.
    # Maps to zlink_monitor_ignore_handler.
    ignore_handler: ClassVar[Callable[[MonitorEvent], None]]

    def recv(self) -> MonitorEvent: ...                                          # Raises: RecvError
    def on_event(self, handler: Callable[[MonitorEvent], None]) -> None: ...     # Raises: HandlerError
    def snapshot(self) -> MonitorSnapshot: ...                                   # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### MonitorSnapshot

Runtime state snapshot exposed by `MonitorSocket.snapshot()` and
the socket-monitor path. Every binding is required to expose the canonical
fields together with the `is_ready()` convenience accessor.

```python
class MonitorSnapshot:
    source_kind: int                 # monitor target kind
    state_flags: int                 # state bitmask
    detail_flags: int                # detail bitmask
    snd_pending_msgs: int            # pending send-queue messages
    rcv_pending_msgs: int            # pending receive-queue messages
    auto_hwm_enabled: bool
    auto_hwm_profile: int
    auto_hwm_role: int
    auto_hwm_policy_class: int
    auto_hwm_managed_connections: int
    auto_hwm_active_hwm_connections: int
    auto_hwm_observed_count: int
    auto_hwm_planning_count: int
    auto_hwm_context_total_planning_count: int
    auto_hwm_base_floor_per_connection: int
    auto_hwm_unit_budget_bytes: int
    auto_hwm_size_cap: int
    auto_hwm_effective_publish_fanout: int
    auto_hwm_applied_sndhwm: int
    auto_hwm_applied_rcvhwm: int
    auto_hwm_requested_sndbuf: int
    auto_hwm_requested_rcvbuf: int
    auto_hwm_effective_sndbuf: int
    auto_hwm_effective_rcvbuf: int
    auto_hwm_total_memory_budget_bytes: int
    auto_hwm_queue_budget_bytes: int
    auto_hwm_transport_budget_bytes: int
    auto_hwm_runtime_reserve_bytes: int
    auto_hwm_socket_queue_share_bytes: int
    auto_hwm_socket_message_slots: int
    auto_hwm_effective_message_bytes: int
    auto_hwm_estimated_max_memory_bytes: int
    auto_hwm_last_recalc_ms: int
    auto_hwm_last_recalc_reason: int
    auto_hwm_send_blocked_ratio_ppm: int
    auto_hwm_scope: int
    auto_hwm_scope_count: int
    auto_hwm_auto_buffer_bytes: int
    auto_hwm_manual_buffer_bytes: int
    auto_hwm_buffer_connections: int
    auto_hwm_deferred_sndhwm: int
    auto_hwm_deferred_rcvhwm: int

    def is_ready(self) -> bool: ...  # True when the ready bit is set in state_flags
```

### MonitorEvent

Value object emitted by `MonitorSocket.recv()` / `on_event(...)`. The
canonical name is `MonitorEvent`. `SocketMonitorEvent` is exported as an
alias for backward compatibility.

```python
class MonitorEvent:
    event: MonitorEventType          # enum (CONNECTION_READY, CONNECTED, DISCONNECTED, PEER_WEIGHT_CHANGED, ...)
    value: int                       # event-specific detail (PEER_WEIGHT_CHANGED carries the new 0..100 weight)
    routing_id: RoutingId | None     # peer routing id when carried by the event, else None
    local_addr: str                  # local endpoint
    remote_addr: str                 # remote endpoint

SocketMonitorEvent = MonitorEvent    # backward-compat alias; prefer MonitorEvent
```

`MonitorEventType` includes `PEER_WEIGHT_CHANGED` (bit 15).

### MonitorEventMask

Bitflag type supplied to `monitor_open(events=...)` to select which
monitor events to deliver. `ALL` selects every event.

```python
class MonitorEventMask(IntFlag):
    NONE = 0
    ALL = ...
    # Individual event bits match the C-side MonitorEventType values.
```

---

## Services

### Registry

```python
class Registry:
    def __init__(self, ctx: Context) -> None: ...
    def bind(self, pub_endpoint: str, router_endpoint: str) -> None: ...         # Raises: BindError
    def set_id(self, registry_id: int) -> None: ...                              # Raises: ConfigError
    def add_peer(self, peer_pub_endpoint: str) -> None: ...                      # Raises: ConfigError
    def set_tls_server(self, cert: str, key: str,
                       require_client_cert: bool = False) -> None: ...           # Raises: ConfigError
    def set_tls_client(self, ca_cert: str | None, hostname: str | None,
                       trust_system: bool = False) -> None: ...                  # Raises: ConfigError
    def set_heartbeat(self, interval_ms: int, timeout_ms: int) -> None: ...      # Raises: ConfigError
    def set_broadcast_interval(self, interval_ms: int) -> None: ...              # Raises: ConfigError
    def status_snapshot(self) -> RegistryStatus: ...                             # Raises: ConfigError
    def service_summary_snapshot(self,
        filter_: RegistryServiceSummaryFilter | None = None
    ) -> list[RegistryServiceSummaryEntry]: ...                                  # Raises: ConfigError
    def member_peers(self, service_type: int,
                     service_name: str) -> list[MemberPeerEntry]: ...            # Raises: ConfigError
    def member_peer_metadata(self, service_type: int, service_name: str,
                             service_role: int, endpoint: str) -> bytes: ...     # Raises: ConfigError
    def topology_snapshot(self) -> list[RegistryTopologyEntry]: ...              # Raises: ConfigError
    def topology_query(self, filter_: RegistryTopologyFilter) -> list[RegistryTopologyEntry]: ...  # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### DiscoveryDealerPeerMode

```python
class DiscoveryDealerPeerMode(IntEnum):
    ROUTER = 1
    DEALER = 2
```

### Discovery

```python
class Discovery:
    def __init__(self, ctx: Context, service_type: int, service_name: str) -> None: ...
    def connect_registry(self, registry_endpoint: str) -> None: ...              # Raises: ConnectError
    def set_value(self, value: int) -> None: ...                                 # Raises: ConfigError
    def get_value(self) -> int: ...                                              # Raises: ConfigError
    def set_metadata(self, data: bytes | bytearray) -> None: ...                 # Raises: ConfigError
    def get_metadata(self) -> bytes: ...                                         # Raises: ConfigError
    def member_peers(self) -> list[MemberPeerEntry]: ...                         # Raises: ConfigError
    def member_peer_metadata(self, service_role: int, endpoint: str) -> bytes: ...  # Raises: ConfigError
    def resolve_spot(self, spot_rid: RoutingId) -> RoutingId: ...                # Raises: ConfigError — maps to zlink_discovery_resolve_spot
    def set_dealer_peer_mode(self, mode: DiscoveryDealerPeerMode) -> None: ...   # Raises: ConfigError — maps to zlink_discovery_set_dealer_peer_mode
    def set_tls_client(self, ca_cert: str | None, hostname: str | None,
                       trust_system: bool = False) -> None: ...                  # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### SpotNode

```python
class SpotNode:
    def __init__(self, ctx: Context, mode: SpotNodeMode | int | None = None): ...
    def __init__(self, ctx: Context) -> None: ...
    def bind(self, endpoint: str) -> None: ...                                   # Raises: BindError
    def last_endpoint(self) -> str: ...                                          # Raises: ConfigError
    def connect_peer(self, endpoint: str) -> None: ...                           # Raises: ConnectError
    def disconnect_peer(self, endpoint: str) -> None: ...                        # Raises: ConnectError
    def attach_discovery(self, discovery: Discovery) -> None: ...                # Raises: ConfigError
    def attach_channel_dealer(self, discovery: Discovery, dealer: DealerSocket) -> None: ...  # Raises: ConfigError
    def attach_channel_dealer_manual(self, channel_name: str, dealer: DealerSocket) -> None: ...  # Raises: ConfigError
    def attach_pub_ingress(self, pub: PubSocket) -> None: ...                    # Raises: ConfigError

    # --- identity / routing ---
    # SpotNode's logical address. Maps to zlink_set_routing_id(node, ...) /
    # zlink_get_routing_id(node, ...).
    def set_routing_id(self, rid: RoutingId) -> None: ...                        # Raises: ConfigError
    @property
    def routing_id(self) -> RoutingId: ...                                       # Raises: ConfigError

    def set_tls_server(self, cert: str, key: str,
                       require_client_cert: bool = False) -> None: ...           # Raises: ConfigError
    def set_tls_client(self, ca_cert: str | None, hostname: str | None,
                       trust_system: bool = False) -> None: ...                  # Raises: ConfigError
    def create_spot(self) -> Spot: ...                                           # Raises: ConfigError
    def status_snapshot(self) -> SpotNodeStatus: ...                             # Raises: ConfigError
    def peers_snapshot(self) -> list[SpotNodePeerEntry]: ...                     # Raises: ConfigError
    def peers_query(self, filter_: SpotNodePeerFilter | None = None
                    ) -> list[SpotNodePeerEntry]: ...                            # Raises: ConfigError
    def subjects_snapshot(self, filter_: SpotNodeSubjectFilter | None = None
                          ) -> list[SpotNodeSubjectEntry]: ...                   # Raises: ConfigError
    def internal_sockets_snapshot(
        self,
        filter_: SpotNodeSocketSnapshotFilter | None = None
    ) -> list[SpotNodeSocketSnapshotEntry]: ...                                  # Raises: ConfigError
    # close() cascades: closes all live Spot handles before the node becomes invalid.
    def close(self) -> None: ...                                                 # Raises: CloseError
```

`SpotNode` owns the lifecycle. `Spot` is created only through
`SpotNode.create_spot()`. Direct `Spot(node)` construction is internal
and is not part of the public API contract.

### Spot

```python
class Spot:
    # __init__(node) is internal. Public code must use SpotNode.create_spot().

    # --- identity / routing ---
    # Spot's logical address / routed ownership key.
    # Maps to zlink_set_routing_id(spot, ...) / zlink_get_routing_id(spot, ...).
    def set_routing_id(self, rid: RoutingId) -> None: ...                        # Raises: ConfigError
    @property
    def routing_id(self) -> RoutingId: ...                                       # Raises: ConfigError

    def publish(self, service_name: str, topic: bytes | str, payload: Message | bytes | list, *, flags: int = 0) -> bool: ...  # Raises: SubmitError
    def send_channel(self, channel_name: str, payload: Message | bytes | list, *, flags: int = 0) -> bool: ...  # Raises: SubmitError
    async def request_channel(self, channel_name: str, payload: Message | bytes | list,
                              *, timeout: int = 0) -> list[Message]: ...
    def request_channel(self, channel_name: str,
                        payload: Message | bytes | list,
                        callback: Callable[[RequestResult, list[Message]], None],
                        *, flags: int = 0, timeout: int = 0) -> bool: ...
    def set_subscription(self, topic_or_pattern: bytes | str) -> None: ...       # Raises: ConfigError
    def unset_subscription(self, topic_or_pattern: bytes | str) -> None: ...     # Raises: ConfigError
    def subscribe(self, *, flags: int = 0) -> TopicMessage | None: ...           # Raises: RecvError
    def receive_subscription_event(self, *, flags: int = 0) -> SubscriptionEvent: ...  # Raises: RecvError
    def on_send_ready(self, handler: Callable[[Spot], None]) -> None: ...        # Raises: HandlerError

    # --- routed request (spot → spot, async) — no flags ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure; RequestError on request completion failure.
    async def request_to_spot(self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
                              payload: Message | bytes | list,
                              *, timeout: int = 0) -> list[Message]: ...

    # --- routed request (spot → spot, callback submit) ---
    # timeout = 0 uses the socket default timeout.
    # Returns False only for temporary backpressure when flags includes DONTWAIT.
    # Raises: SubmitError on submit failure other than temporary backpressure.
    # Callback receives RequestResult; non-OK indicates request-completion failure.
    # Callback receives an empty list on failure.
    def request_to_spot(self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
                        payload: Message | bytes | list,
                        callback: Callable[[RequestResult, list[Message]], None],
                        *, flags: int = 0, timeout: int = 0) -> bool: ...  # Raises: SubmitError

    # --- routed request (spot → router, async) — no flags ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure; RequestError on request completion failure.
    async def request_to_router(self, peer_rid: RoutingId,
                                payload: Message | bytes | list,
                                *, timeout: int = 0) -> list[Message]: ...

    # --- routed request (spot → router, callback submit) ---
    # timeout = 0 uses the socket default timeout.
    # Returns False only for temporary backpressure when flags includes DONTWAIT.
    # Raises: SubmitError on submit failure other than temporary backpressure.
    # Callback receives RequestResult; non-OK indicates request-completion failure.
    # Callback receives an empty list on failure.
    def request_to_router(self, peer_rid: RoutingId,
                          payload: Message | bytes | list,
                          callback: Callable[[RequestResult, list[Message]], None],
                          *, flags: int = 0, timeout: int = 0) -> bool: ...  # Raises: SubmitError

    # --- routed reply (spot → spot) ---
    def reply_to_spot(self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
                      request_seq: int,
                      payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError

    # --- routed reply (spot → router) ---
    def reply_to_router(self, peer_rid: RoutingId, request_seq: int,
                        payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError

    # --- routed receive ---
    def recv_routed(self, *, flags: int = 0) -> Received: ...                    # Raises: RecvError
    def on_routed_receive(self, handler: Callable) -> None: ...                  # Raises: HandlerError
    def on_dispatch_event(self, handler: Callable[[Spot, SpotDispatchInfo], None]) -> None: ...  # Raises: HandlerError
    def drain_channel_reply_from(self, subject) -> None: ...                     # Raises: ConfigError

    def close(self) -> None: ...                                                 # Raises: CloseError
```

```python
class SpotDispatchEvent(IntEnum):
    SUBSCRIBE_READABLE = 1
    ROUTED_READABLE = 2
    TIMER_READABLE = 3
    CHANNEL_REPLY_READABLE = 4

class SpotDispatchSubjectKind(IntEnum):
    SPOT = 1
    TIMER = 2
    CHANNEL_DEALER = 3

@dataclass(frozen=True)
class SpotDispatchInfo:
    event: SpotDispatchEvent
    subject_kind: SpotDispatchSubjectKind
    subject: object
```

For `SUBSCRIBE_READABLE` and `ROUTED_READABLE`, callers must keep draining
`subscribe(...)` / `recv_routed(...)` until the binding raises no-data /
`EAGAIN`.

### RegistryQueryClient

```python
class RegistryQueryClient:
    def __init__(self, ctx: Context) -> None: ...
    def connect(self, endpoint: str) -> None: ...                                # Raises: ConnectError
    def snapshot(self, filter_: RegistryTopologyFilter | None = None
                 ) -> list[RegistryTopologyEntry]: ...                           # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### Service-Layer Entry Types

Value objects returned by service-layer snapshot/query APIs. All are
frozen dataclass-shaped: named fields only, no mutation, no lifecycle
methods. Field types follow the canonical C struct definitions exposed
in `core/include/zlink.h`; fixed-size C strings are decoded to `str`.

Primary entry types used in the default service flow:

```python
@dataclass(frozen=True)
class MemberPeerEntry:
    service_type: int                # zlink_service_type_t
    service_role: int
    service_name: str
    endpoint: str
    routing_id: RoutingId
    value: int                       # int64
    weight: int                      # uint32, 0..100

@dataclass(frozen=True)
class RegistryTopologyEntry:
    routing_id: RoutingId
    service_kind: int                # zlink_service_kind_t
    service_role: int
    service_name: str
    endpoint: str
    source: int                      # zlink_topology_source_t
    state: int                       # zlink_topology_state_t
    desired_count: int
    ready_count: int
    error_code: int
    last_reported_ms: int

@dataclass(frozen=True)
class SpotNodeStatus:
    service_name: str
    local_endpoint: str
    node_routing_id: RoutingId
    state: int                       # zlink_spot_node_state_t
    configured_peer_count: int
    active_peer_count: int
    connected_peer_count: int
    subject_count: int
    ready_subject_count: int
    disconnected_sub_target_count: int
    disconnected_routed_target_count: int
    last_error: int
    last_changed_ms: int
```

Advanced / Diagnostic entry types and filters:

```python
@dataclass(frozen=True)
class RegistryServiceSummaryEntry:
    service_kind: int                # zlink_service_kind_t
    service_role: int
    service_name: str
    total_count: int
    connecting_count: int
    ready_count: int
    error_count: int
    stopped_count: int
    last_reported_ms: int

@dataclass(frozen=True)
class RegistryStatus:
    registry_id: int
    bind_endpoint: str
    state: int                       # zlink_registry_state_t
    topology_entry_count: int
    peer_registry_count: int
    connected_peer_registry_count: int
    list_seq: int
    last_error: int
    last_changed_ms: int

@dataclass(frozen=True)
class SpotNodePeerEntry:
    service_name: str
    local_endpoint: str
    peer_endpoint: str
    source: int                      # zlink_spot_peer_source_t
    state: int                       # zlink_spot_peer_state_t
    weight: int                      # uint32, 0..100
    connected_since_ms: int
    last_changed_ms: int

@dataclass(frozen=True)
class SpotNodeSubjectEntry:
    role: int                        # zlink_spot_role_t
    subject: str
    subject_kind: int
    ready_peer_count: int
    active_peer_count: int
    last_changed_ms: int

@dataclass(frozen=True)
class RegistryServiceSummaryFilter:
    service_kind: int | None = None
    service_role: int | None = None
    service_name: str | None = None

@dataclass(frozen=True)
class RegistryTopologyFilter:
    service_kind: int | None = None
    service_role: int | None = None
    service_name: str | None = None
    routing_id: RoutingId | None = None
    state: int | None = None
    source: int | None = None

@dataclass(frozen=True)
class SpotNodePeerFilter:
    peer_endpoint: str | None = None
    source: int | None = None
    state: int | None = None

@dataclass(frozen=True)
class SpotNodeSubjectFilter:
    role: int | None = None
    subject: str | None = None
    subject_kind: int | None = None
```

---

## Poller

```python
class Poller:
    def __init__(self) -> None: ...
    def add_socket(self, socket, events: int, tag: object = None) -> None: ...   # Raises: ConfigError
    def add_fd(self, fd: int, events: int, tag: object = None) -> None: ...      # Raises: ConfigError
    def add_timer(self, timer: Timer, user_data: object = None) -> None: ...     # Raises: ConfigError
    def modify_socket(self, socket, events: int) -> None: ...                    # Raises: ConfigError
    def modify_fd(self, fd: int, events: int) -> None: ...                       # Raises: ConfigError
    def remove_socket(self, socket) -> None: ...                                 # Raises: ConfigError
    def remove_fd(self, fd: int) -> None: ...                                    # Raises: ConfigError
    def remove_timer(self, timer: Timer) -> None: ...                            # Raises: ConfigError
    def size(self) -> int: ...                                                   # Raises: ConfigError — number of registered items; maps to zlink_poller_size
    def poll(self, timeout_ms: int) -> list[dict]: ...                           # Raises: RecvError
    def close(self) -> None: ...                                                 # Raises: CloseError
    # supports `with` and `async with` — __exit__ raises CloseError
```

The current public poller contract is still generic. It does not yet expose a
Spot-aware result carrying owner `Spot`, dispatch event kind, and drain
subject together.

---

## Timer

### Timer

```python
class Timer:
    def __init__(self) -> None: ...
    @classmethod
    def from_spot(cls, spot: Spot) -> Timer: ...                                 # Raises: ConfigError
    def start(self, interval_ns: int, repeat_count: int) -> None: ...            # Raises: ConfigError
    def stop(self) -> None: ...                                                  # Raises: ConfigError
    def recv(self, flags: int = 0) -> int: ...                                   # Raises: RecvError
    def on_fire(self, handler: Callable[[Timer, int], None]) -> None: ...        # Raises: HandlerError
    def close(self) -> None: ...                                                 # Raises: CloseError
    # supports `with` and `async with` — __exit__ raises CloseError
```

---

## Utilities

### Stopwatch

High-resolution stopwatch for measuring elapsed time.

```python
class Stopwatch:
    def __init__(self) -> None: ...

    def intermediate(self) -> int:
        """Return elapsed microseconds without stopping.

        Raises: ConfigError
        """
        ...

    def stop(self) -> int:
        """Stop the stopwatch and return total elapsed microseconds.

        Raises: ConfigError
        """
        ...

    def close(self) -> None: ...                 # Raises: CloseError
    # supports `with` and `async with` — __exit__ raises CloseError
```

### Thread

Background thread managed by the C library.

```python
class Thread:
    def __init__(self, target: Callable[[], None]) -> None:
        """Start a new thread running the given callable.

        Raises: ConfigError
        """
        ...

    def join(self) -> None:
        """Wait for the thread to finish and release its handle.

        Raises: CloseError
        """
        ...
```

### AtomicCounter

Lock-free atomic counter.

```python
class AtomicCounter:
    def __init__(self) -> None: ...

    def set(self, value: int) -> None: ...       # Raises: ConfigError
    def increment(self) -> int: ...              # Raises: ConfigError
    def decrement(self) -> int: ...              # Raises: ConfigError
    @property
    def value(self) -> int: ...                  # Raises: ConfigError

    def close(self) -> None: ...                 # Raises: CloseError
    # supports `with` and `async with` — __exit__ raises CloseError
```

---

## Module-Level

```python
def version() -> tuple[int, int, int]: ...

# Module-level errno() is NOT public. Access internal errno via
# ZlinkError.internal_errno on the caught exception.

def strerror(code: int) -> str:
    """Return a human-readable string for the given error number."""
    ...

def has(capability: str) -> bool:
    """Check if the library supports a given capability (e.g. 'ipc', 'tls')."""
    ...

def proxy(frontend, backend, capture=None) -> None:
    """Start a built-in proxy between frontend and backend sockets.

    Raises: ConfigError
    """
    ...

def proxy_steerable(frontend, backend, capture, control) -> None:
    """Start a steerable proxy with an additional control socket.

    Raises: ConfigError
    """
    ...

def sleep(seconds: int) -> None:
    """Sleep for the given number of seconds."""
    ...

def multipart_close(parts: list[Message]) -> None:
    """Close all parts in a multipart message array.

    Raises: CloseError
    """
    ...
```

## Peer Disconnect by Routing ID

Python bindings expose `Socket.disconnect_rid(routing_id)` and
`SpotNode.disconnect_peer_rid(target_node_rid)`. The duplicate policy option
and `NOT_FOUND` / `CONFLICT` / `BUSY` connect errors mirror the C core. `Spot`
does not expose a peer-rid disconnect method.
