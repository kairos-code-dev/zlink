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

## 3. canonical 예제

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

## 4. 주요 모듈

| 모듈 | 설명 |
|------|------|
| `_core.py` | Context, Socket, Message |
| `_poller.py` | Poller |
| `_monitor.py` | socket/service monitor wrapper |
| `_discovery.py` | Registry, Discovery |
| `_spot.py` | SpotNode, Spot |
| `_ffi.py` | FFI 바인딩 정의 |
| `_native.py` | 네이티브 라이브러리 로더 |

## 5. 현재 표면

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

canonical 수신 API:

- `Socket.recv_message()`
- `Socket.recv_multipart()`
- `Socket.recv_into()`
- `Socket.recv_topic_message()`
- `Spot.recv()`

canonical callback API:

- `Socket.set_recv_handler()`
- `Socket.set_subscribe_handler()`
- `Socket.set_send_ready_handler()`
- `Spot.set_handler()`
- `Spot.set_send_ready_handler()`

canonical 송신/설정 API:

- `Socket.send()` / `send_multipart()` / `publish()`
- `Socket.set_option()`과 family helper (`set_pub_option()`, `set_sub_option()`, ...)
- `Socket.set_routing_id()`, `subscribe()`, `unsubscribe()`
- `Registry.bind()`
- `Discovery(ctx, service_type, service_name)`
- `Spot(node_or_ctx)`

예제는 `bindings/python/examples/` 아래에 둡니다.

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

## 6. 네이티브 라이브러리

`src/zlink/native/` 디렉토리에 플랫폼별 바이너리 포함.

## 7. 테스트

```bash
cd bindings/python && python -m pytest -q
cd bindings/python && python -m pytest -q tests/integration
cd bindings/python && python -m pytest -q tests/test_bench_fastpath.py
```

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

## 9. 마이그레이션 메모

- `Socket.recv(size)`는 레거시입니다. `recv_message()`, `recv_multipart()`,
  `recv_into()`를 사용합니다.
- `Socket.setsockopt()` / `getsockopt()`는 제거됐습니다. `set_option()`,
  `get_option()`, `set_routing_id()`, `subscribe()`, option-family helper를
  사용합니다.
- direct callback mode는 명시적으로 사용합니다.
  `set_recv_handler()` / `set_subscribe_handler()` /
  `set_send_ready_handler()`를 쓰고, 같은 handle에서 같은 방향의 `recv_*()` 또는
  data-plane poller 등록과 섞지 않습니다.
- `Receiver`와 split SPOT child handle은 현재 Python public surface에 포함되지
  않습니다.
