[English](python.md) | [한국어](python.ko.md)

# Python 바인딩

## 1. 개요

- **ctypes/CFFI** 기반 네이티브 호출
- wheel 배포 지원
- Python 3.9+

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

## 8. 벤치마크 Fast Path

`bindings/python/perf/single`은 선택적으로 C-extension fast path(`_zlink_fastpath`)를 사용합니다.

- 기본값: 벤치마크에서 활성화 (`BENCH_PY_FASTPATH_CEXT=1`)
- 비활성화: `BENCH_PY_FASTPATH_CEXT=0`
- 로드/빌드 실패 시 즉시 실패: `BENCH_PY_FASTPATH_REQUIRE=1`

수동 빌드:

```bash
cd bindings/python/perf/single
python3 setup_fastpath.py build_ext --inplace
```

## 9. STREAM 콜백 API

`Socket` STREAM 헬퍼:
- `stream_attach(handler, mode=StreamDispatchMode.NONE)`
- `stream_detach()`
- `stream_peer_routing_id(index=0)`
- `stream_send(routing_id, payload, flags=0)`

모드 규칙:
- attach 상태에서는 콜백에서 STREAM 페이로드를 소비합니다.
- attach 상태에서 STREAM 페이로드 수신에 `recv()`를 혼용하지 않습니다.
- `stream_detach()` 이후에는 기존 `recv()` 경로를 다시 사용할 수 있습니다.

```python
def on_packets(routing_id, payload):
    copy = bytes(payload)            # echo 전 명시적 복사
    stream.stream_send(routing_id, copy)
    return 0

stream.stream_attach(on_packets, zlink.StreamDispatchMode.LEN32BE)
```
