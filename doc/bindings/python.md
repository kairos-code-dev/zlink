[English](python.md) | [한국어](python.ko.md)

# Python Binding

## 1. Overview

- Native calls based on **ctypes/CFFI**
- Wheel distribution support
- Python 3.9+

## 2. Installation

```bash
pip install zlink
```

## 3. Canonical Example

```python
import zlink

with zlink.Context() as ctx:
    with zlink.Socket(ctx, zlink.SocketType.PAIR) as server:
        with zlink.Socket(ctx, zlink.SocketType.PAIR) as client:
            server.bind("inproc://example-pair")
            client.connect("inproc://example-pair")

            client.send(b"hello")
            with server.recv_message() as received:
                print(received.to_bytes().decode("utf-8"))
```

## 4. Main Modules

| Module | Description |
|--------|-------------|
| `_core.py` | Context, Socket, Message |
| `_poller.py` | Poller |
| `_monitor.py` | Socket/service monitor wrappers |
| `_discovery.py` | Registry, Discovery |
| `_spot.py` | SpotNode, Spot |
| `_ffi.py` | FFI binding definitions |
| `_native.py` | Native library loader |

## 5. Current Surface

```python
with zlink.Registry(ctx) as registry:
    registry.bind("tcp://127.0.0.1:5550", "tcp://127.0.0.1:5551")

with zlink.Discovery(ctx, zlink.ServiceType.SOCKET, "orders") as discovery:
    discovery.connect_registry("tcp://127.0.0.1:5551")

with zlink.SpotNode(ctx) as node:
    node.bind("tcp://127.0.0.1:6000")

with zlink.Spot(node) as spot:
    spot.subscribe(b"room:lobby")
```

Canonical receive APIs:

- `Socket.recv_message()`
- `Socket.recv_multipart()`
- `Socket.recv_into()`
- `Socket.recv_topic_message()`
- `Spot.recv()`

Canonical callback APIs:

- `Socket.set_recv_handler()`
- `Socket.set_subscribe_handler()`
- `Socket.set_send_ready_handler()`
- `Spot.set_handler()`
- `Spot.set_send_ready_handler()`

Canonical send/configuration APIs:

- `Socket.send()` / `send_multipart()` / `publish()`
- `Socket.set_option()` and family helpers (`set_pub_option()`, `set_sub_option()`, ...)
- `Socket.set_routing_id()`, `subscribe()`, `unsubscribe()`
- `Registry.bind()`
- `Discovery(ctx, service_type, service_name)`
- `Spot(node_or_ctx)`

Examples live under `bindings/python/examples/`:

- `pair_recv.py`
- `pair_callback.py`
- `pubsub_recv.py`
- `pubsub_callback.py`
- `dealer_router_recv.py`
- `dealer_router_callback.py`
- `stream_recv.py`
- `stream_callback.py`
- `spot_recv.py`
- `spot_callback.py`

## 6. Native Libraries

Platform-specific binaries are included in the `src/zlink/native/` directory.

## 7. Testing

```bash
cd bindings/python && python -m pytest -q
cd bindings/python && python -m pytest -q tests/integration
cd bindings/python && python -m pytest -q tests/test_bench_fastpath.py
```

## 8. Benchmark Fast Path

`bindings/python/perf/single` supports an optional C-extension fast path (`_zlink_fastpath`).

- Default: enabled for benchmarks (`BENCH_PY_FASTPATH_CEXT=1`).
- Disable: `BENCH_PY_FASTPATH_CEXT=0`
- Require hard-fail on load/build issues: `BENCH_PY_FASTPATH_REQUIRE=1`

Manual build:

```bash
cd bindings/python/perf/single
python3 setup_fastpath.py build_ext --inplace
```

## 9. Migration Notes

- `Socket.recv(size)` is legacy. Use `recv_message()`, `recv_multipart()`, or
  `recv_into()`.
- `Socket.setsockopt()` / `getsockopt()` are removed. Use `set_option()`,
  `get_option()`, `set_routing_id()`, `subscribe()`, and option-family helpers.
- Direct callback mode is explicit. Use `set_recv_handler()` /
  `set_subscribe_handler()` / `set_send_ready_handler()` and do not mix the same
  handle with `recv_*()` or data-plane poller registration for the same
  direction.
- `Receiver` and split SPOT child handles are not part of the current Python
  public surface.
