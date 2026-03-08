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

## 3. Basic Example

```python
import zlink

ctx = zlink.Context()

server = ctx.socket(zlink.PAIR)
server.bind("tcp://*:5555")

client = ctx.socket(zlink.PAIR)
client.connect("tcp://127.0.0.1:5555")

client.send(b"Hello")

reply = server.recv()
print(reply.decode())

client.close()
server.close()
ctx.close()
```

## 4. Main Modules

| Module | Description |
|--------|-------------|
| `_core.py` | Context, Socket, Message |
| `_poller.py` | Poller |
| `_monitor.py` | Monitor |
| `_discovery.py` | Discovery, Gateway, Receiver |
| `_spot.py` | SpotNode, Spot |
| `_ffi.py` | FFI binding definitions |
| `_native.py` | Native library loader |

## 5. Discovery/Gateway Example

```python
discovery = zlink.Discovery(ctx)
# registry bootstrap/control endpoint
discovery.connect_registry("tcp://registry:5550")
discovery.subscribe("payment-service")

gateway = zlink.Gateway(ctx, discovery)
gateway.send("payment-service", b"request data")
reply = gateway.recv()
```

## 6. Native Libraries

Platform-specific binaries are included in the `src/zlink/native/` directory.

## 7. Testing

```bash
cd bindings/python && python -m pytest tests/
```

Uses the unittest framework.

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

## 9. STREAM Callback API

`Socket` STREAM helpers:
- `stream_attach(handler, mode=StreamDispatchMode.NONE)`
- `stream_detach()`
- `stream_peer_routing_id(index=0)`
- `stream_send(routing_id, payload, flags=0)`

Mode rules:
- While attached, consume STREAM payloads in the callback.
- Do not mix `recv()` for STREAM payload consumption while attached.
- After `stream_detach()`, normal `recv()` use is available again.

```python
def on_packets(routing_id, payload):
    copy = bytes(payload)            # explicit copy before echo
    stream.stream_send(routing_id, copy)
    return 0

stream.stream_attach(on_packets, zlink.StreamDispatchMode.LEN32BE)
```
