# ZMP Protocol Overview

> **상태**: In Progress
> 이 문서는 현재 개발 라운드에서 구현 기준으로 쓰는 작업 스펙이다.
> 구현과 테스트가 끝난 뒤 공개 API 기준은 `doc/api` 문서에 반영한다.
> **관련 문서**:
> [`ZMP_REQUEST_REPLY_PROTOCOL.md`](ZMP_REQUEST_REPLY_PROTOCOL.md) — request-reply 상위 프로토콜
> [`ZMP_SPOT_ROUTED_PROTOCOL.md`](ZMP_SPOT_ROUTED_PROTOCOL.md) — SPOT 직접 전달 상위 프로토콜

---

## 목적

이 문서는 저장소 안의 여러 상위 문서가 공통으로 기대하는
`ZMP` 전송 형식을 한 곳에 정리한다.

여기서 다루는 범위는 다음과 같다.

- `ZMP` frame header 기본 구조
- flags 비트 의미
- `ROUTER` 계열에서의 `routing_id` 위치
- multipart 메시지의 기본 의미
- extended header 사용 위치

여기서 다루지 않는 범위는 다음과 같다.

- request-reply 같은 상위 프로토콜 의미
- SPOT routed envelope 같은 상위 프로토콜 의미
- discovery, registry, handshake 정책

상위 문서는 이 문서를 공통 전제로 참조하고,
자기 프로토콜 의미는 그 위에서 따로 정의한다.

---

## ZMP Frame Header

`ZMP` 는 frame header 를 가진다.

기본 형식:

```text
ZMP Frame Header (8 bytes)
[magic: 0x5A] [version: 0x01] [flags: 1 byte] [size: ...]
```

의미:

- `magic` 은 `ZMP` frame 식별자다
- `version` 은 frame 형식 버전이다
- `flags` 는 frame 성격을 나타낸다
- `size` 는 뒤에 오는 frame payload 크기를 나타낸다

이 문서에서 말하는 상위 프로토콜은
모두 이 `ZMP` frame 위에서 동작한다.

---

## Flags

현재 공통으로 전제하는 flags 의미는 아래와 같다.

```text
bit 0 (0x01): MORE            - multipart continues
bit 1 (0x02): CONTROL         - control message
bit 2 (0x04): IDENTITY        - routing_id present
bit 3 (0x08): SUBSCRIBE       - subscribe
bit 4 (0x10): CANCEL          - cancel
bit 5 (0x20): EXTENDED_HEADER - extended header present
bit 6 (0x40): reserved
bit 7 (0x80): reserved
```

규칙:

- `MORE` 가 켜져 있으면 뒤에 같은 logical multipart message 의 다음 part 가 온다
- `IDENTITY` 가 켜져 있으면 `ROUTER` 계열에서 `routing_id` 가 앞에 존재한다
- `EXTENDED_HEADER` 가 켜져 있으면 payload 앞에 extended header 가 존재한다

상위 문서는 필요한 bit 만 골라 참조하면 된다.

### 현재 상위 프로토콜 구분 규칙

현재 채택 방향에서는 request-reply 와 SPOT routed 같은 상위 프로토콜이
첫 envelope part 에 `CONTROL` bit 를 켠다.

이 규칙을 두는 이유는 ordinary payload 의 첫 part 가 우연히 protocol id 값과
같아질 수 있기 때문이다.

정리:

- ordinary payload part 는 protocol id 로 바로 해석하지 않는다
- 상위 프로토콜 envelope 의 첫 part 는 `CONTROL` bit 가 켜져 있어야 한다
- 수신자는 먼저 `CONTROL` bit 로 상위 프로토콜 후보를 좁힌 뒤 protocol id 를 검사한다

---

## ROUTER 와 routing_id

`ROUTER` 계열에서는 transport peer 식별을 위해
wire 앞에 `routing_id` 가 올 수 있다.

개념 형식:

```text
[routing_id (Router only)]
[payload or protocol envelope]
```

중요한 점:

- 이 `routing_id` 는 transport peer identity 다
- application-level source/destination 주소와 같은 값이라고 가정하면 안 된다
- SPOT routed 같은 상위 프로토콜은 자기 source/destination 주소를
  transport `routing_id` 와 별도로 정의할 수 있다

---

## Multipart

`ZMP` 는 multipart 전송을 지원한다.

기본 의미:

- 한 logical message 가 여러 part 로 나뉘어 전송될 수 있다
- 각 part 는 자기 frame header 를 가진다
- `MORE` bit 로 다음 part 존재 여부를 알린다
- 수신자는 part 순서를 보존해서 logical multipart message 로 해석해야 한다

상위 프로토콜 문서가 따로 정하지 않는 한,
multipart 의 각 part 는 순서대로 payload part 로 본다.

상위 프로토콜이 첫 part 앞에 envelope 를 두거나,
특정 위치에 control 정보를 두고 싶다면
그 규칙은 각 프로토콜 문서에서 별도로 정의해야 한다.

---

## Extended Header

`EXTENDED_HEADER` bit 가 켜져 있으면
payload 앞에 extended header 가 존재한다.

개념 형식:

```text
[routing_id if needed]
[extended header if EXTENDED_HEADER]
[payload]
```

규칙:

- extended header 존재 여부는 payload 바이트 패턴이 아니라 flag 로 감지한다
- extended header 는 payload 와 별도 계층이다
- 어떤 내용을 extended header 에 실을지는 상위 문서가 정한다
- 상위 프로토콜을 payload/protocol envelope 로 둘지,
  extended header 로 둘지는 각 스펙이 따로 정해야 한다

즉 `ZMP` 자체는
"extended header 를 실을 수 있는 자리"만 제공하고,
그 위 의미를 강제하지는 않는다.

### 현재 채택 방향

현재 저장소 기준에서는
request-reply 나 SPOT routed 같은 상위 의미를
`EXTENDED_HEADER` 로 싣는 방향을 채택하지 않는다.

정리:

- `EXTENDED_HEADER` bit 와 자리는 공통 `ZMP` 형식 안에 남겨 둘 수 있다
- 하지만 현재 기준 구현에서는 이 자리를 적극 사용하지 않는다
- request-reply 는 `ZMP` transport 위의 request protocol envelope 로 지원한다
- SPOT 직접 전달은 `ZMP` transport 위의 SPOT routed protocol envelope 로 지원한다

즉 현재는 "extended header 기반 확장"보다
"transport 뒤에 상위 protocol envelope 를 두는 방식"을 우선 기준으로 본다.

### 현재 protocol id 할당 표

| 프로토콜 | protocol id | 구분 규칙 |
| --- | --- | --- |
| request-reply | `0x01` | 첫 envelope part 에 `CONTROL` bit 사용 |
| SPOT routed | `0x02` | 첫 envelope part 에 `CONTROL` bit 사용 |

---

## 상위 문서 작성 원칙

다른 스펙 문서는 아래처럼 이 문서를 참조하는 것을 권장한다.

- `ZMP` frame header 와 flags 설명은 여기로 모은다
- `routing_id` transport 의미는 여기 문구를 재사용한다
- request-reply, SPOT routed, metadata 같은 상위 의미는
  각 문서에서 별도로 정의한다

예를 들면:

- request-reply 문서는 "ZMP transport 위의 request-reply envelope" 를 정의한다
- SPOT routed 문서는 "ZMP transport 위의 SPOT routed envelope" 를 정의한다
- metadata 문서는 `zlink_msg_t` 확장안이든 폐기 메모든
  `ZMP` 공통 구조는 이 문서를 참조한다
