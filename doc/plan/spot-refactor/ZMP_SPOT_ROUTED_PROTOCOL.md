# ZMP SPOT Routed Protocol

> **상태**: In Progress
> 이 문서는 현재 개발 라운드에서 구현 기준으로 쓰는 작업 스펙이다.
> 구현과 테스트가 끝난 뒤 공개 API 기준은 `doc/api` 문서에 반영한다.
> **관련 문서**:
> [`ZMP_PROTOCOL_OVERVIEW.md`](ZMP_PROTOCOL_OVERVIEW.md) — 공통 ZMP 전송 형식
> [`ZMP_REQUEST_REPLY_PROTOCOL.md`](ZMP_REQUEST_REPLY_PROTOCOL.md) — request-reply protocol envelope
> [`SPOT_ROUTED_MESSAGE_SPEC.md`](SPOT_ROUTED_MESSAGE_SPEC.md) — SPOT 직접 전달 상위 설계

---

## 목적

이 문서는 SPOT 직접 전달을
`zlink_msg_t` 내부 필드나 message header 확장이 아니라
`ZMP` transport 위의 상위 protocol envelope 로 처리하는 기준을 정의한다.

이 문서가 정의하는 범위:

- SPOT routed protocol envelope 형식
- `spot -> spot`, `spot -> router`, `router -> spot` 해석 규칙
- source/destination 주소 필드 의미
- payload 시작 위치
- transport `routing_id` 와 application-level 주소 구분

이 문서가 정의하지 않는 범위:

- `SpotNode` 로컬 디렉터리 구성
- discovery/registry 조회 정책
- recv queue, callback thread, dispatcher 정책

이런 동작은
[`SPOT_ROUTED_MESSAGE_SPEC.md`](SPOT_ROUTED_MESSAGE_SPEC.md)
가 다룬다.

---

## 기본 원칙

- SPOT 직접 전달 정보는 `message` 레벨이 아니라 protocol 레벨에서 정의한다
- 공통 transport 형식은
  [`ZMP_PROTOCOL_OVERVIEW.md`](ZMP_PROTOCOL_OVERVIEW.md) 를 따른다
- transport `routing_id` 와 SPOT source/destination 주소는 다른 계층이다
- payload 는 protocol envelope 뒤에 온다
- request-reply 가 함께 쓰일 때는 SPOT routed envelope 뒤에 request-reply envelope 가 온다

---

## Envelope 형식

SPOT 직접 전달 메시지는 transport 정보 뒤에
SPOT routed protocol envelope 를 둔다.

이 문서에서 envelope 는
"payload 바이트 안의 내부 필드"가 아니라
"payload 앞쪽에 오는 고정 순서의 ZMP multipart control part 들"을 뜻한다.

즉 받는 쪽은 transport envelope 뒤의 첫 몇 개 part 를
SPOT routed control part 로 읽고,
그 뒤 part 부터를 application payload 로 해석한다.

개념 형식:

```text
[router transport envelope if needed]
[spot routed protocol id]
[spot routed protocol version]
[source class]
[source node rid or empty]
[source spot rid or source router rid or empty]
[destination class]
[destination node rid or empty]
[destination spot rid or destination router rid]
[payload part 0]
[payload part 1]
...
```

request-reply 를 함께 쓰는 개념 형식:

```text
[router transport envelope if needed]
[spot routed protocol envelope]
[request-reply protocol envelope]
[payload]
```

part 단위 규칙:

- `spot routed protocol id` 는 별도 part 1개다
- `spot routed protocol version` 도 별도 part 1개다
- `source class` 도 별도 part 1개다
- `source node rid` 도 별도 part 1개다
- `source spot rid` 또는 `source router rid` 도 별도 part 1개다
- `destination class` 도 별도 part 1개다
- `destination node rid` 도 별도 part 1개다
- `destination spot rid` 또는 `destination router rid` 도 별도 part 1개다
- 그 뒤 part 들은 모두 application payload 다
- `spot routed protocol id` part 는 `CONTROL` bit 가 켜진 part 여야 한다

설명:

- `protocol id` 는 이 메시지가 SPOT routed envelope 를 쓴다는 뜻이다
- `version` 은 envelope 형식 버전이다
- `source class` 와 `destination class` 는 송신자/목적지 종류를 나타낸다
- source/destination rid 필드는 주소 해석에 쓴다
- 그 뒤 part 들은 application payload 다
- 수신자는 먼저 `CONTROL` bit 가 켜진 envelope 후보인지 확인하고,
  그 다음 protocol id 를 검사한다

---

## 필드 의미

### protocol id

- 일반 payload 와 쉽게 구분되어야 한다
- 다른 상위 프로토콜과 충돌하지 않아야 한다

```text
"\x02"
```

이 값은 ordinary payload 와 바로 비교하지 않는다.
반드시 `CONTROL` bit 가 켜진 envelope 후보에서만 검사한다.

### version

- envelope 형식 버전이다
- 받는 쪽은 버전이 맞지 않으면 메시지를 버릴 수 있어야 한다
- 값은 `"\x01"` 이다

### source class / destination class

최소한 아래 둘을 구분해야 한다.

- `0x01 = spot`
- `0x02 = router`

### source node rid

- `source class = spot` 에서만 사용한다
- 보내는 `SpotNode` 의 application-level 주소다
- raw bytes part 로 싣는다
- 값이 없을 때는 part 를 생략하지 않고 길이 0 part 로 둔다

### source spot rid

- `source class = spot` 에서만 사용한다
- 보내는 `Spot` 의 application-level 주소다
- raw bytes part 로 싣는다
- 값이 없을 때는 part 를 생략하지 않고 길이 0 part 로 둔다

### destination node rid

- `destination class = spot` 에서만 사용한다
- 목적지 `SpotNode` 주소다
- raw bytes part 로 싣는다
- 값이 없을 때는 part 를 생략하지 않고 길이 0 part 로 둔다

### destination spot rid / destination router rid

- `destination class = spot` 이면 목적지 `Spot` 주소다
- `destination class = router` 이면 목적지 일반 `ROUTER` peer 주소다
- raw bytes part 로 싣는다
- 값이 없을 때는 part 를 생략하지 않고 길이 0 part 로 둔다

---

## 방향별 의미

### `spot -> spot`

- `source class = spot`
- `source node rid = 보내는 SpotNode`
- `source spot rid = 보내는 Spot`
- `destination class = spot`
- `destination node rid = 받는 SpotNode`
- `destination spot rid = 받는 Spot`

### `spot -> router`

- `source class = spot`
- `source node rid = 보내는 SpotNode`
- `source spot rid = 보내는 Spot`
- `destination class = router`
- `destination node rid = empty`
- `destination router rid = 받는 일반 ROUTER peer`

이 경우 일반 `ROUTER` 는
기존 recv 표면으로 메시지를 받은 뒤
envelope 에서 `source node rid`, `source spot rid` 를 읽어
reply 또는 follow-up `router -> spot` 송신에 사용할 수 있어야 한다.

### `router -> spot`

- `source class = router`
- `source node rid = empty`
- `source spot rid = empty`
- `destination class = spot`
- `destination node rid = 받는 SpotNode`
- `destination spot rid = 받는 Spot`

이 경우 보내는 쪽 일반 `ROUTER` 의 transport peer identity 는
envelope source 주소와 별개로 다룬다.

---

## Multipart 규칙

- envelope 는 payload 앞에 한 번만 온다
- 뒤에 오는 payload part 들은 순서대로 application payload 로 본다
- source/destination 의미는 logical message 전체에 적용된다
- control part 개수는 protocol 버전별로 고정되어 있어야 한다

즉 SPOT routed 정보는 payload 각 part 속성이 아니라
logical message 전체 속성이다.

즉
"transport 뒤 8개 control part + 나머지 payload parts" 로 이해하면 된다.

---

## ROUTER 와의 관계

`ROUTER` 계열에서는 transport `routing_id` 가 앞에 올 수 있다.

중요한 점:

- transport `routing_id` 는 peer identity 다
- source/destination node/spot 주소는 application-level routed 주소다
- 둘은 의미가 다르므로 같은 값처럼 다루면 안 된다

개념 형식:

```text
[router transport routing_id]
[spot routed protocol envelope]
[payload]
```

---

## 유효성 검사

받는 쪽은 최소한 아래를 검사해야 한다.

- protocol id 가 맞는지
- protocol version 이 맞는지
- source/destination class 조합이 유효한지
- `spot` 목적지일 때 `destination node rid`, `destination spot rid` 가 모두 있는지
- `router` 목적지일 때 `destination node rid` 가 비어 있는지

추가로 `spot -> spot` 은 다음도 검사해야 한다.

- source node rid 가 peer metadata 와 일치하는지
- source spot rid 가 그 node 가 광고한 spot 목록 안에 있는지

위 조건을 어기면 메시지를 버리고,
오류 카운터를 올릴 수 있어야 한다.

---

## 지원 방향

현재 기준에서 SPOT 직접 전달은 아래처럼 지원한다.

- 기존 `ROUTER` recv/send 표면은 유지한다
- routed 여부 판단은 SPOT routed protocol envelope 를 읽어 수행한다
- local handoff 와 remote handoff 는 같은 envelope 의미를 공유한다

즉 core `message` 계층은 payload 컨테이너 역할에 집중하고,
SPOT 직접 전달 의미는 이 protocol 문서와
SPOT 상위 설계 문서가 함께 정의한다.

---

## 구현 규칙

- source/destination rid 길이 제한은 별도 구현 제약으로 정한다
- payload 시작 위치는 transport 뒤 8개 control part 다음으로 본다
- generic `ROUTER` helper API 는 이번 라운드에서 추가하지 않는다

---

## 회귀 테스트 기준

이 문서 기준 구현은 아래 항목을 회귀 테스트로 고정해야 한다.

- `spot -> spot` 에서 source/destination 주소가 올바르게 encode/decode 되어야 한다
- `spot -> router` 에서 일반 `ROUTER` 가 source node rid 와 source spot rid 를 읽을 수 있어야 한다
- `router -> spot` 에서 destination node rid 와 destination spot rid 가 올바르게 해석되어야 한다
- `router` 목적지일 때 `destination node rid` 가 비어 있지 않으면 거부해야 한다
- `spot` 목적지일 때 destination node rid 와 destination spot rid 가 모두 있어야 한다
- source/destination class 조합이 잘못되면 메시지를 버려야 한다
- request-reply 를 함께 쓸 때는 `SPOT routed envelope -> request-reply envelope -> payload` 순서가 유지되어야 한다
- `CONTROL` bit 가 없는 ordinary payload part 는 SPOT routed envelope 로 해석하지 않아야 한다
