# Request-Reply Spec

> **상태**: Draft
> **정책 기준**: [`bindings/README.md`](README.md)

---

## 1. 개요

Router 소켓과 Dealer 소켓에 비동기 request-reply 기능을 추가한다.
C API에 메시지 request-reply 필드(msg_type, correlation_id)를 추가하고,
각 바인딩은 언어별 코루틴/async primitive를 활용하여 Router/Dealer 소켓의
기능을 확장한다.

### 1.1 동기

- Router 소켓 사용 시 async request-reply는 거의 항상 필요한 패턴이다.
- 매번 사용자가 correlation 로직, pending map, timeout 관리를 직접 구현해야
  한다.
- 바인딩이 이 패턴을 표준화하면 중복 구현을 없애고 일관된 API를 제공한다.

### 1.2 설계 원칙

- C API는 메시지 request-reply 필드만 제공한다. request-reply 로직은 C API에
  포함하지 않는다.
- request-reply dispatch, pending map, timeout, cancellation은 바인딩 레이어에서
  언어별 코루틴/async primitive로 구현한다.
- request-reply는 Router/Dealer 소켓의 **기능 확장**이다.
  별도 추상 레이어가 아니라 기존 소켓에 request-reply capability를 얹는 것이다.
- blocking request는 제공하지 않는다. 코루틴 버전과 콜백 버전만 제공한다.
  - blocking request는 reply 대기 중 스레드를 점유하여 deadlock 위험이 크다.
  - 단순 동기 용도는 코루틴 버전을 동기적으로 실행하면 된다
    (Java: `.get()`, Python: `asyncio.run()`, C++: `.get()`).

---

## 2. 유효한 Request-Reply 조합

| 요청자 | 응답자 | 가능 | reply 경로 |
|--------|--------|------|-----------|
| Dealer | Router | Y | Router가 Dealer의 routing_id로 회신 |
| Router | Router | Y | 서로 routing_id로 회신 |
| Dealer | Dealer | **N** | 양쪽 다 routing_id 없음 |
| Router | Dealer | **N** | Dealer가 특정 peer에 회신 불가 |

### 2.1 RequestDealer 연결 제약

- RequestDealer의 연결 대상은 **전부 Router**여야 한다.
- Dealer에 Router와 Dealer가 섞여 연결되면, round-robin으로 request가 Dealer에
  전달될 수 있다. 해당 Dealer는 reply 불가하므로 request가 실패한다.
- 이 제약은 바인딩이 런타임에 검증하지 않는다. 사용자 책임이며, API 문서에
  명시한다.

---

## 3. C API: 메시지 Request-Reply 필드

core C API의 상세 스펙은
[`doc/spec/message/MSG_REQUEST_REPLY_SPEC.md`](../doc/spec/message/MSG_REQUEST_REPLY_SPEC.md)
에 정의한다. 아래는 바인딩 구현에 필요한 요약이다.

### 3.1 요약

- `zlink_msg_t`의 공개 ABI는 변경하지 않는다. 내부 상태로 `msg_type`(uint8)과
  `correlation_id`(uint64)를 추가하고, accessor API로만 노출한다.
- 새 메시지의 기본값: `msg_type=DATA(0)`, `correlation_id=0`.
- DATA 메시지는 wire에 envelope가 없다. 기존 동작과 100% 호환.

### 3.2 상수

```c
#define ZLINK_MSG_TYPE_DATA     0
#define ZLINK_MSG_TYPE_REQUEST  1
#define ZLINK_MSG_TYPE_REPLY    2
```

### 3.3 API

```c
int zlink_msg_set_request(zlink_msg_t *msg, uint64_t correlation_id);
int zlink_msg_set_reply(zlink_msg_t *msg, uint64_t correlation_id);
int zlink_msg_get_request_info(const zlink_msg_t *msg,
                               uint8_t *type_out,
                               uint64_t *correlation_id_out);
```

- `set_request`: type=REQUEST + correlation_id 동시 설정.
- `set_reply`: type=REPLY + correlation_id 동시 설정.
- `get_request_info`: type + correlation_id 동시 조회.
  type이 DATA이면 correlation_id는 무의미.

### 3.4 Wire

- REQUEST/REPLY 메시지 send 시 core가 envelope를 자동 직렬화.
- recv 시 core가 자동 파싱하여 msg_type/correlation_id 복원, envelope strip.
- 바인딩은 envelope 파싱을 하지 않는다. recv 후
  `zlink_msg_get_request_info`로 조회하여 분기만 한다.
- 상세 encoding, 기존 API 상호작용, 구현 변경 지점, 테스트 요구사항은
  core 스펙 문서를 참조한다.

---

## 4. Router Request-Reply API

### 4.1 개요

request-reply는 RouterSocket의 **기능 확장**이다. 별도 추상 레이어가 아니라
기존 Router/Dealer 소켓에 request-reply capability를 얹는다.

구현 방식은 언어에 따라 다를 수 있다:
- 기존 소켓 클래스에 메서드 추가
- 확장 클래스 / mixin
- 래퍼 타입

어떤 방식이든 기존 소켓의 send/recv/bind/connect 등은 그대로 사용 가능하다.

#### 수신 Dispatch 모델

request-reply capability를 활성화하면 바인딩 내부에 **dispatch owner**가 생긴다.
이 owner가 해당 소켓의 receive plane을 독점하여 msg_type에 따라 분기한다.

```
소켓 recv owner → [C API: envelope 파싱, msg_type 설정]
                → [바인딩 dispatch owner]
                      ├── REQUEST → onRequest handler
                      ├── REPLY   → pending map resolve / callback 호출
                      └── DATA    → 기존 recv / 일반 수신 callback 경로
```

- dispatch owner는 `request()` 또는 `onRequest()` 최초 호출 시 자동 시작된다.
- dispatch owner가 활성화되면 **내부적으로 C API recv를 독점**한다.
  사용자의 `recv()` / `tryRecv()`는 dispatch owner가 분류한 DATA 큐에서
  읽는 형태로 전환된다. 즉 사용자 `recv()`는 더 이상 C API 직접 호출이 아니라
  dispatch owner가 채운 DATA 큐의 consumer가 된다.
- dispatch owner는 `REPLY`만 내부에서 소비한다.
- `DATA` 메시지는 기존 public receive surface로 그대로 전달된다.
  - direct receive: 기존 `recv()` / `tryRecv()` — DATA 큐에서 읽음
  - callback receive: 기존 일반 수신 callback (`onReceive` 또는 언어별 동등 이름)

DATA 수신 세부 규칙:
- `tryRecv()`: DATA 큐가 비어 있으면 기존과 동일하게 empty/null/None 반환.
- `recv()` (blocking): DATA 큐가 비어 있으면 DATA가 도착할 때까지 대기.
- DATA 메시지의 도착 순서는 wire 순서를 유지한다.
- 소켓 close 시 dispatch owner가 종료되고 blocking `recv()`가 wakeup된다.
- `onReceive` callback과 direct `recv()`는 기존 정책대로 상호 배타적이다.
  `onRequest`와 `onReceive`는 서로 다른 message type을 처리하므로
  동시 등록이 가능하다.

#### Callback 실행 컨텍스트

- `onRequest` handler, request callback, Future resolve는 모두
  **바인딩이 보장하는 안전한 실행 컨텍스트**에서 실행된다.
  - Node: event loop thread
  - Python: asyncio event loop
  - Java: socket dispatcher thread 또는 소켓에 설정된 executor
  - .NET: 캡처된 SynchronizationContext 또는 thread pool
  - Go: goroutine (별도)
  - C++: 코루틴 resume context
  - Rust: async runtime executor
- callback 내에서 같은 소켓의 `reply()`, `send()` 등을 호출할 수는 있지만,
  callback context에서 blocking send를 가정하면 안 된다.
- 바인딩은 기존 callback deadlock 회피 정책을 유지해야 한다. callback 내부 send는
  non-blocking 또는 callback-safe 경로여야 한다.

### 4.2 RequestRouter API

RouterSocket에 추가되는 request-reply capability.

#### Capability Matrix

| Capability | RequestRouter |
|---|---|
| `request` (코루틴) | Y — async send + reply 대기 → Received |
| `request` (콜백) | Y — async send + callback 으로 reply 전달 |
| `tryRequest` (코루틴) | Y — non-blocking send + reply 대기 → Received |
| `tryRequest` (콜백) | Y — non-blocking send + callback 으로 reply 전달 |
| `reply` | Y — async send (writable 대기) |
| `tryReply` | Y — non-blocking send → `SendResult` |
| `onRequest` | Y — 수신 REQUEST callback |
| 기존 Router 기능 | 그대로 유지 (send, recv, bind, connect 등) |

#### API 상세

**`request(routingId, message, [timeout])` → 코루틴 대기 → Received**

- REQUEST를 전송하고 REPLY가 올 때까지 **코루틴을 suspend**한다.
- 호출 스레드를 blocking하지 않는다.
- 내부 동작은 4.4 Request 내부 구현을 따른다.

```typescript
// Node
const reply = await router.request(routingId, msg, { timeout: 5000 });
```

```python
# Python
reply = await router.request(routing_id, msg, timeout=5.0)
```

```cpp
// C++
auto reply = co_await router.request(routing_id, msg, 5000ms);
```

```java
// Java — CompletableFuture 반환, .join()으로 동기 대기 가능
Received reply = router.request(routingId, msg, Duration.ofSeconds(5)).join();
```

**`request(routingId, message, callback, [timeout])` → 즉시 반환**

- REQUEST를 전송하고 **즉시 반환**한다. REPLY가 오면 callback을 호출한다.
- 코루틴 컨텍스트가 아닌 환경에서 사용한다.
- 내부 동작은 4.4 Request 내부 구현을 따른다. Future 대신 callback을 등록한다.

```typescript
// Node
router.request(routingId, msg, (err, reply) => {
  if (err) { /* timeout or error */ }
  else { /* process reply */ }
}, { timeout: 5000 });
```

```python
# Python
def on_reply(reply):
    ...
def on_error(err):
    ...
router.request(routing_id, msg, on_reply, on_error, timeout=5.0)
```

**`reply(routingId, correlationId, message)`**

- 수신한 REQUEST에 대한 REPLY를 전송한다.
- 내부적으로 `zlink_msg_set_reply(msg, correlationId)` 설정 후
  **async send (writable 대기 → 전송)**한다. `request()`와 동일한
  backpressure 처리를 사용한다.
- backpressure 시 writable 될 때까지 비동기 대기한다. 예외를 던지지 않는다.
- send 실패(EAGAIN 외 오류) 시 `ZlinkError(errno)` 예외를 던진다.
- callback 내에서 `reply()`를 호출하면 코루틴 suspend가 발생할 수 있다.
  이것은 기존 callback 내 `send()` 호출과 동일한 주의사항이다.
  callback이 suspend되는 동안 dispatch loop이 멈출 수 있으므로,
  고처리량 환경에서는 callback 내에서 reply를 직접 호출하는 대신
  별도 코루틴/태스크로 분리하는 것을 권장한다.

**`tryRequest(routingId, message, [timeout])` → 코루틴 대기 → Received**

- `request()`와 동일하지만 send 단계에서 **non-blocking send (trySend)**를
  사용한다. backpressure 시 writable 대기 없이 즉시 `ZlinkError(EAGAIN)`로
  실패한다.
- send 성공 후 reply 대기는 `request()`와 동일하게 코루틴 suspend.
- backpressure를 caller가 직접 제어하고 싶을 때 사용한다.

**`tryRequest(routingId, message, callback, [timeout])` → 즉시 반환**

- 콜백 버전. send 단계에서 backpressure 시 즉시 error callback 호출.

**`tryReply(routingId, correlationId, message)` → `SendResult`**

- `reply()`와 동일하지만 **non-blocking send (trySend)**를 사용한다.
- backpressure 시 writable 대기 없이 즉시 `SendResult::Backpressured` 반환.
- callback 내에서 dispatch loop을 멈추지 않고 즉시 반환해야 할 때 사용한다.
- 기존 `trySend()`와 동일한 반환 규칙: `Sent` / `Backpressured` / `NotReady`.
  EAGAIN 외 오류만 예외.

**`onRequest(handler)`**

- 수신 REQUEST에 대한 callback을 등록한다.
- handler는 `(routingId, correlationId, received)` 를 별도 파라미터로 받는다.
- handler 내부에서 `reply()` 또는 `tryReply()`를 호출하여 응답한다.
  - `reply()`: backpressure 시 코루틴 suspend. 안전하지만 dispatch loop 정지 가능.
  - `tryReply()`: 즉시 반환. dispatch loop을 멈추지 않지만 `Backpressured` 처리 필요.
- 두 번째 `onRequest()` 호출은 이전 handler를 **교체**한다. 체이닝하지 않는다.
- Callback API Policy를 따른다: callback 해제는 소켓 close로만 한다.

### 4.4 Request 내부 구현

#### Send 전략: Async Send (기존 send와 동일한 backpressure 처리)

`request()`의 내부 send는 기존 blocking `send()`와 동일하게 backpressure 시
**writable 될 때까지 비동기 대기** 후 전송한다. caller에게 backpressure를
예외로 노출하지 않는다.

```
request() 내부 흐름:

  1. atomic counter에서 correlation_id 채번 (uint64)
  2. zlink_msg_set_request(msg, correlation_id) 호출
  3. pending map에 correlation_id → Future/Promise/callback 등록
  4. timeout timer 시작 (전체 경과 시간 기준)
  5. async send (fd writable 대기 → trySend)
     - writable 될 때까지 코루틴 suspend (스레드 blocking 아님)
     - timeout 초과 시: pending map 제거 → ZlinkError(ETIMEDOUT)
     - send 실패 (EAGAIN 외 오류): pending map 제거 → ZlinkError(errno)
  6. reply 대기 (코루틴 suspend)
     - timeout 초과 시: pending map 제거 → ZlinkError(ETIMEDOUT)
     - 취소 시: pending map 제거 → ZlinkError(ECANCELED)
     - reply 수신 시: pending map 제거 → Received 반환
```

#### Timeout은 전체 경과 시간 기준

timeout은 send 대기 + reply 대기를 합산한 **전체 경과 시간**에 적용된다.

- timeout timer는 **send 전에** 시작한다.
- send 대기가 오래 걸리면 reply 대기 시간이 그만큼 줄어든다.
- send가 즉시 성공하면 timeout 전체가 reply 대기에 사용된다.
- timeout 시 어느 phase에 있든 즉시 중단하고 `ZlinkError(ETIMEDOUT)`을
  반환한다.

```
예: timeout = 5초

경우 1: send 즉시 성공 → reply 대기 5초
경우 2: send 대기 2초 + reply 대기 3초 = 합계 5초
경우 3: send 대기 5초 → timeout → reply 대기 없이 실패
```

#### Pending map 등록 시점

pending map 등록을 send 전에 수행하는 이유:
- peer가 매우 빠르게 reply하면 send 직후 recv 루프가 REPLY를 처리할 수 있다.
  send 후에 등록하면 pending map에 아직 항목이 없어 reply가 discard되고
  false timeout이 발생한다.
- send 실패 또는 timeout 시 pending map에서 즉시 제거하므로 orphan 항목이
  남지 않는다.

#### 메시지 상태

- `request()`는 내부적으로 `zlink_msg_set_request`를 호출하여 메시지를
  변경한다. send 실패 시 메시지는 변경된 상태로 caller에게 돌아온다.
- 재시도 시에는 새 메시지를 생성하거나, 동일 메시지를 재사용해도 된다
  (다음 `request()` 호출이 correlation_id를 다시 설정하므로).

#### 기존 send()와의 정렬

| | `send()` | `request()` |
|---|---|---|
| backpressure | writable 될 때까지 대기 | writable 될 때까지 비동기 대기 |
| 대기 방식 | 스레드 blocking | 코루틴 suspend (스레드 blocking 아님) |
| timeout | send timeout 옵션 | request timeout (전체 경과 시간) |
| 실패 | 예외 | 예외 (ZlinkError + errno) |

caller 입장에서 `request()`는 `send()` + reply 대기를 하나로 묶은 것이다.
backpressure가 예외로 노출되지 않으므로 별도 재시도 로직이 필요 없다.

#### Backpressure 전략

request-reply 경로의 backpressure는 두 지점에서 발생할 수 있다.

**1. Send 방향 backpressure (소켓 HWM)**

| 항목 | 동작 |
|------|------|
| 항목 | `request()` / `reply()` | `tryRequest()` / `tryReply()` |
|------|------------------------|-------------------------------|
| 발생 조건 | 소켓의 send HWM 도달 | 동일 |
| 처리 | writable 대기 → 자동 전송 | 즉시 실패 반환 |
| caller 영향 | suspend 시간 증가, timeout에 합산 | `EAGAIN` 예외 / `SendResult` |

- `request()` / `reply()`: backpressure가 caller에 노출되지 않는다.
  기존 `send()`와 동일한 메커니즘.
- `tryRequest()` / `tryReply()`: backpressure가 즉시 반환된다.
  기존 `trySend()`와 동일한 메커니즘.
- `request()`에서 send 대기가 길어지면 전체 timeout에서 reply 대기 시간이
  줄어든다. 극단적으로 send 대기만으로 timeout이 초과하면 `ETIMEDOUT`.

**2. Pending request 누적 (응답 지연)**

| 항목 | 동작 |
|------|------|
| 발생 조건 | peer가 reply를 느리게 보내거나 보내지 않음 |
| 처리 | 각 request의 timeout이 개별적으로 만료됨 |
| caller 영향 | timeout된 request는 `ETIMEDOUT` 예외 |
| 별도 제한 | 없음. 소켓 HWM이 send 방향 제한을 제공 |

- pending map 크기를 별도로 제한하지 않는다.
- 소켓 HWM이 send 방향의 자연스러운 상한을 제공한다. HWM에 도달하면
  새 request의 send 단계에서 writable 대기가 발생하고, 이것이 간접적으로
  pending request 증가 속도를 늦춘다.
- 각 pending request는 개별 timeout을 가지므로 응답 없는 request는
  자동으로 정리된다.
- caller가 추가적인 동시 요청 제한이 필요하면 caller 측에서
  semaphore/rate limiter를 사용한다. 바인딩은 이를 내장하지 않는다.

**3. 응답 방향 backpressure (reply 전송)**

| 항목 | `reply()` | `tryReply()` |
|------|-----------|-------------|
| 발생 조건 | send HWM 도달 | 동일 |
| 처리 | writable 대기 후 자동 전송 | 즉시 `SendResult::Backpressured` 반환 |
| caller 영향 | 코루틴 suspend | 즉시 반환, caller가 처리 |

- `reply()`: backpressure를 내부에서 writable 대기로 처리한다.
  callback 내에서 호출 시 코루틴 suspend가 발생할 수 있다.
  이것은 기존 callback 내 `send()` 호출과 동일한 주의사항이다.
- `tryReply()`: backpressure를 `SendResult`로 즉시 반환한다.
  callback 내에서 dispatch loop을 멈추지 않아야 할 때 사용한다.
  기존 `trySend()`와 동일한 동작.
- 고처리량 환경의 callback 내에서는 `tryReply()`를 사용하거나,
  `reply()`를 별도 코루틴/태스크로 분리하는 것을 권장한다.
- reply 전송이 지연되더라도 요청 측의 timeout은 독립적으로 동작한다.
  reply가 timeout 전에 도착하지 않으면 요청 측에서 `ETIMEDOUT`으로 처리된다.

#### 메시지 Dispatch 규칙

수신 메시지의 `msg_type`에 따라 내부에서 분기한다:
- `REQUEST` → `onRequest` handler로 전달
- `REPLY` → pending map에서 correlation_id로 매칭하여 resolve (또는 callback 호출)
- `DATA` → 기존 `recv()` / 일반 수신 callback 경로로 전달

### 4.3 RequestDealer API

DealerSocket에 추가되는 request-only capability.

#### Capability Matrix

| Capability | RequestDealer |
|---|---|
| `request` (코루틴) | Y — async send + reply 대기 → Received |
| `request` (콜백) | Y — async send + callback 으로 reply 전달 |
| `tryRequest` (코루틴) | Y — non-blocking send + reply 대기 → Received |
| `tryRequest` (콜백) | Y — non-blocking send + callback 으로 reply 전달 |
| 기존 Dealer 기능 | 그대로 유지 (send, recv, bind, connect 등) |

#### API 상세

**`request(message, [timeout])` → 코루틴 대기 → Received**

- REQUEST를 전송하고 REPLY가 올 때까지 코루틴을 suspend한다.
- 내부 동작은 4.4 Request 내부 구현을 따른다.
- routing_id 파라미터가 없다. Dealer는 peer를 지정할 수 없다.

**`request(message, callback, [timeout])` → 즉시 반환**

- 콜백 버전. 내부 동작은 4.4 Request 내부 구현을 따른다.
  Future 대신 callback을 등록한다.

**`tryRequest(message, [timeout])` → 코루틴 대기 → Received**

- send 단계에서 non-blocking send. backpressure 시 즉시 `ZlinkError(EAGAIN)`.
- reply 대기는 `request()`와 동일.

**`tryRequest(message, callback, [timeout])` → 즉시 반환**

- 콜백 버전. send 단계에서 backpressure 시 즉시 error callback.

**메시지 dispatch 규칙:**

- `REPLY` → pending map에서 correlation_id로 매칭하여 resolve
- `REQUEST` / `DATA` → 기존 recv / 일반 수신 경로로 전달

#### 연결 제약

- **연결 대상이 모두 Router여야 한다.**
- Dealer와 Router가 섞여 연결되면 request가 Dealer에 전달될 수 있고,
  해당 Dealer는 reply 불가하므로 timeout으로 실패한다.
- 바인딩은 이 제약을 런타임에 검증하지 않는다.
- API 문서와 sample에 이 제약을 명시한다.

---

## 5. 반환 타입과 오류

### 5.1 request() 반환

- `request()` 성공 시 기존 `Received` domain object를 반환한다.
  별도 `Reply` 타입은 만들지 않는다.
- correlation_id 매칭은 내부에서 완료되므로 caller는 `Received`의
  payload parts만 사용하면 된다.
- `Received`에 `routingId`가 포함되므로 Router-Router 시나리오에서
  응답자 식별도 가능하다.

### 5.2 onRequest handler 파라미터

- handler는 `(routingId, correlationId, received)` 별도 파라미터를 받는다.
  별도 `Request` 타입은 만들지 않는다.
- `routingId`: 요청자의 routing ID (Router에서만)
- `correlationId`: reply 시 사용할 correlation ID (uint64)
- `received`: 기존 `Received` domain object (payload parts)

### 5.3 예외 처리

request-reply 실패는 별도 `RequestError` 타입을 도입하지 않는다.
기존 `ZlinkError`/`ZlinkException` + errno 체계를 그대로 사용한다.

`request()` 는 이름 규칙상 blocking 계열이므로 backpressure 를 caller 에
예외로 노출하지 않는다. 내부적으로 writable 될 때까지 비동기 대기한 후
전송하며, 기존 `send()` 와 동일한 backpressure 처리를 제공한다.

#### errno 매핑

| 실패 원인 | errno | 발생 시점 | caller 대응 |
|----------|-------|----------|-----------|
| timeout (send 대기 + reply 대기 합산) | `ETIMEDOUT` | send 대기 중 또는 reply 대기 중 | timeout 조정, peer 상태 확인 |
| send 오류 (EAGAIN 외) | 해당 errno | send 단계 | 오류에 따라 판단 |
| caller 취소 | `ECANCELED` | send 대기 중 또는 reply 대기 중 | 정리 처리 |
| 소켓 close | `ETERM` | 대기 중 또는 close 후 호출 | 정리 처리 |

- backpressure(`EAGAIN`)는 caller 에 노출되지 않는다. 내부에서 writable
  대기로 처리한다. send 대기 중 timeout 초과 시 `ETIMEDOUT` 으로 전환된다.
- timeout(`ETIMEDOUT`): pending map 에서 제거 후 예외. send 대기 중이든
  reply 대기 중이든 동일하게 `ETIMEDOUT`.
- 취소(`ECANCELED`): pending map 에서 제거. 늦은 reply 는 discard.
  (.NET `CancellationToken`, Go `context.Cancel`, 기타 언어별 취소 메커니즘)
- close(`ETERM`): 모든 pending request 일괄 reject. close 후 새 호출도 `ETERM`.

상세 정책은 `bindings/README.md` 의 Request-Reply Error Policy 를 따른다.

#### 코루틴 버전 예외 전달

코루틴 버전에서 send 실패는 동기적으로 예외를 던지지 않는다.
즉시 실패한 Future/Promise 를 반환하여 모든 실패가 Future 경로를 통한다.
- Java: `CompletableFuture.failedFuture(new ZlinkException(errno))`
- .NET: `Task.FromException<Received>(new ZlinkException(errno))`
- Node: `Promise.reject(new ZlinkError(errno))`
- Python: 코루틴 내부에서 `raise ZlinkError(errno)`
- C++: `Task<Received>` 가 예외와 함께 즉시 resolve
- Rust: `Err(ZlinkError{code: errno})` 를 포함한 즉시 완료 Future
- Go: `(Received{}, ZlinkError{Code: errno})` 즉시 반환

#### Callback 계약

- request 의 callback(성공/실패)은 **정확히 한 번** 호출된다.
  성공이면 reply callback, 실패면 error callback. 둘 다 호출되거나
  둘 다 안 호출되는 경우는 없다.
- Java `BiConsumer<Received, ZlinkException>`: 성공 시 `(received, null)`,
  실패 시 `(null, exception)`. 둘 다 non-null 인 경우는 없다.
- Node `(err, received)`: 성공 시 `(null, received)`, 실패 시 `(err, undefined)`.
- Python: 별도 `on_reply`/`on_error` callback. 하나만 호출된다.
- error callback 에 전달되는 예외는 위 errno 매핑을 따른다.

---

## 6. Correlation ID 관리

- **채번**: 소켓 인스턴스 내부의 atomic uint64 counter. 0부터 순차 증가.
- **범위**: 소켓 인스턴스별 독립. 서로 다른 소켓의 correlation_id는
  충돌할 수 있지만, 각 소켓이 자신의 pending map만 관리하므로 문제 없다.
- **wrap-around**: uint64 범위이므로 실질적으로 wrap-around 없음.
- **pending map**: `Map<uint64, Future/Promise/Channel>`.
  request 시 등록, reply 수신 또는 timeout 시 제거.
- **스레드 안전성**: request()는 호출 스레드에서, reply dispatch는 recv
  루프 스레드에서 실행되므로 pending map은 thread-safe 자료구조를 사용한다.
  언어별 적절한 메커니즘을 적용한다 (Java: `ConcurrentHashMap`,
  C++: `std::mutex` 또는 lock-free map, Go: `sync.Map`, Rust: `DashMap`
  또는 `Mutex<HashMap>`, Node: 단일 스레드이므로 일반 Map,
  Python: asyncio 단일 루프이므로 일반 dict).

---

## 7. Timeout 처리

- timeout은 바인딩 레이어에서 처리한다. C API에 timeout 개념은 없다.
- request 호출 시 timeout 파라미터를 생략하면 **기본 timeout**으로 동작한다.
- 기본 timeout은 소켓 인스턴스별로 설정 가능하다.

### 7.1 기본 Timeout 설정

| Canonical Name | 설명 |
|---|---|
| `setDefaultRequestTimeout(duration)` | 기본 request timeout 설정 |
| `getDefaultRequestTimeout()` | 현재 기본 request timeout 조회 |

- 초기 기본값: 30초 (전 언어 동일).
- 소켓 인스턴스별 설정. 전역 설정이 아니다.
- request 호출 시 명시적 timeout을 전달하면 기본값보다 우선한다.

### 7.2 Timeout 동작

- timeout 초과 시:
  1. pending map에서 해당 correlation_id 항목 제거
  2. 코루틴 버전: `ZlinkError(ETIMEDOUT)` 예외/오류 반환
  3. 콜백 버전: callback에 `ZlinkError(ETIMEDOUT)` 전달

---

## 8. 언어별 인터페이스

### 8.1 C++ (C++20 coroutines)

```cpp
class RouterSocket {
    // ... 기존 API ...

    // 기본 timeout 설정
    void set_default_request_timeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds get_default_request_timeout() const;

    // 코루틴 — co_await로 reply 대기
    Task<Received> request(const RoutingId& routing_id, Message msg);
    Task<Received> request(const RoutingId& routing_id, Message msg,
                        std::chrono::milliseconds timeout);

    // 콜백 — 즉시 반환
    void request(const RoutingId& routing_id, Message msg,
                 std::function<void(Received)> on_reply,
                 std::function<void(ZlinkError)> on_error);
    void request(const RoutingId& routing_id, Message msg,
                 std::function<void(Received)> on_reply,
                 std::function<void(ZlinkError)> on_error,
                 std::chrono::milliseconds timeout);

    // tryRequest — non-blocking send + reply 대기
    Task<Received> try_request(const RoutingId& routing_id, Message msg);
    Task<Received> try_request(const RoutingId& routing_id, Message msg,
                               std::chrono::milliseconds timeout);
    void try_request(const RoutingId& routing_id, Message msg,
                     std::function<void(Received)> on_reply,
                     std::function<void(ZlinkError)> on_error);
    void try_request(const RoutingId& routing_id, Message msg,
                     std::function<void(Received)> on_reply,
                     std::function<void(ZlinkError)> on_error,
                     std::chrono::milliseconds timeout);

    // reply — async send, backpressure 내부 처리
    void reply(const RoutingId& routing_id, uint64_t correlation_id,
               Message msg);

    // tryReply — non-blocking send
    SendResult try_reply(const RoutingId& routing_id, uint64_t correlation_id,
                         Message msg);

    // 수신 request callback
    void on_request(std::function<void(RoutingId, uint64_t, Received)> handler);
};

class DealerSocket {
    // ... 기존 API ...

    void set_default_request_timeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds get_default_request_timeout() const;

    Task<Received> request(Message msg);
    Task<Received> request(Message msg, std::chrono::milliseconds timeout);
    void request(Message msg,
                 std::function<void(Received)> on_reply,
                 std::function<void(ZlinkError)> on_error);
    void request(Message msg,
                 std::function<void(Received)> on_reply,
                 std::function<void(ZlinkError)> on_error,
                 std::chrono::milliseconds timeout);

    Task<Received> try_request(Message msg);
    Task<Received> try_request(Message msg, std::chrono::milliseconds timeout);
    void try_request(Message msg,
                     std::function<void(Received)> on_reply,
                     std::function<void(ZlinkError)> on_error);
    void try_request(Message msg,
                     std::function<void(Received)> on_reply,
                     std::function<void(ZlinkError)> on_error,
                     std::chrono::milliseconds timeout);
};
```

```cpp
// 사용 예
auto reply = co_await router.request(routing_id, msg, 5000ms);

router.on_request([&](RoutingId id, uint64_t corr_id, Received req) {
    router.reply(id, corr_id, Message::from("ok"));
});
```

### 8.2 Java (Kotlin 호환)

Java API는 `CompletableFuture`를 반환하여 Kotlin에서 `.await()` 확장 함수로
자연스럽게 사용할 수 있도록 설계한다.

```java
public class RouterSocket {
    // ... 기존 API ...

    // 기본 timeout 설정
    void setDefaultRequestTimeout(Duration timeout);
    Duration getDefaultRequestTimeout();

    // 코루틴 대기 — CompletableFuture 반환
    // Kotlin: router.request(routingId, msg).await()
    CompletableFuture<Received> request(RoutingId routingId, Message message);
    CompletableFuture<Received> request(RoutingId routingId, Message message,
                                     Duration timeout);

    // 콜백 — 즉시 반환
    void request(RoutingId routingId, Message message,
                 BiConsumer<Received, ZlinkException> callback);
    void request(RoutingId routingId, Message message,
                 BiConsumer<Received, ZlinkException> callback, Duration timeout);

    // tryRequest — non-blocking send + reply 대기
    CompletableFuture<Received> tryRequest(RoutingId routingId, Message message);
    CompletableFuture<Received> tryRequest(RoutingId routingId, Message message,
                                           Duration timeout);
    void tryRequest(RoutingId routingId, Message message,
                    BiConsumer<Received, ZlinkException> callback);
    void tryRequest(RoutingId routingId, Message message,
                    BiConsumer<Received, ZlinkException> callback, Duration timeout);

    // reply — async send, backpressure 내부 처리
    void reply(RoutingId routingId, long correlationId, Message message);

    // tryReply — non-blocking send
    SendResult tryReply(RoutingId routingId, long correlationId, Message message);

    // 수신 request callback
    void onRequest(RequestHandler handler);
}

@FunctionalInterface
public interface RequestHandler {
    void handle(RoutingId routingId, long correlationId, Received received);
}

public class DealerSocket {
    // ... 기존 API ...

    void setDefaultRequestTimeout(Duration timeout);
    Duration getDefaultRequestTimeout();

    CompletableFuture<Received> request(Message message);
    CompletableFuture<Received> request(Message message, Duration timeout);
    void request(Message message, BiConsumer<Received, ZlinkException> callback);
    void request(Message message, BiConsumer<Received, ZlinkException> callback,
                 Duration timeout);

    CompletableFuture<Received> tryRequest(Message message);
    CompletableFuture<Received> tryRequest(Message message, Duration timeout);
    void tryRequest(Message message, BiConsumer<Received, ZlinkException> callback);
    void tryRequest(Message message, BiConsumer<Received, ZlinkException> callback,
                    Duration timeout);
}
```

```java
// Java 사용 예
Received reply = router.request(routingId, msg, Duration.ofSeconds(5)).join();

router.onRequest((id, corrId, req) -> {
    router.reply(id, corrId, Message.from("ok"));
});
```

```kotlin
// Kotlin 사용 예 — coroutine scope에서 자연스럽게 사용

// 코루틴 — CompletableFuture.await()으로 suspend
suspend fun handleRequest() {
    val reply = router.request(routingId, msg, 5.seconds.toJavaDuration()).await()
    println("reply: ${reply.parts[0]}")
}

// 콜백 — lambda 직접 전달
router.request(routingId, msg, { received, error ->
    if (error != null) {
        println("error: ${error.errorCode}")
    } else {
        println("reply: ${received!!.parts[0]}")
    }
}, Duration.ofSeconds(5))

// 수신 request handler
router.onRequest { id, corrId, req ->
    router.reply(id, corrId, Message.from("ok"))
}

// default timeout 설정
router.setDefaultRequestTimeout(Duration.ofSeconds(10))

// timeout 생략 시 default timeout 사용
val reply = router.request(routingId, msg).await()

// Dealer
val dealerReply = dealer.request(msg, 3.seconds.toJavaDuration()).await()
```

Kotlin 호환 설계 원칙:
- `CompletableFuture` 반환으로 `kotlinx.coroutines`의 `.await()` 사용 가능
- `@FunctionalInterface`로 Kotlin lambda 직접 전달 가능
- `Duration` 파라미터로 Kotlin `kotlin.time.Duration.toJavaDuration()` 호환
- Java API만으로 Kotlin에서 자연스럽게 동작. 별도 Kotlin 래퍼 불필요.

Send 실패 경로:
- `request()`: async send이므로 backpressure가 노출되지 않는다.
  timeout/send 오류 시 실패한 `CompletableFuture`를 반환한다.
- `tryRequest()`: trySend 실패(Backpressured, NotReady) 시 즉시 실패한
  `CompletableFuture`를 반환한다
  (`CompletableFuture.failedFuture(new ZlinkException(EAGAIN))`).
- 모든 실패가 Future 경로를 통하므로 caller의 에러 핸들링이 일관된다.
- 이 원칙은 .NET `Task`, Node `Promise`, Rust `Future` 등 전 언어에 동일하게
  적용한다.

### 8.3 .NET (async/await)

```csharp
public class RouterSocket {
    // ... 기존 API ...

    TimeSpan DefaultRequestTimeout { get; set; }

    // async — await로 reply 대기
    Task<Received> RequestAsync(RoutingId routingId, Message message,
                             CancellationToken ct = default);
    Task<Received> RequestAsync(RoutingId routingId, Message message,
                             TimeSpan timeout, CancellationToken ct = default);

    // 콜백 — 즉시 반환
    void Request(RoutingId routingId, Message message,
                 Action<Received> onReply, Action<ZlinkException> onError);
    void Request(RoutingId routingId, Message message,
                 Action<Received> onReply, Action<ZlinkException> onError,
                 TimeSpan timeout);

    // tryRequest — non-blocking send
    Task<Received> TryRequestAsync(RoutingId routingId, Message message,
                                   CancellationToken ct = default);
    Task<Received> TryRequestAsync(RoutingId routingId, Message message,
                                   TimeSpan timeout, CancellationToken ct = default);
    void TryRequest(RoutingId routingId, Message message,
                    Action<Received> onReply, Action<ZlinkException> onError);
    void TryRequest(RoutingId routingId, Message message,
                    Action<Received> onReply, Action<ZlinkException> onError,
                    TimeSpan timeout);

    void Reply(RoutingId routingId, ulong correlationId, Message message);
    SendResult TryReply(RoutingId routingId, ulong correlationId, Message message);

    void OnRequest(Action<RoutingId, ulong, Received> handler);
}

public class DealerSocket {
    // ... 기존 API ...

    TimeSpan DefaultRequestTimeout { get; set; }

    Task<Received> RequestAsync(Message message,
                             CancellationToken ct = default);
    Task<Received> RequestAsync(Message message, TimeSpan timeout,
                             CancellationToken ct = default);
    void Request(Message message,
                 Action<Received> onReply, Action<ZlinkException> onError);
    void Request(Message message,
                 Action<Received> onReply, Action<ZlinkException> onError,
                 TimeSpan timeout);

    Task<Received> TryRequestAsync(Message message,
                                   CancellationToken ct = default);
    Task<Received> TryRequestAsync(Message message, TimeSpan timeout,
                                   CancellationToken ct = default);
    void TryRequest(Message message,
                    Action<Received> onReply, Action<ZlinkException> onError);
    void TryRequest(Message message,
                    Action<Received> onReply, Action<ZlinkException> onError,
                    TimeSpan timeout);
}
```

```csharp
// 사용 예
var reply = await router.RequestAsync(routingId, msg,
                                      TimeSpan.FromSeconds(5));

router.OnRequest((id, corrId, req) => {
    router.Reply(id, corrId, Message.From("ok"));
});
```

- .NET은 `CancellationToken`으로 취소를 지원한다.
- timeout과 cancellation이 모두 적용되며, 먼저 발생한 쪽이 우선한다.

### 8.4 Node / TypeScript (async/await)

```typescript
class RouterSocket {
    // ... 기존 API ...

    setDefaultRequestTimeout(ms: number): void;
    getDefaultRequestTimeout(): number;

    // 코루틴 — await로 reply 대기
    request(routingId: RoutingId, message: Message): Promise<Received>;
    request(routingId: RoutingId, message: Message,
            options: { timeout?: number }): Promise<Received>;

    // 콜백 — 즉시 반환
    request(routingId: RoutingId, message: Message,
            callback: (err: ZlinkError | null, reply?: Received) => void): void;
    request(routingId: RoutingId, message: Message,
            callback: (err: ZlinkError | null, reply?: Received) => void,
            options: { timeout?: number }): void;

    // tryRequest — non-blocking send
    tryRequest(routingId: RoutingId, message: Message): Promise<Received>;
    tryRequest(routingId: RoutingId, message: Message,
               options: { timeout?: number }): Promise<Received>;
    tryRequest(routingId: RoutingId, message: Message,
               callback: (err: ZlinkError | null, reply?: Received) => void): void;
    tryRequest(routingId: RoutingId, message: Message,
               callback: (err: ZlinkError | null, reply?: Received) => void,
               options: { timeout?: number }): void;

    reply(routingId: RoutingId, correlationId: bigint, message: Message): void;
    tryReply(routingId: RoutingId, correlationId: bigint, message: Message): SendResult;

    onRequest(handler: (routingId: RoutingId, correlationId: bigint,
                        received: Received) => void): void;
}

class DealerSocket {
    // ... 기존 API ...

    setDefaultRequestTimeout(ms: number): void;
    getDefaultRequestTimeout(): number;

    request(message: Message): Promise<Received>;
    request(message: Message, options: { timeout?: number }): Promise<Received>;
    request(message: Message,
            callback: (err: ZlinkError | null, reply?: Received) => void): void;
    request(message: Message,
            callback: (err: ZlinkError | null, reply?: Received) => void,
            options: { timeout?: number }): void;

    tryRequest(message: Message): Promise<Received>;
    tryRequest(message: Message, options: { timeout?: number }): Promise<Received>;
    tryRequest(message: Message,
               callback: (err: ZlinkError | null, reply?: Received) => void): void;
    tryRequest(message: Message,
               callback: (err: ZlinkError | null, reply?: Received) => void,
               options: { timeout?: number }): void;
}
```

```typescript
// 사용 예
const reply = await router.request(routingId, msg, { timeout: 5000 });

router.onRequest((routingId, correlationId, received) => {
    router.reply(routingId, correlationId, Buffer.from("ok"));
});
```

- Node의 callback 스타일은 `(err, result)` 패턴을 따른다.
- timeout 단위는 milliseconds (Node 관례).
- `correlationId`는 `bigint` (uint64).

### 8.5 Python (asyncio)

```python
class AsyncRouterSocket:
    # ... 기존 API ...

    def set_default_request_timeout(self, timeout: float) -> None: ...
    def get_default_request_timeout(self) -> float: ...

    # 코루틴 — await로 reply 대기
    async def request(self, routing_id: RoutingId, message: Message,
                      *, timeout: float | None = None) -> Received: ...

    # 콜백 — 즉시 반환
    def request_with_callback(
        self, routing_id: RoutingId, message: Message,
        on_reply: Callable[[Received], None],
        on_error: Callable[[ZlinkError], None],
        *, timeout: float | None = None
    ) -> None: ...

    # tryRequest — non-blocking send
    async def try_request(self, routing_id: RoutingId, message: Message,
                          *, timeout: float | None = None) -> Received: ...

    def try_request_with_callback(
        self, routing_id: RoutingId, message: Message,
        on_reply: Callable[[Received], None],
        on_error: Callable[[ZlinkError], None],
        *, timeout: float | None = None
    ) -> None: ...

    def reply(self, routing_id: RoutingId, correlation_id: int,
              message: Message) -> None: ...

    def try_reply(self, routing_id: RoutingId, correlation_id: int,
                  message: Message) -> SendResult: ...

    def on_request(self, handler: Callable[
        [RoutingId, int, Received], None
    ]) -> None: ...


class AsyncDealerSocket:
    # ... 기존 API ...

    def set_default_request_timeout(self, timeout: float) -> None: ...
    def get_default_request_timeout(self) -> float: ...

    async def request(self, message: Message,
                      *, timeout: float | None = None) -> Received: ...

    def request_with_callback(
        self, message: Message,
        on_reply: Callable[[Received], None],
        on_error: Callable[[ZlinkError], None],
        *, timeout: float | None = None
    ) -> None: ...

    async def try_request(self, message: Message,
                          *, timeout: float | None = None) -> Received: ...

    def try_request_with_callback(
        self, message: Message,
        on_reply: Callable[[Received], None],
        on_error: Callable[[ZlinkError], None],
        *, timeout: float | None = None
    ) -> None: ...
```

```python
# 사용 예
reply = await router.request(routing_id, msg, timeout=5.0)

def handle_request(routing_id, correlation_id, received):
    router.reply(routing_id, correlation_id, b"ok")

router.on_request(handle_request)
```

- timeout 단위는 seconds (float, Python 관례).
- Python은 overloading이 없으므로 콜백 버전은 `request_with_callback`으로
  분리한다.
- `timeout=None`이면 기본 timeout 사용.

### 8.6 Go (context)

Go는 코루틴 대신 goroutine + `context.Context`로 timeout/cancellation을
처리한다. goroutine은 경량이므로 goroutine을 block하는 것이 idiomatic하다.

```go
type RouterSocket struct {
    // ... 기존 API ...
}

func (s *RouterSocket) SetDefaultRequestTimeout(d time.Duration)
func (s *RouterSocket) GetDefaultRequestTimeout() time.Duration

// context로 timeout/cancellation — goroutine을 block
func (s *RouterSocket) Request(ctx context.Context,
    routingId RoutingId, msg Message) (Received, error)

// 콜백 — 즉시 반환
func (s *RouterSocket) RequestAsync(routingId RoutingId, msg Message,
    callback func(Received, error), timeout time.Duration)

// tryRequest — non-blocking send
func (s *RouterSocket) TryRequest(ctx context.Context,
    routingId RoutingId, msg Message) (Received, error)
func (s *RouterSocket) TryRequestAsync(routingId RoutingId, msg Message,
    callback func(Received, error), timeout time.Duration)

func (s *RouterSocket) Reply(routingId RoutingId,
    correlationId uint64, msg Message) error
func (s *RouterSocket) TryReply(routingId RoutingId,
    correlationId uint64, msg Message) (SendResult, error)

func (s *RouterSocket) OnRequest(
    handler func(RoutingId, uint64, Received))


type DealerSocket struct {
    // ... 기존 API ...
}

func (s *DealerSocket) SetDefaultRequestTimeout(d time.Duration)
func (s *DealerSocket) GetDefaultRequestTimeout() time.Duration

func (s *DealerSocket) Request(ctx context.Context,
    msg Message) (Received, error)
func (s *DealerSocket) RequestAsync(msg Message,
    callback func(Received, error), timeout time.Duration)

func (s *DealerSocket) TryRequest(ctx context.Context,
    msg Message) (Received, error)
func (s *DealerSocket) TryRequestAsync(msg Message,
    callback func(Received, error), timeout time.Duration)
```

```go
// 사용 예
ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
defer cancel()
reply, err := router.Request(ctx, routingId, msg)

router.OnRequest(func(id RoutingId, corrId uint64, req Received) {
    router.Reply(id, corrId, NewMessage([]byte("ok")))
})
```

- Go는 `context.Context`가 timeout/cancellation의 표준이다.
- `Request`는 goroutine을 block하지만 OS 스레드를 block하지 않는다.
- `RequestAsync`는 콜백 패턴이 필요한 경우에 사용한다.

### 8.7 Rust (async/await)

```rust
impl RouterSocket {
    // ... 기존 API ...

    fn set_default_request_timeout(&self, timeout: Duration);
    fn get_default_request_timeout(&self) -> Duration;

    // async — .await로 reply 대기
    async fn request(&self, routing_id: &RoutingId, msg: Message)
        -> Result<Received, ZlinkError>;
    async fn request_with_timeout(&self, routing_id: &RoutingId,
        msg: Message, timeout: Duration) -> Result<Received, ZlinkError>;

    // 콜백 — 즉시 반환
    fn request_callback(&self, routing_id: &RoutingId, msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static);
    fn request_callback_with_timeout(&self, routing_id: &RoutingId,
        msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static,
        timeout: Duration);

    // try_request — non-blocking send
    async fn try_request(&self, routing_id: &RoutingId, msg: Message)
        -> Result<Received, ZlinkError>;
    async fn try_request_with_timeout(&self, routing_id: &RoutingId,
        msg: Message, timeout: Duration) -> Result<Received, ZlinkError>;
    fn try_request_callback(&self, routing_id: &RoutingId, msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static);
    fn try_request_callback_with_timeout(&self, routing_id: &RoutingId,
        msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static,
        timeout: Duration);

    fn reply(&self, routing_id: &RoutingId, correlation_id: u64,
             msg: Message) -> Result<(), ZlinkError>;
    fn try_reply(&self, routing_id: &RoutingId, correlation_id: u64,
                 msg: Message) -> SendResult;

    fn on_request(&self,
        handler: impl Fn(RoutingId, u64, Received) + Send + Sync + 'static);
}

impl DealerSocket {
    // ... 기존 API ...

    fn set_default_request_timeout(&self, timeout: Duration);
    fn get_default_request_timeout(&self) -> Duration;

    async fn request(&self, msg: Message)
        -> Result<Received, ZlinkError>;
    async fn request_with_timeout(&self, msg: Message, timeout: Duration)
        -> Result<Received, ZlinkError>;
    fn request_callback(&self, msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static);
    fn request_callback_with_timeout(&self, msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static,
        timeout: Duration);

    async fn try_request(&self, msg: Message)
        -> Result<Received, ZlinkError>;
    async fn try_request_with_timeout(&self, msg: Message, timeout: Duration)
        -> Result<Received, ZlinkError>;
    fn try_request_callback(&self, msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static);
    fn try_request_callback_with_timeout(&self, msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static,
        timeout: Duration);
}
```

```rust
// 사용 예
let reply = router.request_with_timeout(
    &routing_id, msg, Duration::from_secs(5)
).await?;

router.on_request(|id, corr_id, req| {
    router.reply(&id, corr_id, Message::from(b"ok")).unwrap();
});
```

- Rust는 overloading이 없으므로 timeout 변형은 `_with_timeout` 접미사.
- `Result<Received, ZlinkError>`로 오류를 명시적으로 전달. errno 로 원인 구분.
- callback은 `FnOnce` + `Send` + `'static` 바운드.

### 8.8 REPLY 수신 Dispatch

모든 언어에서 내부 수신 루프는 동일한 dispatch 규칙을 따른다:

```
내부 수신 루프:
  loop:
    msg = socket.recv()
    zlink_msg_get_request_info(msg, &type, &correlation_id)
    switch type:
      case REPLY:
        entry = pending_map.remove(correlation_id)
        if entry:
          resolve(entry, Received(msg))  // Future resolve 또는 callback 호출
        else:
          discard  // timeout, 취소, 또는 중복/stray reply — 무시
      case REQUEST:
        deliver to onRequest handler
      case DATA:
        기존 recv 경로로 전달
```

DATA 메시지는 request-reply 수신 루프가 소비하지 않는다.
기존 recv 경로(direct recv, onReceive callback)로 그대로 전달된다.

---

## 9. 소유권 및 Lifecycle

### 9.1 Request-Reply 상태와 소켓 Lifecycle

- request-reply 상태(pending map, correlation counter)는 소켓 인스턴스에
  귀속된다.
- 소켓 close 시 pending map의 모든 미완료 request는 `ZlinkError(ETERM)`으로
  reject된다.

### 9.2 메시지 소유권

- `request()` 호출 시 메시지 ownership은 기존 send 계약을 따른다.
  send 성공 시 native로 이동, 실패 시 caller 유지.
- `reply()` 호출 시 동일.
- 수신한 `Received`의 ownership은 기존 recv 계약을 따른다.
  바인딩이 받아서 해제 책임.
- `onRequest` callback으로 전달된 `Received`의 parts는 callback 반환 후에도
  유효하다 (기존 `onReceive` callback payload lifetime과 동일).

### 9.3 잘못된 routingId로 reply 전송

- `reply(routingId, correlationId, msg)`에서 routingId가 원래 요청자와 다른
  경우, reply가 엉뚱한 peer에 전달된다. 원래 요청자는 timeout으로 실패한다.
- 바인딩은 routingId의 정확성을 검증하지 않는다. 사용자 책임이다.
- 안전한 패턴: `onRequest` handler에서 받은 `routingId`를 그대로
  `reply()`에 전달한다.

---

## 10. Error 처리

별도 `RequestError` 타입은 도입하지 않는다. 기존 `ZlinkError` + errno 를
그대로 사용한다. 상세 정책은 `bindings/README.md` Request-Reply Error Policy
를 따른다.

### 10.1 request 오류

| 상황 | `request()` | `tryRequest()` |
|------|------------|---------------|
| backpressure | writable 대기 (timeout에 합산) | 즉시 `ZlinkError(EAGAIN)` |
| timeout | `ZlinkError(ETIMEDOUT)` | 동일 |
| send 오류 (EAGAIN 외) | `ZlinkError(errno)` | 동일 |
| caller 취소 | `ZlinkError(ECANCELED)` | 동일 |
| 소켓 close | `ZlinkError(ETERM)` | 동일 |
| pending map에 없는 reply | 무시 | 무시 |

- `request()`: backpressure를 내부에서 writable 대기로 처리한다.
- `tryRequest()`: backpressure를 즉시 `EAGAIN`으로 반환한다.
- 코루틴 버전은 실패한 Future/예외, 콜백 버전은 error callback 으로 전달한다.

### 10.2 reply 오류

| 상황 | `reply()` | `tryReply()` |
|------|-----------|-------------|
| backpressure | writable 대기 | `SendResult::Backpressured` 반환 |
| not ready | writable 대기 | `SendResult::NotReady` 반환 |
| send 성공 | 정상 반환 | `SendResult::Sent` 반환 |
| send 오류 (EAGAIN 외) | `ZlinkError(errno)` 예외 | `ZlinkError(errno)` 예외 |
| 잘못된 correlation_id | 검증 안 함 | 검증 안 함 |

- `reply()`: async send. 기존 `send()`와 동일한 backpressure 처리.
- `tryReply()`: non-blocking send. 기존 `trySend()`와 동일한 `SendResult` 반환.

### 10.3 Error Policy 정렬

| API | 대응하는 기존 API | backpressure 처리 | 실패 시 |
|-----|-----------------|------------------|--------|
| `request()` | `send()` | writable 대기 | `ZlinkError(errno)` 예외 |
| `tryRequest()` | `trySend()` | 즉시 `EAGAIN` | `ZlinkError(errno)` 예외 |
| `reply()` | `send()` | writable 대기 | `ZlinkError(errno)` 예외 |
| `tryReply()` | `trySend()` | `SendResult` 반환 | EAGAIN 외만 예외 |

---

## 11. Naming Policy

bindings/README.md Naming Policy를 따른다. canonical 이름:

| Socket | Canonical Name | 설명 |
|---|---|---|
| Router | `request` / `tryRequest` | async send / non-blocking send + reply 대기 |
| Router | `reply` / `tryReply` | async send / non-blocking send |
| Router | `onRequest` | 수신 request callback |
| Router, Dealer | `setDefaultRequestTimeout` | 기본 request timeout 설정 |
| Router, Dealer | `getDefaultRequestTimeout` | 기본 request timeout 조회 |
| Dealer | `request` / `tryRequest` | async send / non-blocking send + reply 대기 |

`request`/`reply`와 `tryRequest`/`tryReply`의 관계는 기존 `send`/`trySend`와
동일하다. `try*` 접두사는 non-blocking send를 의미한다.

언어별 케이싱 변형:
- Python: `request`, `try_request`, `reply`, `try_reply`, `on_request`,
  `set_default_request_timeout`, `get_default_request_timeout`.
  콜백 버전은 `request_with_callback`, `try_request_with_callback`.
- Go: `Request`, `TryRequest`, `Reply`, `TryReply`, `OnRequest`,
  `SetDefaultRequestTimeout`, `GetDefaultRequestTimeout`.
  콜백 버전은 `RequestAsync`, `TryRequestAsync`.
- Rust: `request`, `try_request`, `reply`, `try_reply`, `on_request`,
  `set_default_request_timeout`, `get_default_request_timeout`.
  timeout 변형은 `request_with_timeout`, `try_request_with_timeout`.
- 그 외: camelCase 동일 (`tryRequest`, `tryReply`)

---

## 12. Testing Policy

### Surface Tests
- Router request-reply capability 존재 확인
- Dealer request capability 존재 확인
- request/reply가 기존 `Received` domain object를 사용하는지 확인
- request 실패 시 기존 `ZlinkError` + errno 사용 확인 (별도 RequestError 없음)

### Contract Tests
- 소켓 close 시 pending request 전부 reject 확인
- request-reply 상태가 소켓 lifecycle에 귀속되는지 확인

### Behavior Tests
- Dealer → Router: request(코루틴) → reply round-trip 성공
- Dealer → Router: request(콜백) → reply round-trip 성공
- Router → Router: request → reply round-trip 성공
- request timeout 동작 확인 (코루틴 버전)
- request timeout 동작 확인 (콜백 버전)
- 다수 concurrent request의 correlation_id 매칭 정확성
- onRequest callback 호출 확인
- DATA 메시지가 기존 recv 경로로 정상 전달되는지 확인

### Ownership Tests
- request 전송 시 message ownership 이동 확인
- reply 전송 시 message ownership 이동 확인
- 수신 Received 해제 확인

### Send / Backpressure Tests
- backpressure 시 writable 대기 후 자동 전송 확인 (EAGAIN이 caller에 노출되지 않음)
- send 대기 중 timeout 초과 시 `ZlinkError(ETIMEDOUT)` 확인
- send 오류 (EAGAIN 외) 시 pending map에서 제거됨 확인
- send 오류 시 코루틴 버전과 콜백 버전 모두 동일한 오류 종류 확인

### Regression Tests

#### Dispatch / Pending Map 회귀

- request timeout 후 뒤늦게 도착한 reply가 discard되는지 확인
  (다음 request의 reply로 잘못 매칭되지 않아야 함)
- 다수 concurrent request 중 일부만 timeout되고 나머지는 정상 resolve 확인
- 소켓 close 시 pending request가 `ZlinkError(ETERM)`으로 reject된 후
  callback/Future가 정상 정리되는지 확인 (메모리 누수 없음)
- request 도중 peer disconnect → reconnect 후 새 request가 정상 동작 확인
- correlation_id counter가 소켓 재사용 시 이전 pending과 충돌하지 않는지 확인
- DATA/REQUEST/REPLY 메시지가 혼재된 상황에서 dispatch가 정확한지 확인
  (DATA는 recv 경로, REQUEST는 onRequest, REPLY는 pending map)
- onRequest handler 내에서 reply를 보내지 않은 경우 request 측에서
  timeout으로 정상 실패하는지 확인
- 콜백 버전에서 callback 내 예외가 소켓 상태를 오염시키지 않는지 확인
- default timeout 변경 후 기존 pending request에 영향 없이
  새 request에만 적용되는지 확인

#### Dispatch와 Envelope Interaction 회귀

> core C API의 envelope 단위/Wire 테스트는
> [`MSG_REQUEST_REPLY_SPEC.md`](../doc/spec/message/MSG_REQUEST_REPLY_SPEC.md)
> 섹션 10,
> [`MSG_METADATA_SPEC.md`](../doc/spec/message/MSG_METADATA_SPEC.md)
> 섹션 13에 정의한다.
> 이 섹션은 바인딩 dispatch 레이어와 envelope이 결합될 때 깨질 수 있는
> 시나리오만 다룬다.

- dispatch owner가 recv한 메시지에서 `get_request_info`로 추출한
  msg_type/correlation_id가 pending map lookup/onRequest 분기에 정확히
  사용되는지 확인 (envelope strip 후 dispatch 로직에 값이 전달되어야 함)
- dispatch 활성 상태에서 DATA 메시지의 payload가 envelope 검사에 의해
  변형되지 않는지 확인 (DATA payload를 envelope로 오인하면 안 됨)
- REQUEST 메시지에 per-message metadata가 동시에 설정된 경우,
  dispatch가 msg_type으로 REQUEST를 정확히 인식하고 metadata도
  사용자에게 전달되는지 확인
- REPLY 메시지에 per-message metadata가 동시에 설정된 경우,
  pending map에서 correlation_id로 정확히 매칭되고 metadata도
  resolve된 결과에 포함되는지 확인
- onRequest handler에 전달된 메시지에서 사용자가 `getMetadata`로
  metadata를 읽을 수 있는지 확인 (dispatch가 metadata를 소비하지 않아야 함)
- multipart REQUEST + metadata → dispatch → onRequest handler에
  part count가 보존되고 metadata도 접근 가능한지 확인
- correlation_id가 0인 REQUEST 메시지가 정상 dispatch되는지 확인
  (0은 DATA의 기본값이지만 msg_type이 REQUEST이면 유효한 request)
- 대량 concurrent request 중 일부가 metadata를 포함하고 일부는 포함하지
  않을 때, pending map 매칭과 metadata 전달이 각각 독립적으로 정확한지 확인

---

## 13. Performance Policy

### 13.1 비용 모델

request-reply 활성화 시 수신 경로에 dispatch overhead가 추가된다.
구현자는 아래 비용 모델을 인지하고, hot path 최적화를 우선해야 한다.

| 경로 | dispatch 비활성 | dispatch 활성 | 추가 비용 |
|------|----------------|--------------|----------|
| DATA recv | C API → 사용자 | C API → envelope 검사 → dispatch → DATA 큐 → 사용자 | envelope 검사 + 큐 hop |
| REPLY recv | — | C API → envelope 파싱 → dispatch → pending map lookup | envelope 파싱 + map lookup |
| REQUEST recv | — | C API → envelope 파싱 → dispatch → onRequest | envelope 파싱 + callback |
| send (DATA) | 변경 없음 | 변경 없음 | 0 |
| request() send | — | async send + pending map insert + timer 등록 | writable 대기 + map insert + timer |
| tryRequest() send | — | trySend + pending map insert + timer 등록 | map insert + timer (writable 대기 없음) |

- **DATA 경로가 가장 중요하다.** 대부분의 메시지가 DATA이므로, dispatch
  활성화 후 DATA 처리량 저하를 최소화해야 한다.
- C API envelope 검사(크기 비교 + magic 비교)는 DATA가 fast path가 되도록
  branch prediction friendly하게 구현한다 (DATA가 대부분 → 검사 실패가 fast path).

### 13.2 DATA 큐 정책

dispatch owner가 DATA 메시지를 넣는 내부 큐의 정책:

- **unbounded 큐를 기본**으로 한다. 소켓 자체의 HWM이 상위 backpressure를
  제공하므로, 큐가 무한 성장하려면 소켓 HWM이 먼저 작동한다.
- 다만 dispatch owner가 recv를 독점하므로, REPLY/REQUEST 처리가 느리면
  DATA 큐에 메시지가 쌓일 수 있다. 이 경우에도 소켓 HWM이 최종
  backpressure 역할을 한다.
- 바인딩이 bounded 큐를 선택할 경우:
  - 큐가 가득 차면 dispatch owner가 recv를 멈추고 소켓 레벨 backpressure를
    자연스럽게 발동시킨다.
  - bounded 크기는 소켓 옵션으로 설정 가능하게 한다.

### 13.3 Timeout 관리: Timer Wheel 권장

concurrent request가 대량(수천~수만)일 때 per-request timer는 비용이 크다.

| 방식 | timer 수 | 정밀도 | 비용 |
|------|---------|--------|------|
| per-request timer | N개 | 정확 | timer 객체 N개 할당, 취소 비용 |
| timer wheel | 1개 | tick 단위 (e.g. 100ms) | 고정 비용, 대량 request에 유리 |

- **timer wheel 패턴을 권장**한다. 하나의 wheel이 모든 pending request의
  timeout을 관리한다.
- tick 간격은 timeout 정밀도 요구에 맞게 설정한다 (기본 100ms 권장).
- per-request timer도 허용하지만, concurrent request가 1,000개를 초과하는
  시나리오에서는 timer wheel이 우선이다.
- 언어별 timer wheel 구현 참고:
  - Java: `HashedWheelTimer` (Netty)
  - Go: `time.AfterFunc`는 runtime이 내부적으로 heap 관리
  - Node: libuv timer는 내부적으로 min-heap
  - Rust: `tokio::time`은 내부적으로 hierarchical wheel

### 13.4 Pending Map 구현 지침

pending map은 request/reply hot path에서 접근되므로 경합을 최소화해야 한다.

| 경합 | 스레드 | 빈도 |
|------|--------|------|
| insert | request 호출 스레드 | 매 request |
| lookup + remove | dispatch 스레드 | 매 REPLY 수신 |
| remove | timer 스레드 | timeout 시 |

권장 구현:
- **lock-free map** 또는 **sharded lock map**을 기본으로 한다.
- 단일 mutex + HashMap은 concurrent request가 100개 미만일 때만 허용한다.
- 언어별 권장:
  - Java: `ConcurrentHashMap` (lock striping 내장)
  - C++: `tbb::concurrent_hash_map` 또는 sharded `std::mutex` + `std::unordered_map`
  - Go: `sync.Map` 또는 sharded lock map
  - Rust: `DashMap` (sharded RwLock 내장)
  - Node: 일반 `Map` (단일 스레드)
  - Python: 일반 `dict` (asyncio 단일 루프)
  - .NET: `ConcurrentDictionary`

### 13.5 객체 할당 비용

매 request마다 할당되는 객체:
- Future/Promise/CompletableFuture (1개)
- pending map entry (1개)
- timer 또는 timer wheel slot (1개)

고처리량 시나리오에서 GC 압력이 될 수 있다. 바인딩은 다음을 고려한다:
- Java: `CompletableFuture`는 경량이므로 일반적으로 허용 가능
- .NET: `ValueTask<Received>`를 사용하면 heap 할당을 줄일 수 있다
- C++/Rust: stack 기반 coroutine/future는 할당 없음
- object pool은 복잡도 대비 효과가 낮으므로 권장하지 않는다.
  필요 시 바인딩별 판단.

### 13.6 벤치마크 항목

request-reply 구현의 성능 검증을 위해 아래 벤치마크를 측정한다:

| 벤치마크 | 측정 대상 | 기준 |
|---------|----------|------|
| DATA throughput (dispatch 비활성) | 순수 DATA send/recv 처리량 | baseline |
| DATA throughput (dispatch 활성) | dispatch 활성 상태에서 DATA 처리량 | baseline 대비 저하율 < 5% 목표 |
| request-reply latency | request → reply round-trip 지연 | p50, p99 |
| request-reply throughput | 초당 완료 request 수 | 언어별 기준 |
| concurrent request scalability | pending request 1K/10K/100K에서 처리량 변화 | linear degradation 허용 |
| timeout overhead | pending 10K에서 timeout 처리 비용 | timer wheel vs per-request 비교 |
| pending map contention | multi-thread request + reply에서 map 경합 | lock-free vs mutex 비교 |

- DATA throughput 저하율 5% 이내를 목표로 한다. 이를 초과하면 dispatch
  구현을 최적화한다.
- 벤치마크는 `core/perf` 패턴에 request-reply 시나리오를 추가하여
  `doc/perf` 정책을 따른다.

---

## 14. Sample Policy

최소 canonical sample:

| Sample | 설명 |
|--------|------|
| `request_reply_sample` | Dealer → Router request-reply 기본 패턴 |
| `request_reply_router_sample` | Router → Router 양방향 request-reply |
| `request_reply_callback_sample` | onRequest callback 패턴 |

---

## 15. Capability 요약

```
RouterSocket + request-reply 확장
  ├── request(routingId, msg, [timeout])              → async send + 코루틴 대기 → Received
  ├── request(routingId, msg, callback, [timeout])    → async send + callback
  ├── tryRequest(routingId, msg, [timeout])            → non-blocking send + 코루틴 대기 → Received
  ├── tryRequest(routingId, msg, callback, [timeout])  → non-blocking send + callback
  ├── reply(routingId, correlationId, msg)             → async send
  ├── tryReply(routingId, correlationId, msg)           → non-blocking send → SendResult
  ├── onRequest(handler)
  └── 기존 Router 기능 그대로 유지

DealerSocket + request 확장
  ├── request(msg, [timeout])              → async send + 코루틴 대기 → Received
  ├── request(msg, callback, [timeout])    → async send + callback
  ├── tryRequest(msg, [timeout])            → non-blocking send + 코루틴 대기 → Received
  ├── tryRequest(msg, callback, [timeout])  → non-blocking send + callback
  └── 기존 Dealer 기능 그대로 유지
  ⚠ 연결 대상이 모두 Router여야 함
```
