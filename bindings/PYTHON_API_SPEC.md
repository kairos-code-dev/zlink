# Python Binding API Spec

> **적용 그룹**: recv + callback
> **정책 기준**: [`bindings/README.md`](README.md)

---

## 1. Native Bridge 전략

### 1.1 현재 상태 및 문제점

| 항목 | 현재 구현 | 문제 |
|------|-----------|------|
| FFI | ctypes (순수 Python) | 호출 오버헤드 높음, ABI 레벨 최적화 불가 |
| 콜백 브릿징 | `CFUNCTYPE` → C I/O 스레드에서 직접 호출 | asyncio 미통합, 스레드 안전 문제 |
| async | 없음 | asyncio 지원 없음 — 현대 Python에서 비관용적 |
| GIL | ctypes 자동 release (비최적) | blocking 호출 최적화 없음 |
| 메모리 | 매 recv마다 bytes 복사 | zero-copy 경로 없음 |
| 리소스 | 수동 close() | context manager 일관성 부족 |

### 1.2 목표 아키텍처

```
┌────────────────────────────────────────────────┐
│  Python (asyncio event loop thread)            │
│  ┌──────────────────┐  ┌────────────────────┐  │
│  │ await recv()     │  │ async for msg in   │  │
│  │  → Future        │  │   socket:          │  │
│  └───────┬──────────┘  └─────────┬──────────┘  │
│          │                       │             │
│    ┌─────▼───────────────────────▼──────┐      │
│    │  loop.add_reader(fd, _on_readable) │      │
│    └──────────────┬─────────────────────┘      │
├─────��─────────────┼────────────────────────────┤
│  cffi (C layer)   │                            │
│    ┌──────────────▼──────────────────┐         │
│    │  zlink_recv() DONTWAIT          │         │
│    │  → ffi.buffer() → memoryview    │         │
│    └─────────────────────────────────┘         │
└────────────────────────────────────────────────┘
```

### 1.3 FFI 메커니즘: ctypes → cffi 마이그레이션

**cffi out-of-line ABI mode**를 목표로 한다.

| 비교 | ctypes | cffi | pybind11 |
|------|--------|------|----------|
| 호출 오버헤드 | ~2μs | ~0.3μs | ~0.2μs |
| 빌드 필요 | 아니오 | 아니오 (ABI mode) | 예 (C++ 컴파일러) |
| GIL 제어 | 자동 release | `nogil=True` 명시 가능 | 명시적 `py::gil_scoped_release` |
| zero-copy | `ctypes.cast` (번거로움) | `ffi.buffer()` → memoryview | 직접 |
| 콜백 | `CFUNCTYPE` (GC 위험) | `@ffi.def_extern()` (안전) | C++ lambda |
| 배포 | 순수 Python | 순수 Python (ABI mode) | wheel 빌드 필요 |

cffi를 선택하는 이유:
- ctypes 대비 ~6x 호출 오버헤드 감소
- `ffi.buffer()` → `memoryview`로 zero-copy 수신 가능
- `@ffi.def_extern()` 콜백은 GC에 안전
- ABI mode는 C 컴파일러 불필요, pip install만으로 배포 가능
- pybind11은 C++ 컴파일러 필요하고 wheel 빌드 복잡도 증가

### 1.4 콜백 브릿징

#### 고빈도 데이터 경로 (recv, subscribe): fd-driven

```python
fd = lib.zlink_get_option_fd(socket_handle)  # ZLINK_OPT_FD
loop.add_reader(fd, self._on_readable)

def _on_readable(self):
    while True:
        rc = lib.zlink_recv(self._handle, ..., ZLINK_DONTWAIT)
        if rc == -1:  # EAGAIN
            break
        received = self._wrap_received(...)
        self._pending_future.set_result(received)
```

- asyncio 이벤트 루프에 직접 등록
- fd readable 시 non-blocking recv drain
- Future/async iterator에 결과 전달

#### C 콜백 경로 (on_receive, on_subscribe, on_send_ready)

```python
@ffi.def_extern()
def _recv_callback(source_rid, parts, part_count, userdata):
    socket = ffi.from_handle(userdata)
    received = socket._wrap_callback_received(source_rid, parts, part_count)
    # I/O thread → event loop thread 안전 전달
    socket._loop.call_soon_threadsafe(socket._deliver_callback, received)
```

- `@ffi.def_extern()`: GC에 안전한 콜백 정의
- `loop.call_soon_threadsafe()`: I/O 스레드 → asyncio 이벤트 루프 전달
- 콜백 내에서 메시지를 복사 후 전달 (콜백 반환 시 C가 해제하므로)

#### 저빈도 이벤트 (monitor): call_soon_threadsafe

모니터/서비스 이벤트도 동일한 `call_soon_threadsafe` 패턴.

### 1.5 메모리 및 소유권

#### Recv (C → Python, zero-copy)

```python
msg_data = lib.zlink_msg_data(msg_ptr)
msg_size = lib.zlink_msg_size(msg_ptr)
view = ffi.buffer(msg_data, msg_size)  # memoryview, 복사 없음
```

- `ffi.buffer()` → Python `memoryview` (zero-copy)
- `Message` 객체가 `zlink_msg_t`를 소유하고 `__del__`에서 `zlink_msg_close` 호출
- `msg.bytes()` 호출 시에만 명시적 복사 발생

#### Send (Python → C, zero-copy)

```python
buf = ffi.from_buffer(data)  # bytes/bytearray/memoryview → C pointer
lib.zlink_msg_init_data(msg, buf, len(data), ffi.NULL, ffi.NULL)
```

- `ffi.from_buffer()`: Python 버퍼 → C 포인터 (복사 없음)
- send 성공: 소유권 C로 이동
- 주의: Python 객체가 GC되지 않도록 참조 유지 필요

#### 소유권 계약

| 경로 | 소유권 | 해제 |
|------|--------|------|
| send 성공 | native로 이동, Python 접근 금지 | native가 해제 |
| send 실패 | caller 유지 | caller가 close |
| recv 성공 | Python으로 이동 | `Message.close()` 또는 `__del__` |
| callback delivery | 콜백 내에서만 유효 | 콜백 반환 시 native 해제 |

### 1.6 이벤트 루프 통합

```
ZLINK_OPT_FD → socket의 signaling fd

asyncio:
  loop.add_reader(fd, _on_readable)  → readable 시 zlink_recv drain
  loop.add_writer(fd, _on_writable)  → writable 시 zlink_send flush
  loop.remove_reader(fd)             → 정리
```

- edge-triggered: 한 번의 wake에서 EAGAIN까지 모두 drain
- GIL: cffi ABI mode에서 C 호출 시 자동 GIL release

---

## 2. API 설계 원칙

### 2.1 소켓 타입 계층 전략

- 소켓 타입별 concrete class를 제공한다 (`PairSocket`, `DealerSocket`, `RouterSocket`,
  `PubSocket`, `SubSocket`, `XPubSocket`, `XSubSocket`, `StreamSocket`).
- 각 클래스는 `bindings/README.md` Socket Capability Matrix의 `Y` 항목만 public으로
  노출한다. `—` 항목은 해당 클래스에 존재하지 않아야 한다.
- 공통 lifecycle (`bind`, `unbind`, `close`, `monitor_open`, TLS, common options)은
  base class로 올린다.
- `connect`/`disconnect`는 Stream을 제외한 소켓만 사용 가능한 별도 계층으로 분리한다.
- send/recv/publish/subscribe는 capability에 따라 concrete class에만 존재한다.
  generic base에 올리지 않는다.

### 2.2 Sync / Async 이중 API

- **Sync 클래스** (`Context`, `PairSocket`, `Poller` 등): threading/blocking 용도.
  `__enter__`/`__exit__` context manager, `__iter__` iterator 지원.
- **Async 클래스** (`AsyncContext`, `AsyncPairSocket`, `AsyncPoller` 등): asyncio 용도.
  `__aenter__`/`__aexit__`, `__aiter__`/`__anext__` 지원.
- 두 클래스는 동일한 capability를 노출하되, blocking 메서드는 sync 클래스에,
  awaitable 메서드는 async 클래스에 둔다.
- `try_*` 메서드는 양쪽 모두 동일 (non-blocking이므로 async 불필요).

**Naming:**
- **blocking API**: 기본 동사 이름 (`send`, `recv`, `publish`, `subscribe`).
  Sync에서는 blocking, Async에서는 awaitable.
- **non-blocking API**: `try_` 접두사 (`try_send`, `try_recv`, `try_publish`,
  `try_subscribe`).
- blocking/non-blocking을 public `flags` 파라미터로 전환하지 않는다.
- Python snake_case. canonical name의 케이싱 변형만 허용.
- keyword/optional parameter로 구분 가능하므로 파라미터 인코딩 접미사 금지.
  - `send(message)` / `send(routing_id, message)` — keyword로 구분.
- callback: `on_receive`, `on_subscribe`, `on_send_ready` (canonical).

### 2.3 Domain Object 전략

- `bindings/README.md` Domain Object Policy에 따라 최소 핵심 domain model을 제공한다:
  `Message`, `RoutingId`, `Received`, `Subscribed`, `SubscriptionEvent`, `SendResult`.
- `Received`: `routing_id: RoutingId | None` + `parts: tuple[Message, ...]`.
  `@dataclass(frozen=True, slots=True)`.
  callback delivery와 direct recv의 payload shape가 동일해야 한다.
- `SendResult`: `IntEnum` — `SENT`, `BACKPRESSURED`, `NOT_READY`.
  non-blocking send는 `bool`이 아니라 explicit outcome.
- `Message`: `zlink_msg_t` wrapper. `data` property → `memoryview` (zero-copy,
  `ffi.buffer()` 기반). `bytes()` 메서드는 명시적 복사.
  `close()`, `__enter__`/`__exit__`, `__del__` (safety net).
- `RoutingId`: 생성 시 255바이트 초과 `ValueError`. 불변 값 객체.
- 서비스 계층 domain object도 동일 원칙:
  `SpotNodeStatus`, `SpotNodePeerEntry`, `MemberPeerEntry`, `ServiceEvent`,
  `MonitorEvent`, `MonitorSnapshot` 등.

### 2.4 Option Facade 전략

- raw `set_option`/`get_option` bag은 public에 노출하지 않는다.
- `CommonSocketOptions`: 전 소켓 공통 typed property (linger, HWM, timeout,
  heartbeat, TCP keepalive 등)
- 소켓 타입별 option facade: `RouterSocketOptions`, `DealerSocketOptions`,
  `StreamSocketOptions`, `PubSocketOptions`, `SubSocketOptions`
- option 값 타입: `bool`, `int` (ms 단위), `RoutingId`, `str`.
  raw int 금지.

### 2.5 리소스 관리

- 전 타입 `__enter__`/`__exit__` (sync) 및 `__aenter__`/`__aexit__` (async) 지원.
- `Message`는 `__enter__`/`__exit__` + `__del__` (safety net).
- context manager 사용을 권장 경로로 한다. 수동 `close()`도 가능.

### 2.6 Error 전략

- blocking send/publish 실패: `raise ZlinkError`.
- non-blocking send/publish: `BACKPRESSURED`/`NOT_READY`는 정상 `SendResult` 반환.
  그 외 오류는 `raise ZlinkError`.
- non-blocking recv: `None` 반환 (EAGAIN). 그 외 오류는 `raise ZlinkError`.
- 바인딩 입력 검증 (RoutingId 길이, endpoint 길이, null 검사): 즉시
  `ValueError`/`TypeError`.
- native 상태 오류 (EFSM, ETERM 등): `raise ZlinkError`로 surface.
  managed layer가 임의 추론하지 않는다.

### 2.7 Capability 범위

소켓 계층, 서비스 계층(Spot, SpotNode, Discovery, Registry), 모니터(SocketMonitor,
ServiceMonitor), Poller 모두 `bindings/README.md`의 해당 Capability Matrix를 따른다.
구체적 메서드 목록은 Matrix를 참조한다.

---

## 3. Async API 설계

### 3.1 패턴 개요

| 패턴 | 용도 | 구현 |
|------|------|------|
| `await recv()` | 단발 수신 | `add_reader(fd)` → `try_recv()` → Future resolve |
| `async for msg in socket` | 스트림 수신 | `AsyncIterator` + `add_reader` |
| `await send(msg)` | 비동기 송신 | `add_writer(fd)` → `try_send()` → Future resolve |
| `await poller.poll()` | 비동기 폴링 | `add_reader` on poller fd |
| `on_receive(handler)` | C 콜백 직결 | `call_soon_threadsafe` |

### 3.2 스트림 수신 (AsyncIterator)

```python
# Pair, Dealer, Router, Stream
async with AsyncContext() as ctx:
    dealer = AsyncDealerSocket(ctx)
    dealer.connect("tcp://localhost:5555")

    async for received in dealer:
        payload = received.parts[0].bytes()
        # process

# Sub, XSub
async with AsyncContext() as ctx:
    sub = AsyncSubSocket(ctx)
    sub.connect("tcp://localhost:5555")
    sub.set_subscription("market.")

    async for subscribed in sub:
        print(f"[{subscribed.topic}] {subscribed.parts[0].bytes()}")

# Monitor
async with socket.monitor_open() as monitor:
    async for event in monitor:
        print(f"{event.type.name}: {event.remote_address}")
```

내부 구현:

```python
class AsyncPairSocket:
    async def __anext__(self) -> Received:
        while True:
            result = self.try_recv()
            if result is not None:
                return result
            # fd not readable yet, wait
            await self._wait_readable()

    async def _wait_readable(self) -> None:
        future = self._loop.create_future()
        self._loop.add_reader(self._fd, future.set_result, None)
        try:
            await future
        finally:
            self._loop.remove_reader(self._fd)
```

### 3.3 비동기 송신

```python
await socket.send(b"hello")

# Router — routed send
await router.send(routing_id, b"response")
```

내부: `add_writer(fd)` → writable 시 `try_send()` → Sent면 resolve,
Backpressured면 다시 대기.

### 3.4 리소스 관리

```python
# context manager (권���)
async with AsyncContext() as ctx:
    async with AsyncDealerSocket(ctx) as socket:
        socket.connect("tcp://localhost:5555")
        async for msg in socket:
            ...
# 스코프 끝에서 자동 close

# sync
with Context() as ctx:
    with DealerSocket(ctx) as socket:
        socket.connect("tcp://localhost:5555")
        for msg in socket:
            ...
```

---

## 4. 성능 최적화 전략

### 4.1 Zero-Copy 경로

| 경로 | 방법 | 복사 횟수 |
|------|------|-----------|
| recv → memoryview | `ffi.buffer(zlink_msg_data(msg), size)` | 0 |
| recv → bytes | `Message.bytes()` (명시적 요청 시) | 1 |
| bytes → send | `ffi.from_buffer(data)` + `zlink_msg_init_data` | 0 |
| str → send | `str.encode()` + zero-copy send | 1 (인코딩) |

### 4.2 버퍼 관리

- recv: `Message.data`는 `memoryview` (zero-copy). `Message.close()` 전까지 유효
- send: `ffi.from_buffer()`로 Python 버퍼 포인터 직접 사용
- multipart: cffi 측에서 `zlink_msg_t[]` 관리, Python에는 `tuple[Message, ...]` 노출

### 4.3 Sync vs Async 오버헤드

| 경로 | 오버헤드 | 용도 |
|------|----------|------|
| `try_recv()` (sync) | cffi 호출 1회 (~0.3μs) | perf, threading |
| `await recv()` (async) | `add_reader` wake + cffi (~10μs) | asyncio 앱 |
| `on_receive()` (callback) | `call_soon_threadsafe` (~50–200μs) | 앱 콜백 패턴 |

### 4.4 수신 방식 트레이드오프 비교

| 방식 | 지연 | 처리량 | 복잡도 | 용도 |
|------|------|--------|--------|------|
| `add_reader` + `try_recv` | ~10μs | 최고 | 중간 | async API 내부, 앱 |
| `call_soon_threadsafe` callback | ~50–200μs | 중간 | 낮음 | 앱 콜백 API |
| `threading.Event` 폴링 | ~1–10ms | 낮음 | 최저 | **제거 대상** |
| blocking `recv()` (GIL release) | 0 (대기 중) | 높음 | 최저 | threading, perf |

### 4.5 FFI 오버헤드 비교

| FFI | 단순 호출 | 콜백 | zero-copy |
|-----|-----------|------|-----------|
| ctypes | ~2μs | CFUNCTYPE (GC 위험) | 번거로움 |
| cffi ABI | ~0.3μs | `@ffi.def_extern` (안전) | `ffi.buffer()` |
| pybind11 | ~0.2μs | C++ lambda | 직접 |

cffi ABI mode가 빌드 복잡도 없이 ctypes 대비 ~6x 오버헤드 감소.

---

---

## 5. 기존 구현 개선 가이드

### 5.1 현재 구현 문제점

| 문제 | 영향 |
|------|------|
| ctypes FFI | 호출 오버헤드 ~2μs, hot path에서 누적 |
| asyncio 미지원 | 현대 Python 앱에서 사용 불가 |
| C I/O 스레드 직접 콜백 | 스레드 안전 문제, asyncio 비호환 |
| CFUNCTYPE GC 위험 | 참조 수동 유지 필요 (`self._recv_handler_cb`) |
| 매 recv bytes 복사 | 대용량 메시지 처리량 저하 |
| context manager 불완전 | 리소스 누수 위험 |
| 타입 힌트 미흡 | IDE/mypy 지원 부족 |

### 5.2 개선 항목 및 우선순위

| 우선순위 | 항목 | 현재 | 목표 | 예상 효과 |
|----------|------|------|------|-----------|
| **P0** | ctypes → cffi | ctypes (~2μs/call) | cffi ABI (~0.3μs/call) | 호출 오버헤드 ~6x 감소 |
| **P0** | asyncio 통합 | 없음 | `add_reader(fd)` + async/await | 현대 Python 사용 가능 |
| **P0** | 콜백 디스패치 | C I/O 스레드 직접 | `call_soon_threadsafe` | 스레드 안전, asyncio 호환 |
| **P1** | zero-copy recv | bytes 복사 | `ffi.buffer()` → memoryview | 대용량 ~2x 처리량 |
| **P1** | zero-copy send | bytes 복사 | `ffi.from_buffer()` | 대용량 ~2x 처리량 |
| **P1** | context manager | 부분적 | 전 타입 `__enter__`/`__aenter__` | 리소스 안전 |
| **P1** | Sync + Async 이중 API | blocking만 | Sync 클래스 + Async 클래스 | 모든 사용 패턴 지원 |
| **P2** | 완전 타입 힌트 | 부분 | py.typed + 전체 annotation | IDE/mypy 지원 |
| **P2** | capability matrix 정렬 | 미검증 | surface test 포함 | 정책 준수 검증 |
| **P2** | `@ffi.def_extern` 콜백 | `CFUNCTYPE` (GC 위험) | cffi extern callback | GC 안전 |
