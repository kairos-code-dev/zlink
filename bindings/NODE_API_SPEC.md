# Node.js Binding API Spec

> **적용 그룹**: recv + callback
> **정책 기준**: [`bindings/README.md`](README.md)

---

## 1. Native Bridge 전략

### 1.1 현재 상태 및 문제점

| 항목 | 현재 구현 | 문제 |
|------|-----------|------|
| FFI | N-API C++ addon (node-gyp) | 유지 — 적절한 선택 |
| 콜백 브릿징 | `napi_threadsafe_function` 8 슬롯 고정 | 동시 소켓 수 8개 제한, 슬롯 부족 시 실패 |
| async recv | `setImmediate` 폴링 루프 | 1–4ms 지연, CPU 낭비, 비관용적 |
| 메시지 전달 | Buffer 복사 | zero-copy 경로 없음 |
| TypeScript | 부분 타입 | 제네릭 소켓 타입 미지원 |

### 1.2 목표 아키텍처

```
┌──────────────────────────────────────────────┐
│  TypeScript (event loop thread)              │
│  ┌──────────────┐  ┌───────────────────┐     │
│  │ await recv() │  │ for await (msg of │     │
│  │  → Promise   │  │   socket) { ... } │     │
│  └──────┬───────┘  └────────┬──────────┘     │
│         │                   │                │
│    ┌────▼───────────────────▼─────┐          │
│    │  uv_poll_t (POLLIN/POLLOUT)  │          │
│    │  registered on zlink fd      │          │
│    └──────────────┬───────────────┘          │
├───────────────────┼──────────────────────────┤
│  N-API C++ addon  │                          │
│    ┌──────────────▼──────────────────┐       │
│    │  zlink_recv() DONTWAIT          │       │
│    │  → zlink_msg_t* (zero-copy)     │       │
│    └─────────────────────────────────┘       │
└──────────────────────────────────────────────┘
```

### 1.3 FFI 메커니즘

- **N-API** (node-addon-api) 유지. Node.js ABI 안정 레이어로 최적 선택.
- 빌드: `node-gyp` + `binding.gyp`
- 타겟: Node.js 22+ (ES2023, `using` 지원)

### 1.4 콜백 브릿징

#### 고빈도 데이터 경로 (recv, subscribe): fd-driven

소켓 fd를 `uv_poll_t`로 이벤트 루프에 등록한다. readable 이벤트 시
`zlink_recv()` DONTWAIT로 메시지를 drain하고 Promise를 resolve한다.

```
zlink_get_option(socket, ZLINK_OPT_FD) → fd
uv_poll_init(loop, &handle, fd)
uv_poll_start(&handle, UV_READABLE, on_readable)

on_readable:
  while (zlink_recv(socket, ..., ZLINK_DONTWAIT) == 0)
    → napi_resolve_deferred(promise, received)
```

- TSFN 슬롯 제한 없음
- 이벤트 루프 네이티브 통합으로 ~5μs wake latency
- C 콜백(`zlink_recv_handler`) 대비 fd-driven이 Node.js에서 더 자연스러움

#### C 콜백 경로 (onReceive, onSubscribe, onSendReady)

앱 API로 C 콜백도 제공한다. TSFN을 동적 할당으로 전환한다.

```
C I/O thread callback
  → napi_call_threadsafe_function(tsfn, data, napi_tsfn_nonblocking)
  → event loop에서 JS handler 호출
```

- 슬롯 고정 배열 → 소켓별 동적 TSFN 할당
- TSFN 생성/해제는 소켓 lifecycle에 바인딩

#### 저빈도 이벤트 (monitor): TSFN

모니터 이벤트는 빈도가 낮으므로 TSFN이 적합하다.

### 1.5 메모리 및 소유권

#### Recv (C → JS, zero-copy)

```cpp
// C++ addon 내부
napi_create_external_buffer(env, zlink_msg_data(msg), zlink_msg_size(msg),
                            release_msg_callback, msg_ptr, &buffer);
```

- `zlink_msg_data()` 포인터를 직접 JS Buffer로 노출
- GC finalize 콜백에서 `zlink_msg_close()` 호출
- 복사 없음. JS에서 Buffer를 읽기만 하면 zero-copy 완성

#### Send (JS → C, zero-copy)

```cpp
// JS Buffer의 데이터 포인터를 직접 사용
napi_get_buffer_info(env, js_buffer, &data, &length);
zlink_msg_init_data(&msg, data, length, release_ref_callback, ref);
```

- `napi_create_reference`로 JS Buffer를 GC에서 보호
- send 완료 후 `zlink_free_fn` 콜백에서 참조 해제
- Buffer/TypedArray/ArrayBuffer 모두 지원

#### Multipart

- `zlink_msg_t` 배열을 C++ 측에서 관리
- JS에는 `Received` 객체의 `parts: Buffer[]`로 노출
- 각 part가 개별 external buffer (lazy copy 없음)

#### 소유권 계약

| 경로 | 소유권 | 해제 |
|------|--------|------|
| send 성공 | native로 이동, JS 접근 금지 | native가 해제 |
| send 실패 | caller 유지 | caller가 close |
| recv 성공 | JS로 이동 | GC finalize에서 `zlink_msg_close` |
| callback delivery | 콜백 내에서만 유효 | 콜백 반환 시 native 해제 |

### 1.6 이벤트 루프 통합

```
ZLINK_OPT_FD → socket의 signaling fd
uv_poll_t    → libuv 이벤트 루프에 fd 등록
ZLINK_OPT_EVENTS → POLLIN/POLLOUT edge 확인
```

- `uv_poll_start(handle, UV_READABLE, cb)` → readable 시 `zlink_recv` drain
- `uv_poll_start(handle, UV_WRITABLE, cb)` → writable 시 `zlink_send` flush
- edge-triggered: 한 번의 wake에서 EAGAIN까지 모두 drain

---

## 2. API 설계 원칙

### 2.1 소켓 타입 계층 전략

- 소켓 타입별 concrete class를 제공한다 (`PairSocket`, `DealerSocket`, `RouterSocket`,
  `PubSocket`, `SubSocket`, `XPubSocket`, `XSubSocket`, `StreamSocket`).
- 각 클래스는 `bindings/README.md` Socket Capability Matrix의 `Y` 항목만 public으로
  노출한다. `—` 항목은 컴파일 타임에 접근 불가해야 한다.
- 공통 lifecycle (`bind`, `unbind`, `close`, `monitorOpen`, TLS, common options)은
  base interface로 올린다.
- `connect`/`disconnect`는 Stream을 제외한 소켓만 사용 가능한 별도 계층으로 분리한다.
- send/recv/publish/subscribe는 capability에 따라 concrete class에만 존재한다.
  generic base에 올리지 않는다.

### 2.2 Sync / Async 분리

- **blocking API**: 기본 동사 이름 (`send`, `recv`, `publish`, `subscribe`)
- **non-blocking API**: `try*` 접두사 (`trySend`, `tryRecv`, `tryPublish`, `trySubscribe`)
- **async API**: `a` 접두사 (`asend`, `arecv`). `Promise<T>` 반환.
  내부적으로 `uv_poll_t` fd-driven wake + `tryRecv`/`trySend` 조합.
- **stream 수신**: `Symbol.asyncIterator` / `asyncSubscribe()` →
  `AsyncIterableIterator<Received | Subscribed>`.
  `for await (const msg of socket)` 패턴.
- blocking/non-blocking을 public `flags` 파라미터로 전환하지 않는다.

### 2.3 Domain Object 전략

- `bindings/README.md` Domain Object Policy에 따라 최소 핵심 domain model을 제공한다:
  `Message`, `RoutingId`, `Received`, `Subscribed`, `SubscriptionEvent`, `SendResult`.
- `Received`: `routingId: RoutingId | null` + `parts: ReadonlyArray<Buffer>`.
  callback delivery와 direct recv의 payload shape가 동일해야 한다.
- `SendResult`: `Sent | Backpressured | NotReady` enum.
  non-blocking send는 `bool`이 아니라 explicit outcome.
- `Message`: `zlink_msg_t` wrapper. `data: Buffer` (zero-copy external buffer view),
  `close()`, `Symbol.dispose`. GC finalize에서 `zlink_msg_close` 호출.
- `RoutingId`: 생성 시 255바이트 초과 fail-fast. 불변 값 객체.
- 서비스 계층 domain object도 동일 원칙:
  `SpotNodeStatus`, `SpotNodePeerEntry`, `MemberPeerEntry`, `ServiceEvent`,
  `MonitorEvent`, `MonitorSnapshot` 등.

### 2.4 Option Facade 전략

- raw `setOption`/`getOption` bag은 public에 노출하지 않는다.
- `CommonSocketOptions`: 전 소켓 공통 typed property (linger, HWM, timeout,
  heartbeat, TCP keepalive 등)
- 소켓 타입별 option facade: `RouterSocketOptions`, `DealerSocketOptions`,
  `StreamSocketOptions`, `PubSocketOptions`, `SubSocketOptions`
- option 값 타입: `boolean`, `number` (ms 단위), `RoutingId`, `string`.
  raw int 금지.

### 2.5 리소스 관리

- 전 타입 `Symbol.asyncDispose` 지원 → `await using` 패턴.
- `Message`는 `Symbol.dispose` (sync) 지원.
- `close()` 명시 호출도 가능하지만, `await using`을 권장 경로로 한다.

### 2.6 Naming 규칙

- `bindings/README.md` Naming Policy를 따른다.
- TypeScript camelCase. canonical name의 케이싱 변형만 허용.
- overloading/union type으로 구분 가능하므로 파라미터 인코딩 접미사 금지.
  - `send(message)` / `send(routingId, message)` — 이름 하나, 시그니처로 구분.
- callback: `onReceive`, `onSubscribe`, `onSendReady` (canonical).

### 2.7 Error 전략

- blocking send/publish 실패: `throw ZlinkError`.
- non-blocking send/publish: `Backpressured`/`NotReady`는 정상 `SendResult` 반환.
  그 외 오류는 `throw`.
- non-blocking recv: `null` 반환 (EAGAIN). 그 외 오류는 `throw`.
- 바인딩 입력 검증 (RoutingId 길이, endpoint 길이, null 검사): 즉시 `throw TypeError`/
  `RangeError`.
- native 상태 오류 (EFSM, ETERM 등): `throw ZlinkError`로 surface. managed layer가
  임의 추론하지 않는다.

### 2.8 Capability 범위

소켓 계층, 서비스 계층(Spot, SpotNode, Discovery, Registry), 모니터(SocketMonitor,
ServiceMonitor), Poller 모두 `bindings/README.md`의 해당 Capability Matrix를 따른다.
구체적 메서드 목록은 Matrix를 참조한다.

---

## 3. Async API 설계

### 3.1 패턴 개요

| 패턴 | 용도 | 구현 |
|------|------|------|
| `await arecv()` | 단발 수신 | `uv_poll_t` readable → `tryRecv()` → Promise resolve |
| `for await (... of socket)` | 스트림 수신 | `AsyncIterableIterator` + `uv_poll_t` |
| `await asend(msg)` | 비동기 송신 | `uv_poll_t` writable → `trySend()` → Promise resolve |
| `await apoll(timeout)` | 비동기 폴링 | `uv_poll_t` on poller fd |
| `onReceive(handler)` | C 콜백 직결 | TSFN (동적 할당) |

### 3.2 스트림 수신 (AsyncIterableIterator)

```typescript
// Pair, Dealer, Router, Stream
const dealer = new DealerSocket(ctx);
dealer.connect("tcp://localhost:5555");

for await (const received of dealer) {
  const payload = received.parts[0];
  // process
}

// Sub, XSub
const sub = new SubSocket(ctx);
sub.connect("tcp://localhost:5555");
sub.setSubscription("market.");

for await (const subscribed of sub.asyncSubscribe()) {
  console.log(`[${subscribed.topic}] ${subscribed.parts[0]}`);
}

// Monitor
const monitor = socket.monitorOpen();
for await (const event of monitor) {
  console.log(`${MonitorEventType[event.type]}: ${event.remoteAddress}`);
}
```

내부 구현:

```typescript
// AsyncIterableIterator 내부
async *[Symbol.asyncIterator]() {
  while (!this._closed) {
    const received = await this.arecv();  // uv_poll_t 대기
    yield received;
  }
}
```

### 3.3 비동기 송신

```typescript
await socket.asend(Buffer.from("hello"));

// Router — routed send
await router.asend(routingId, Buffer.from("response"));
```

내부 구현: `uv_poll_t` writable 감시 → writable 시 `trySend()` → Sent면
resolve, Backpressured면 다시 대기.

### 3.4 리소스 관리

```typescript
// await using (TC39 Explicit Resource Management)
await using ctx = new ZlinkContext();
await using socket = new DealerSocket(ctx);
socket.connect("tcp://localhost:5555");

for await (const msg of socket) {
  // ...
}
// 스코프 끝에서 자동 close
```

---

## 4. 성능 최적화 전략

### 4.1 Zero-Copy 경로

| 경로 | 방법 | 복사 횟수 |
|------|------|-----------|
| recv → JS Buffer | `napi_create_external_buffer` + `zlink_msg_data()` | 0 |
| JS Buffer → send | `napi_get_buffer_info` + `zlink_msg_init_data` + ref pinning | 0 |
| recv → string | `Buffer.toString()` | 1 (인코딩) |
| string → send | `Buffer.from(string)` + zero-copy send | 1 (인코딩) |

### 4.2 버퍼 관리

- recv: `Received.parts`는 외부 버퍼 배열. GC가 수거할 때 `zlink_msg_close` 호출
- send: JS Buffer를 `napi_create_reference`로 고정, `zlink_free_fn`에서 해제
- multipart: C++ 측에서 `zlink_msg_t[]` 스택/힙 관리, JS에는 Buffer[] 노출

### 4.3 Sync vs Async 오버헤드

| 경로 | 오버헤드 | 용도 |
|------|----------|------|
| `tryRecv()` (sync) | N-API 호출 1회 (~1μs) | perf, worker thread |
| `arecv()` (async) | `uv_poll_t` wake + N-API (~5μs) | 앱 이벤트 루프 |
| `onReceive()` (callback) | TSFN dispatch (~50–100μs) | 앱 콜백 패턴 |

### 4.4 수신 방식 트레이드오프 비교

| 방식 | 지연 | 처리량 | 복잡도 | 용도 |
|------|------|--------|--------|------|
| `uv_poll_t` + `tryRecv` | ~5μs | 최고 | 중간 | async API 내부, perf |
| TSFN callback | ~50–100μs | 중간 | 낮음 | 앱 콜백 API |
| `setImmediate` 폴링 | ~1–4ms | 낮음 | 최저 | **제거 대상** |
| blocking `recv()` | 0 (대기 중) | 높음 | 최저 | worker thread, perf |

---

## 5. 기존 구현 개선 가이드

### 5.1 현재 구현 문제점

| 문제 | 영향 |
|------|------|
| `setImmediate` 폴링 | 1–4ms 지연, CPU 낭비 |
| TSFN 8 슬롯 고정 | 동시 소켓 8개 제한 |
| Buffer 복사 (recv) | 대용량 메시지 처리량 저하 |
| 부분적 TypeScript 타입 | IDE 지원 미흡 |
| `Symbol.asyncDispose` 미지원 | 리소스 누수 위험 |
| async iterator 미지원 | 비관용적 수신 패턴 |

### 5.2 개선 항목 및 우선순위

| 우선순위 | 항목 | 현재 | 목표 | 예상 효과 |
|----------|------|------|------|-----------|
| **P0** | `uv_poll_t` 통합 | `setImmediate` 폴링 | fd-driven async | 지연 100–800x 개선 |
| **P0** | `AsyncIterableIterator` | 없음 | `for await (... of socket)` | idiomatic 수신 패턴 |
| **P0** | TSFN 동적 할당 | 8 슬롯 고정 | 소켓별 동적 할당 | 동시 소켓 무제한 |
| **P1** | zero-copy recv | Buffer 복사 | `napi_create_external_buffer` | 대용량 메시지 ~2x 처리량 |
| **P1** | zero-copy send | Buffer 복사 | ref pinning + `zlink_msg_init_data` | 대용량 메시지 ~2x 처리량 |
| **P1** | Promise 기반 async | 없음 | `asend()` / `arecv()` | 이벤트 루프 네이티브 통합 |
| **P2** | TypeScript strict types | 부분 타입 | 제네릭 소켓 타입 + 완전 타이핑 | DX 개선 |
| **P2** | `Symbol.asyncDispose` | 수동 `close()` | `await using` 지원 | 리소스 안전 |
| **P2** | capability matrix 정렬 | 미검증 | surface test 포함 | 정책 준수 검증 |
