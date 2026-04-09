# ZMP Request-Reply Protocol

> **상태**: In Progress
> 이 문서는 현재 개발 라운드에서 구현 기준으로 쓰는 작업 스펙이다.
> 구현과 테스트가 끝난 뒤 공개 API 기준은 `doc/api` 문서에 반영한다.
> **관련 문서**:
> [`ZMP_PROTOCOL_OVERVIEW.md`](ZMP_PROTOCOL_OVERVIEW.md) — 공통 ZMP 전송 형식
> [`SOCKET_REQUEST_REPLY_API_SPEC.md`](SOCKET_REQUEST_REPLY_API_SPEC.md) — socket 레벨 request-reply API

---

## 목적

이 문서는 request-reply 를
`zlink_msg_t` 내부 필드가 아니라
`ZMP` transport 위의 상위 프로토콜 envelope 로 처리하는 기준을 정리한다.

이 문서가 정의하는 범위:

- request-reply protocol envelope 형식
- request 와 reply 구분 방식
- request seq 표현 방식
- multipart payload 와 envelope 경계
- `ROUTER` transport `routing_id` 와 request-reply 의미 구분

이 문서가 정의하지 않는 범위:

- pending map
- request lifecycle 관리
- application dispatch 정책

이런 로직은 바인딩 또는 상위 레이어가 담당한다.
socket 공개 표면은
[`SOCKET_REQUEST_REPLY_API_SPEC.md`](SOCKET_REQUEST_REPLY_API_SPEC.md)
에서 다룬다.

---

## 기본 원칙

- request-reply 는 `message` 레벨이 아니라 protocol 레벨에서 정의한다
- 공통 transport 형식은
  [`ZMP_PROTOCOL_OVERVIEW.md`](ZMP_PROTOCOL_OVERVIEW.md) 를 따른다
- envelope 는 payload 와 구분되는 상위 프로토콜 정보다
- multipart 메시지에서도 request seq 의미는 logical message 전체에 적용된다

---

## Envelope 형식

request-reply 메시지는 transport 정보 뒤에
request-reply protocol envelope 를 둔다.

이 문서에서 envelope 는
"payload 바이트 안의 내부 필드"가 아니라
"payload 앞쪽에 오는 고정 순서의 ZMP multipart control part 들"을 뜻한다.

즉 받는 쪽은 transport envelope 뒤의 첫 몇 개 part 를
request-reply control part 로 읽고,
그 뒤 part 부터를 application payload 로 해석한다.

개념 형식:

```text
[router transport envelope if needed]
[request-reply protocol id]
[request-reply protocol version]
[message type]
[request seq]
[payload part 0]
[payload part 1]
...
```

part 단위 규칙:

- `request-reply protocol id` 는 별도 part 1개다
- `request-reply protocol version` 도 별도 part 1개다
- `message type` 도 별도 part 1개다
- `request seq` 도 별도 part 1개다
- 그 뒤 part 들은 모두 application payload 다
- `request-reply protocol id` part 는 `CONTROL` bit 가 켜진 part 여야 한다

설명:

- `request-reply protocol id` 는 이 메시지가 request-reply envelope 를 쓴다는 뜻이다
- `request-reply protocol version` 은 형식 버전이다
- `message type` 은 request 인지 reply 인지 나타낸다
- `request seq` 는 request 와 reply 를 연결하는 식별자다
- 그 뒤 part 들은 application payload 다
- 수신자는 먼저 `CONTROL` bit 가 켜진 envelope 후보인지 확인하고,
  그 다음 protocol id 를 검사한다

---

## 필드 의미

### protocol id

- 일반 payload 와 쉽게 구분되어야 한다
- 다른 상위 프로토콜과 충돌하지 않아야 한다

```text
"\x01"
```

이 값은 ordinary payload 와 바로 비교하지 않는다.
반드시 `CONTROL` bit 가 켜진 envelope 후보에서만 검사한다.

### version

- envelope 형식 버전이다
- 받는 쪽은 버전이 맞지 않으면 메시지를 버릴 수 있어야 한다
- 값은 `"\x01"` 이다

### message type

최소한 아래 둘을 구분해야 한다.

- `0x01 = request`
- `0x02 = reply`
- `0x03 = error reply`

### request seq

- request 와 reply 를 연결하는 값이다
- request 와 그에 대한 reply 는 같은 request seq 를 사용한다
- 의미는 transport `routing_id` 와 다르다
- 8바이트 고정 part 와 network byte order 를 쓴다
- 같은 소켓에서 여러 request 가 동시에 outstanding 상태여도
  request seq 로 각각 구분할 수 있어야 한다
- request 송신 측이 locally unique 하게 생성한다
- reply 송신 측은 request 에서 받은 request seq 값을 그대로 다시 실어 보낸다

### error reply

`error reply` 는 request 는 정상적으로 수신했지만
성공 payload 대신 명시적 오류 결과를 돌려줘야 할 때 사용한다.

이 형식은 timeout 과 구분되는 실패를 표현할 때 쓴다.

예:

- 대상 `Spot` 이 존재하지 않음
- request 가 target object 를 찾지 못함
- request 를 처리할 최소 조건이 충족되지 않음

형식 규칙:

- `message type = 0x03`
- `request seq` 는 원 request 와 같은 값을 쓴다
- 첫 번째 payload part 는 4바이트 network byte order `errno` 값이다
- 그 뒤 payload part 는 선택 사항이다

수신 규칙:

- `error reply` 를 받은 쪽은 첫 payload part 의 `errno` 값을 읽어
  해당 request completion 을 실패로 완료한다
- `error reply` 는 ordinary success reply payload 로 전달하지 않는다
- high-level callback 에는 `errno != 0` 형태로 전달한다

---

## Multipart 규칙

- envelope 는 payload 앞에 한 번만 온다
- 뒤에 오는 payload part 들은 순서대로 application payload 로 본다
- multipart 메시지 전체가 하나의 request seq 를 공유한다
- control part 개수는 protocol 버전별로 고정되어 있어야 한다
- reply 도착 순서는 request 송신 순서와 같을 필요가 없다

즉 request-reply 정보는 payload 각 part 속성이 아니라
logical message 전체 속성이다.

예를 들면
"transport 뒤 4개 control part + 나머지 payload parts" 로 이해하면 된다.

---

## ROUTER 와의 관계

`ROUTER` 계열에서는 transport `routing_id` 가 앞에 올 수 있다.

중요한 점:

- transport `routing_id` 는 peer identity 다
- request seq 는 request-reply 매칭용 식별자다
- 둘은 의미가 전혀 다르므로 같은 값처럼 다루면 안 된다

개념 형식:

```text
[router transport routing_id]
[request-reply protocol envelope]
[payload]
```

---

## 지원 방향

현재 기준에서 request-reply 는 아래처럼 지원한다.

- 기존 send/recv 표면은 유지한다
- request/reply 판단은 protocol envelope 를 읽어 수행한다
- request seq 저장과 dispatch 는 상위 레이어가 담당한다
- timeout 정책은 socket API 가 담당한다
- multiple in-flight request 는 request seq 로 구분한다

즉 core `message` 계층은 payload 컨테이너 역할에 집중하고,
request-reply 의미는 이 protocol 문서가 정의한다.

---

## 구현 규칙

- payload 시작 위치는 transport 뒤 4개 control part 다음으로 본다
- `ROUTER` 계열 helper API 는 이번 라운드에서 추가하지 않는다

---

## 회귀 테스트 기준

이 문서 기준 구현은 아래 항목을 회귀 테스트로 고정해야 한다.

- request envelope encode 후 recv 쪽에서 같은 `request seq` 를 읽을 수 있어야 한다
- reply envelope encode 후 recv 쪽에서 같은 `request seq` 를 읽을 수 있어야 한다
- error reply envelope encode 후 recv 쪽에서 같은 `request seq` 와 `errno` 를 읽을 수 있어야 한다
- 같은 소켓에서 여러 request 를 동시에 outstanding 상태로 둘 수 있어야 한다
- reply 도착 순서가 request 송신 순서와 달라도 `request seq` 로 정확히 매칭해야 한다
- payload 가 multipart 여도 control part 와 payload part 경계가 깨지지 않아야 한다
- protocol id 가 틀리면 ordinary payload 로 취급하거나 request-reply 경로에서 제외해야 한다
- `CONTROL` bit 가 없는 ordinary payload part 는 request-reply envelope 로 해석하지 않아야 한다
- version 이 틀리면 메시지를 거부하거나 실패 경로로 보내야 한다
- 같은 `request seq` 의 extra reply 는 첫 reply 뒤에 무시해야 한다
