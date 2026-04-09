# Socket Request-Reply API Spec

> **상태**: In Progress
> 이 문서는 현재 개발 라운드에서 구현 기준으로 쓰는 작업 스펙이다.
> 구현과 테스트가 끝난 뒤 공개 API 기준은 `doc/api` 문서에 반영한다.
> **관련 문서**:
> [`ZMP_PROTOCOL_OVERVIEW.md`](ZMP_PROTOCOL_OVERVIEW.md) — 공통 ZMP 전송 형식
> [`ZMP_REQUEST_REPLY_PROTOCOL.md`](ZMP_REQUEST_REPLY_PROTOCOL.md) — request-reply protocol envelope
> [`SPOT_ROUTED_MESSAGE_SPEC.md`](SPOT_ROUTED_MESSAGE_SPEC.md) — SPOT 직접 전달 상위 설계

---

## 목적

이 문서는 socket 레벨에서 request-reply 를 어떻게 노출할지 정의한다.

핵심 목표는 다음과 같다.

- 기존 `zlink_msg_t` request marking API 없이도
  request-reply 를 사용할 수 있어야 한다
- `DEALER` 와 `ROUTER` 의 역할 차이를 public API 에서 바로 알 수 있어야 한다
- bindings 가 callback 방식과 coroutine 방식 둘 다 만들기 쉬운 표면을 제공해야 한다
- transport `routing_id` 와 request-reply context 를 섞지 않아야 한다

이 문서는 socket API 표면을 다룬다.
wire 형식 자체는
[`ZMP_REQUEST_REPLY_PROTOCOL.md`](ZMP_REQUEST_REPLY_PROTOCOL.md)
를 따른다.

---

## 기본 원칙

### 1. request-reply 는 socket 전용 API 로 연다

request-reply 는 더 이상 `zlink_msg_t` 에
request 표시를 붙이는 방식으로 열지 않는다.

대신 `DEALER`, `ROUTER` 에
전용 request/reply API 를 둔다.

### 2. `DEALER.request()` 는 아무 peer 에게나 허용하지 않는다

`DEALER.request()` 는
request-reply 프로토콜을 지원하는 `ROUTER` peer 에게만 허용한다.

즉 다음은 허용하지 않는다.

- `DEALER -> DEALER request`
- request-reply capability 가 없는 peer 에 대한 request

`DEALER.request()` 는 호출 시점에 기존 peer 선택 규칙으로 고른 peer 의
종류와 request-reply capability 를 확인해야 한다.
선택된 peer 가 `ROUTER` 가 아니거나
request-reply capability 가 없으면 즉시 `EOPNOTSUPP` 로 실패한다.

`DEALER.request()` 는 기존 `DEALER` 송신 경로를 그대로 쓴다.
즉 사용자가 `peer_rid` 를 직접 주지 않고,
기존 peer 선택 규칙으로 요청이 나간다.

그래서 `DEALER.request()` 는
"기존 peer 선택 결과가 request-reply 지원 `ROUTER` 여야 한다"는
호출 조건을 가진다.

### 3. reply 는 상대 주소와 request seq 기준으로 보낸다

reply 는 받은 request 의 `peer_rid` 와 `request_seq` 를 기준으로 보낸다.

이렇게 해야 다음 문제가 줄어든다.

- transport `routing_id` 와 request seq 혼동
- 잘못된 peer 로 reply 하는 실수
- bindings 마다 reply 경로를 다시 조합하는 중복

### 4. callback 기반 비동기 모델을 기준으로 둔다

core C API 는 callback 기반 비동기 응답 모델을 기준으로 둔다.

이 기준을 쓰면 bindings 는 다음 둘 다 만들기 쉽다.

- callback API
- coroutine / future / promise 기반 API

즉 core 는 "요청 1건을 보내고, 나중에 응답 callback 을 받는 모델"을 제공하고,
bindings 가 그 위에 더 높은 수준의 async 표면을 얹는다.

### 5. 여러 request 를 동시에 보낼 수 있어야 한다

이 모델은 HTTP 처럼
"응답을 받은 뒤 다음 request 를 보낸다"를 전제로 하지 않는다.

같은 `DEALER` 또는 `ROUTER` 소켓에서
이전 request 의 응답을 기다리지 않고
여러 request 를 연속으로 보낼 수 있어야 한다.

즉
여러 in-flight request 를 request seq 로 구분한다.

reply 는 송신 순서대로 올 필요가 없다.
각 request 는 자기 request seq 와 일치하는 reply 로 완료된다.

### 6. timeout 은 필수다

request 는 응답이 오지 않을 수 있으므로 timeout 이 반드시 있어야 한다.

- socket default timeout 을 둘 수 있다
- 각 request 는 per-call timeout override 를 줄 수 있다
- per-call timeout 이 없으면 socket default timeout 을 쓴다
- 둘 다 없으면 구현 기본 timeout `5000ms` 를 쓴다

---

## 역할별 지원 범위

### `DEALER`

`DEALER` 는 request 시작점으로 쓴다.

허용:

- `dealer.request(...)`

허용하지 않음:

- `dealer.reply(...)`

제약:

- 대상 peer 는 request-reply 지원 `ROUTER` 여야 한다
- 다른 종류의 peer 로 보내지면 즉시 실패하거나 timeout 으로 끝날 수 있다

### `ROUTER`

`ROUTER` 는 서버와 능동 client 양쪽 역할을 가질 수 있다.

허용:

- `router.request(...)`
- `router.reply(...)`
- `router.request_handler(...)`

설명:

- `router.request(...)` 는 `ROUTER` 가 능동적으로 다른 request-reply 지원 peer 에
  요청을 보낼 때 쓴다
- `router.reply(...)` 는 받은 request 에 답할 때 쓴다
- `router.request_handler(...)` 는 request 수신 callback 등록에 쓴다

---

## 공개 API

### 공통 타입

```c
typedef void (*zlink_reply_handler_fn)(
    int errno,
    zlink_msg_t *parts,
    size_t part_count,
    void *userdata);

typedef void (*zlink_router_request_handler_fn)(
    const zlink_routing_id_t *peer_rid,
    uint64_t request_seq,
    zlink_msg_t *parts,
    size_t part_count,
    void *userdata);
```

설명:

- request 수신 handler 는
  받은 request 의 reply 주소 정보와 payload 를 함께 받는다

수명 규칙:

- callback 또는 request handler 로 전달된 `parts` 는 borrowed view 다
- `parts` 와 각 `zlink_msg_t` 는 callback 또는 handler 반환 시점까지만 유효하다
- callback 밖에서 payload 를 유지하려면 사용자가 복사해야 한다
- request handler 로 전달된 `peer_rid` 는 borrowed view 다
- callback 밖에서 `peer_rid` 를 유지하려면 사용자가 복사해야 한다

### timeout 설정

```c
typedef enum zlink_dealer_option_t {
    ...
    ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS = ...,
} zlink_dealer_option_t;

typedef enum zlink_router_option_t {
    ...
    ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS = ...,
} zlink_router_option_t;
```

규칙:

- `DEALER` 는 `zlink_set_dealer_option()` 으로 request timeout 기본값을 설정한다
- `ROUTER` 는 `zlink_set_router_option()` 으로 request timeout 기본값을 설정한다
- 별도 설정이 없으면 request timeout 기본값은 `5000ms` 다
- `timeout_ms = 0` 이면 구현 기본 timeout `5000ms` 를 사용한다
- 이 값은 per-call timeout 을 주지 않았을 때만 적용한다

예:

```c
uint32_t timeout_ms = 1000;

zlink_set_dealer_option(
    dealer,
    ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS,
    &timeout_ms,
    sizeof(timeout_ms));

zlink_set_router_option(
    router,
    ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS,
    &timeout_ms,
    sizeof(timeout_ms));
```

### `DEALER.request`

```c
int zlink_dealer_request(
    void *dealer,
    zlink_msg_t *parts,
    size_t part_count,
    uint32_t timeout_ms,
    zlink_reply_handler_fn handler,
    void *userdata);
```

규칙:

- `dealer` 는 `DEALER` 소켓이어야 한다
- `DEALER` 는 기존 peer 선택 규칙으로 대상 peer 를 고른다
- `handler` 는 응답 또는 실패를 비동기로 받는다
- reply 는 최대 한 번만 callback 으로 전달한다
- `timeout_ms = 0` 이면 socket default timeout 을 쓴다
- 다른 in-flight request 가 있어도 새 request 를 막지 않는다

실패:

- 인자가 잘못되면 즉시 실패한다
- 연결된 peer 가 하나도 없으면 즉시 실패한다
- 선택된 peer 가 `ROUTER` 가 아니거나 request-reply capability 가 없으면 즉시 `EOPNOTSUPP` 로 실패한다

### `ROUTER.request`

```c
int zlink_router_request(
    void *router,
    const zlink_routing_id_t *peer_rid,
    zlink_msg_t *parts,
    size_t part_count,
    uint32_t timeout_ms,
    zlink_reply_handler_fn handler,
    void *userdata);
```

규칙:

- `router` 는 `ROUTER` 소켓이어야 한다
- 대상 peer 는 request-reply 지원 peer 여야 한다
- 비동기 응답 모델은 `dealer_request()` 와 같다
- `peer_rid` 는 항상 명시적으로 지정한다
- `timeout_ms = 0` 이면 socket default timeout 을 쓴다
- 다른 in-flight request 가 있어도 새 request 를 막지 않는다

### `ROUTER.reply`

```c
int zlink_router_reply(
    void *router,
    const zlink_routing_id_t *peer_rid,
    uint64_t request_seq,
    zlink_msg_t *parts,
    size_t part_count);
```

규칙:

- `peer_rid` 와 `request_seq` 는 기존 request handler 에서 받은 값이어야 한다
- 여러 reply 를 허용하더라도 각 reply 는 같은 reply 경로와 같은 request seq 의미를 공유한다

### `ROUTER -> SPOT` 송신

일반 `ROUTER` 에서 `SpotNode` 안의 target `Spot` 으로 직접 보내는 socket 계열 API 는
아래처럼 본다.

```c
int zlink_router_send_spot(
    void *router,
    const zlink_routing_id_t *dest_node_rid,
    const zlink_routing_id_t *dest_spot_rid,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_send_flags_t flags);
```

규칙:

- `router` 는 generic `ROUTER` 소켓이어야 한다
- 목적지는 `dest_node_rid + dest_spot_rid` 로 지정한다
- 이 함수는 `router -> spot` 직접 전달용이며 request-reply 전용 함수는 아니다
- 수신한 `Spot` 쪽에서는 ordinary routed recv 또는 request-reply 수신 규칙에 따라 해석한다

### `ROUTER.request_handler`

```c
int zlink_router_request_handler(
    void *router,
    zlink_router_request_handler_fn handler,
    void *userdata);
```

규칙:

- `ROUTER` 가 request-reply protocol envelope 를 받은 경우에만 이 handler 로 보낸다
- ordinary `ROUTER` traffic 는 이 handler 로 보내지 않는다
- request 수신과 ordinary raw recv 표면의 관계는 별도 충돌 규칙으로 정해야 한다

---

## peer 제약과 capability

request 가능 여부는 소켓 타입 이름만으로 다 정하지 않는다.
실제 판단은 peer capability 와 peer 종류를 함께 봐야 한다.

최소 요구사항:

- peer 가 `ROUTER` 인지 구분할 수 있어야 한다
- peer 가 request-reply protocol 을 지원하는지 알 수 있어야 한다
- `DEALER.request()` 는 연결 대상이 모두 이 두 조건을 만족할 때만 안전하다
- `ROUTER.request()` 는 `peer_rid` 기준으로 capability 판단을 수행한다

즉 `DEALER` 라는 소켓 타입이 request 시작점이라는 뜻이지,
아무 peer 에게나 request 를 보낼 수 있다는 뜻은 아니다.

---

## 오류 처리 규칙

### 즉시 실패

다음 경우는 API 호출 시 즉시 실패해야 한다.

- handle 이 NULL 이거나 소켓 타입이 맞지 않는 경우
- `DEALER.request()` 에서 선택된 peer 가 `ROUTER` 가 아니거나 request-reply capability 가 없는 경우
- `ROUTER.request()` 에서 `peer_rid` 가 비어 있거나 잘못된 경우
- `ROUTER.request()` 대상 peer 에 request-reply capability 가 없는 경우
- `reply()` 에 유효하지 않은 `peer_rid` 또는 `request_seq` 를 준 경우

권장 errno:

- `EINVAL`: 인자 오류
- `EOPNOTSUPP`: peer 종류 또는 capability 가 맞지 않음
- `ENOENT`: 대상 peer 를 찾지 못함

### 비동기 실패

다음 경우는 request 는 접수되었지만
응답 callback 으로 실패를 돌려줄 수 있다.

- timeout
- peer disconnect
- protocol parse failure
- remote peer 가 보낸 protocol-level error reply
- late reply 무시

이 영역의 세부 정책은 bindings 또는 상위 레이어가 정할 수 있다.
다만 core 는 callback 에 `errno` 를 통해 실패를 알릴 수 있어야 한다.

설명:

- `errno = 0` 이면 성공 reply 다
- `errno != 0` 이면 timeout, disconnect, protocol 오류, remote error reply 같은 실패다

추가 규칙:

- API 호출이 즉시 실패한 경우에는 reply callback 을 호출하지 않는다
- API 호출이 성공적으로 접수된 경우에는 completion callback 이 정확히 한 번 온다

---

## callback 과 coroutine 관계

이 문서가 callback 모델을 기준으로 두는 이유는
bindings 확장이 쉽기 때문이다.

예:

- C binding: callback 그대로 노출
- Python/Kotlin/JS binding: callback 을 future/promise 로 감싸기
- coroutine binding: callback completion 을 suspend/resume 로 연결

즉 core 는 callback 기반 비동기 API 하나를 제공하고,
bindings 가 그 위에서 coroutine 버전을 구성하는 구조를 권장한다.

---

## request 수신과 raw recv 관계

`ROUTER` 는 기존 raw recv 표면도 가질 수 있다.
따라서 request handler 와 raw recv 사이 충돌 규칙을 정해야 한다.

규칙:

- 같은 request-reply 수신 표면에서는
  `request_handler` 와 raw recv 를 동시에 허용하지 않는다
- ordinary raw traffic 와 request-reply traffic 는
  protocol envelope 로 구분한다
- 충돌 시 `EBUSY` 를 사용한다

이 규칙은 SPOT routed recv 와 비슷하게
"한 수신 표면에는 한 모델만 활성화" 원칙으로 설명할 수 있다.

---

## bindings 관점 정리

bindings 가 기대할 수 있는 최소 계약은 다음과 같다.

- request 는 callback 기반으로 보낸다
- timeout 은 socket default 와 per-call override 둘 다 지원한다
- default request timeout 은 각 socket 전용 option 으로 설정한다
- 같은 소켓에서 여러 request 를 동시에 outstanding 상태로 둘 수 있다
- 응답은 최대 한 번 callback 으로 온다
- reply 는 `peer_rid + request_seq` 기준으로 보낸다
- `DEALER -> DEALER request` 는 지원하지 않는다
- `DEALER` 가 선택한 peer 가 `ROUTER` 가 아니거나 request-reply capability 가 없으면 즉시 `EOPNOTSUPP` 로 실패한다

설명:

- low-level `router_reply(peer_rid, request_seq, ...)` 는 여러 번 호출할 수 있다
- `request()` 완료 callback 은 첫 번째 reply 로 완료된다
- 그 뒤 reply 를 별도 public 표면으로 노출할지는 추가 설계가 필요하다
- reply 도착 순서는 request 송신 순서와 같을 필요가 없다
- 즉 server 측 low-level reply 다중 호출과 requester 측 callback 1회 완료는 서로 다른 계약이다

이 계약이면 bindings 는
callback API 와 coroutine API 를 모두 무리 없이 제공할 수 있다.

---

## 구현 중 정리할 항목

- capability 광고 위치와 필드 이름

---

## 회귀 테스트 기준

이 문서 기준 구현은 아래 항목을 회귀 테스트로 고정해야 한다.

- `DEALER -> ROUTER request/reply`
- `ROUTER -> ROUTER request/reply`
- 같은 소켓에서 여러 request 를 동시에 outstanding 상태로 둘 수 있어야 한다
- reply 도착 순서가 request 송신 순서와 달라도 올바른 callback 이 완료되어야 한다
- `DEALER.request()` 는 `peer_rid` 없이 동작해야 한다
- `ROUTER.request()` 는 `peer_rid` 기준으로 대상 peer 를 골라야 한다
- `DEALER -> DEALER request` 는 비지원이어야 한다
- 잘못된 대상 peer 에 대한 request 는 즉시 `EOPNOTSUPP` 로 실패해야 한다
- 첫 reply 이후의 extra reply 는 high-level callback 을 다시 완료시키지 않아야 한다
- request handler 와 raw recv 를 동시에 켰을 때 충돌 규칙이 문서와 같아야 한다
