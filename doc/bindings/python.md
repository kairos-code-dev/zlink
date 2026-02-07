# Python 바인딩

## 1. 개요

- **ctypes/CFFI** 기반 네이티브 호출
- wheel 배포 지원
- Python 3.8+

## 2. 설치

```bash
pip install zlink
```

## 3. 기본 예제

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

## 4. 주요 모듈

| 모듈 | 설명 |
|------|------|
| `_core.py` | Context, Socket, Message |
| `_poller.py` | Poller |
| `_monitor.py` | Monitor |
| `_discovery.py` | Discovery, Gateway, Receiver |
| `_spot.py` | SpotNode, Spot |
| `_ffi.py` | FFI 바인딩 정의 |
| `_native.py` | 네이티브 라이브러리 로더 |

## 5. Discovery/Gateway 예시

```python
discovery = zlink.Discovery(ctx)
discovery.connect_registry("tcp://registry:5550")
discovery.subscribe("payment-service")

gateway = zlink.Gateway(ctx, discovery)
gateway.send("payment-service", b"request data")
reply = gateway.recv()
```

## 6. 네이티브 라이브러리

`src/zlink/native/` 디렉토리에 플랫폼별 바이너리 포함.

## 7. 테스트

```bash
cd bindings/python && python -m pytest tests/
```

unittest 프레임워크 사용.
