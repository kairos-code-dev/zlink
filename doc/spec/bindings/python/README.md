[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Python Binding Specification

This document defines the complete public API surface of the zlink Python binding.
Every class, method, and type listed here is part of the contract that the binding
must expose. Internal/private methods are omitted.

---

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
    @property
    def socket_limit(self) -> int: ...       # read-only
    @property
    def msg_t_size(self) -> int: ...          # read-only
    def add_thread_affinity(self, cpu: int) -> None: ...
    def remove_thread_affinity(self, cpu: int) -> None: ...
```

---

## Socket Types

All sockets support `with` / `async with` context managers.

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
    def send(self, payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError
    def recv(self, *, flags: int = 0) -> Received: ...                           # Raises: RecvError
    def on_receive(self, handler: Callable[[Received], None]) -> None: ...       # Raises: HandlerError
    def on_send_ready(self, handler: Callable[[PairSocket], None]) -> None: ...  # Raises: HandlerError
    def monitor_open(self, events: MonitorEvent = MonitorEvent.ALL) -> MonitorSocket: ...  # Raises: ConfigError
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
    def publish(self, topic: bytes | str, payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError
    def on_send_ready(self, handler: Callable[[PubSocket], None]) -> None: ...   # Raises: HandlerError
    def monitor_open(self, events: MonitorEvent = MonitorEvent.ALL) -> MonitorSocket: ...  # Raises: ConfigError
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
    def subscribe(self, *, flags: int = 0) -> Subscribed: ...                    # Raises: RecvError
    def on_subscribe(self, handler: Callable[[Subscribed], None]) -> None: ...   # Raises: HandlerError
    def monitor_open(self, events: MonitorEvent = MonitorEvent.ALL) -> MonitorSocket: ...  # Raises: ConfigError
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
    def send(self, payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError
    def recv(self, *, flags: int = 0) -> Received: ...                             # Raises: RecvError
    def on_receive(self, handler: Callable[[Received], None]) -> None: ...         # Raises: HandlerError
    def on_send_ready(self, handler: Callable[[DealerSocket], None]) -> None: ...  # Raises: HandlerError
    def monitor_open(self, events: MonitorEvent = MonitorEvent.ALL) -> MonitorSocket: ...  # Raises: ConfigError
    def attach_discovery(self, discovery: Discovery) -> None: ...                  # Raises: ConfigError

    # --- request (async) — no flags ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure; RequestError on request completion failure.
    async def request(self, payload: Message | bytes | list,
                      *, timeout: int = 0) -> Received: ...

    # --- request (callback) — raises on submit failure ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure. Callback receives RequestResult;
    #   non-OK indicates request-completion failure (RequestError semantics).
    def request(self, payload: Message | bytes | list,
                callback: Callable[[RequestResult, Received | None], None],
                *, flags: int = 0, timeout: int = 0) -> None: ...

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
    def send(self, routing_id: RoutingId, payload: Message | bytes | list[Message], *, flags: int = 0) -> None: ...  # Raises: SubmitError
    def recv(self, *, flags: int = 0) -> Received: ...                           # Raises: RecvError
    def on_receive(self, handler: Callable[[Received], None]) -> None: ...       # Raises: HandlerError
    def on_send_ready(self, handler: Callable) -> None: ...                      # Raises: HandlerError
    def monitor_open(self, events: MonitorEvent = MonitorEvent.ALL) -> MonitorSocket: ...  # Raises: ConfigError
    def attach_discovery(self, discovery: Discovery) -> None: ...                # Raises: ConfigError

    # --- request (async) — no flags ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure; RequestError on request completion failure.
    async def request(self, peer_rid: RoutingId,
                      payload: Message | bytes | list,
                      *, timeout: int = 0) -> Received: ...

    # --- request (callback) — raises on submit failure ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure. Callback receives RequestResult;
    #   non-OK indicates request-completion failure (RequestError semantics).
    def request(self, peer_rid: RoutingId,
                payload: Message | bytes | list,
                callback: Callable[[RequestResult, Received | None], None],
                *, flags: int = 0, timeout: int = 0) -> None: ...

    # --- reply ---
    def reply(self, routing_id: RoutingId, request_seq: int,
              payload: Message | bytes | list, *, flags: int = 0) -> None: ...   # Raises: SubmitError

    # --- router → spot routed send ---
    def send_to_spot(self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
                     payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError

    # --- router → spot routed request (async) — no flags ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure; RequestError on request completion failure.
    async def request_to_spot(self, dest_node_rid: RoutingId,
                              dest_spot_rid: RoutingId,
                              payload: Message | bytes | list,
                              *, timeout: int = 0) -> Received: ...

    # --- router → spot routed request (callback) — raises on submit failure ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure. Callback receives RequestResult;
    #   non-OK indicates request-completion failure (RequestError semantics).
    def request_to_spot(self, dest_node_rid: RoutingId,
                        dest_spot_rid: RoutingId,
                        payload: Message | bytes | list,
                        callback: Callable[[RequestResult, Received | None], None],
                        *, flags: int = 0, timeout: int = 0) -> None: ...

    # --- router → spot routed reply ---
    def reply_to_spot(self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
                      request_seq: int,
                      payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError

    # --- router spot receive ---
    def recv_spot(self, *, flags: int = 0) -> Received: ...                      # Raises: RecvError
    def on_spot_receive(self, handler: Callable) -> None: ...                    # Raises: HandlerError

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
    def publish(self, topic: bytes | str, payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError
    def receive_subscription_event(self, *, flags: int = 0) -> SubscriptionEvent: ...  # Raises: RecvError
    def on_send_ready(self, handler: Callable) -> None: ...                      # Raises: HandlerError
    def monitor_open(self, events: MonitorEvent = MonitorEvent.ALL) -> MonitorSocket: ...  # Raises: ConfigError
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
    def subscribe(self, *, flags: int = 0) -> Subscribed: ...                    # Raises: RecvError
    def on_subscribe(self, handler: Callable[[Subscribed], None]) -> None: ...   # Raises: HandlerError
    def monitor_open(self, events: MonitorEvent = MonitorEvent.ALL) -> MonitorSocket: ...  # Raises: ConfigError
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
    def send(self, routing_id: RoutingId, payload: Message | bytes | list[Message], *, flags: int = 0) -> None: ...  # Raises: SubmitError
    def recv(self, *, flags: int = 0) -> Received: ...                           # Raises: RecvError
    def on_receive(self, handler: Callable[[Received], None]) -> None: ...       # Raises: HandlerError
    def on_send_ready(self, handler: Callable) -> None: ...                      # Raises: HandlerError
    def monitor_open(self, events: MonitorEvent = MonitorEvent.ALL) -> MonitorSocket: ...  # Raises: ConfigError
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
    def copy_from(cls, data: bytes | bytearray) -> Message: ...              # Raises: ConfigError
    @classmethod
    def wrap_buffer(cls, data: bytes | bytearray) -> Message: ...            # Raises: ConfigError
    @staticmethod
    def from_bytes(data: bytes) -> Message: ...                              # Raises: ConfigError
    def size(self) -> int: ...                                               # Raises: ConfigError
    @property
    def data(self) -> memoryview: ...                                        # Raises: ConfigError
    def to_bytes(self) -> bytes: ...                                         # Raises: ConfigError
    def getProperty(self, name: str) -> str | None: ...                      # Raises: ConfigError
    def refCount(self) -> int: ...                                           # Raises: ConfigError
    def send(self, socket) -> None: ...                                      # Raises: SubmitError
    def close(self) -> None: ...                                             # Raises: CloseError
    # supports `with` and `async with` — __exit__ raises CloseError
```

### RoutingId

```python
class RoutingId:
    def __init__(self, data: bytes | bytearray) -> None: ...
    def to_bytes(self) -> bytes: ...
    def __bytes__(self) -> bytes: ...
    def __len__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __eq__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...
```

### Received

```python
class Received:
    routing_id: RoutingId | None
    parts: tuple[ReceivedMessage, ...]
    def __iter__(self) -> Iterator: ...
    def __len__(self) -> int: ...
    def to_bytes_list(self) -> list[bytes]: ...
    def close(self) -> None: ...                 # Raises: CloseError
    # supports `with` and `async with` — __exit__ raises CloseError
```

### TopicMessage / Subscribed

```python
class TopicMessage:
    topic: bytes
    routing_id: RoutingId | None
    parts: tuple[ReceivedMessage, ...]
    def __iter__(self) -> Iterator: ...
    def __len__(self) -> int: ...
    def to_bytes_list(self) -> list[bytes]: ...
    def close(self) -> None: ...                 # Raises: CloseError

class Subscribed(TopicMessage):
    pass
```

### SubscriptionEvent

```python
class SubscriptionEvent:
    routing_id: RoutingId | None
    topic: bytes
    subscribed: bool
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

Result codes for handler registration operations (on_receive, on_subscribe, etc.).

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

Raised by handler-registration operations (`on_receive`,
`on_send_ready`, `on_subscribe`, `on_event`, `on_fire`, etc.).
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

```python
class MonitorSocket:
    def recv(self) -> SocketMonitorEvent: ...                                    # Raises: RecvError
    def on_event(self, handler: Callable[[SocketMonitorEvent], None]) -> None: ...  # Raises: HandlerError
    def snapshot(self) -> MonitorSnapshot: ...                                   # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### ServiceMonitor

```python
class ServiceMonitor:
    def recv(self) -> ServiceMonitorEvent: ...                                    # Raises: RecvError
    def on_event(self, handler: Callable[[ServiceMonitorEvent], None]) -> None: ...  # Raises: HandlerError
    def snapshot(self) -> MonitorSnapshot: ...                                    # Raises: ConfigError
    def close(self) -> None: ...                                                  # Raises: CloseError
```

### MonitorSnapshot

```python
class MonitorSnapshot:
    source_kind: int
    state_flags: int
    detail_flags: int
    snd_pending_msgs: int
    rcv_pending_msgs: int
```

### SocketMonitorEvent

```python
class SocketMonitorEvent:
    event: int
    value: int
    routing_id: RoutingId | None
    local_addr: str
    remote_addr: str
```

### ServiceMonitorEvent / ServiceEvent

```python
class ServiceMonitorEvent:
    service_kind: int
    event_type: int
    status: int
    error_code: int
    value: int
    detail_flags: int
    service_name: str
    endpoint: str
    routing_id: RoutingId | None
    subject: str
    subject_kind: int

ServiceEvent = ServiceMonitorEvent
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
    def set_tls_client(self, ca_cert: str | None, hostname: str | None,
                       trust_system: bool = False) -> None: ...                  # Raises: ConfigError
    def monitor_open(self, events: ServiceMonitorMask = ServiceMonitorMask.ALL
                     ) -> ServiceMonitor: ...                                    # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### SpotNode

```python
class SpotNode:
    def __init__(self, ctx: Context) -> None: ...
    def bind(self, endpoint: str) -> None: ...                                   # Raises: BindError
    def last_endpoint(self) -> str: ...                                          # Raises: ConfigError
    def connect_peer(self, endpoint: str) -> None: ...                           # Raises: ConnectError
    def disconnect_peer(self, endpoint: str) -> None: ...                        # Raises: ConnectError
    def attach_discovery(self, discovery: Discovery) -> None: ...                # Raises: ConfigError
    def set_routing_id(self, routing_id: RoutingId | bytes) -> None: ...         # Raises: ConfigError
    def get_routing_id(self) -> RoutingId | None: ...                            # Raises: ConfigError
    def set_tls_server(self, cert: str, key: str,
                       require_client_cert: bool = False) -> None: ...           # Raises: ConfigError
    def set_tls_client(self, ca_cert: str | None, hostname: str | None,
                       trust_system: bool = False) -> None: ...                  # Raises: ConfigError
    def wrap_handle(self) -> Spot: ...                                           # Raises: ConfigError
    def status_snapshot(self) -> SpotNodeStatus: ...                             # Raises: ConfigError
    def peers_snapshot(self) -> list[SpotNodePeerEntry]: ...                     # Raises: ConfigError
    def peers_query(self, filter_: SpotNodePeerFilter | None = None
                    ) -> list[SpotNodePeerEntry]: ...                            # Raises: ConfigError
    def subjects_snapshot(self, filter_: SpotNodeSubjectFilter | None = None
                          ) -> list[SpotNodeSubjectEntry]: ...                   # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
```

### Spot

```python
class Spot:
    def __init__(self, node: SpotNode) -> None: ...
    def publish(self, topic: bytes | str, payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError
    def set_subscription(self, topic: bytes | str) -> None: ...                  # Raises: ConfigError
    def unset_subscription(self, topic: bytes | str) -> None: ...                # Raises: ConfigError
    def subscribe(self, *, flags: int = 0) -> Subscribed: ...                    # Raises: RecvError
    def on_subscribe(self, handler: Callable[[Subscribed], None]) -> None: ...   # Raises: HandlerError
    def on_send_ready(self, handler: Callable[[Spot], None]) -> None: ...        # Raises: HandlerError

    # --- routed send (spot → spot) ---
    def send_to_spot(self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
                     payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError

    # --- routed request (spot → spot, async) — no flags ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure; RequestError on request completion failure.
    async def request_to_spot(self, dest_node_rid: RoutingId,
                              dest_spot_rid: RoutingId,
                              payload: Message | bytes | list,
                              *, timeout: int = 0) -> Received: ...

    # --- routed request (spot → spot, callback) — raises on submit failure ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure. Callback receives RequestResult;
    #   non-OK indicates request-completion failure (RequestError semantics).
    def request_to_spot(self, dest_node_rid: RoutingId,
                        dest_spot_rid: RoutingId,
                        payload: Message | bytes | list,
                        callback: Callable[[RequestResult, Received | None], None],
                        *, flags: int = 0, timeout: int = 0) -> None: ...

    # --- routed reply (spot → spot) ---
    def reply_to_spot(self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
                      request_seq: int,
                      payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError

    # --- routed send (spot → router) ---
    def send_to_router(self, peer_rid: RoutingId,
                       payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError

    # --- routed request (spot → router, async) — no flags ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure; RequestError on request completion failure.
    async def request_to_router(self, peer_rid: RoutingId,
                                payload: Message | bytes | list,
                                *, timeout: int = 0) -> Received: ...

    # --- routed request (spot → router, callback) — raises on submit failure ---
    # timeout = 0 uses the socket default timeout.
    # Raises: SubmitError on submit failure. Callback receives RequestResult;
    #   non-OK indicates request-completion failure (RequestError semantics).
    def request_to_router(self, peer_rid: RoutingId,
                          payload: Message | bytes | list,
                          callback: Callable[[RequestResult, Received | None], None],
                          *, flags: int = 0, timeout: int = 0) -> None: ...

    # --- routed reply (spot → router) ---
    def reply_to_router(self, peer_rid: RoutingId, request_seq: int,
                        payload: Message | bytes | list, *, flags: int = 0) -> None: ...  # Raises: SubmitError

    # --- routed receive ---
    def recv_routed(self, *, flags: int = 0) -> Received: ...                    # Raises: RecvError
    def on_routed_receive(self, handler: Callable) -> None: ...                  # Raises: HandlerError
    def on_dispatch_event(self, handler: Callable) -> None: ...                  # Raises: HandlerError

    def close(self) -> None: ...                                                 # Raises: CloseError
```

### RegistryQueryClient

```python
class RegistryQueryClient:
    def __init__(self, ctx: Context) -> None: ...
    def connect(self, endpoint: str) -> None: ...                                # Raises: ConnectError
    def snapshot(self, filter_: RegistryTopologyFilter | None = None
                 ) -> list[RegistryTopologyEntry]: ...                           # Raises: ConfigError
    def close(self) -> None: ...                                                 # Raises: CloseError
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
    def size(self) -> int: ...                                                   # Raises: ConfigError
    def poll(self, timeout_ms: int) -> list[dict]: ...                           # Raises: RecvError
    def close(self) -> None: ...                                                 # Raises: CloseError
    # supports `with` and `async with` — __exit__ raises CloseError
```

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

def errno() -> int:
    """Return the errno for the current thread."""
    ...

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
