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
