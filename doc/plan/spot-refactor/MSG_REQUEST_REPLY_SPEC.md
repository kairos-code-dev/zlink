# Message Request-Reply 필드 — Core C API Spec

> **상태**: Historical Draft
> **소비자**: [`bindings/README.md`](../../../bindings/README.md) (Request-Reply Policy 섹션),
> [`bindings/lang/*.md`](../../../bindings/lang/) (언어별 인터페이스)
> **관련 문서**:
> [`MSG_METADATA_SPEC.md`](MSG_METADATA_SPEC.md) — 범용 per-message metadata,
> [`ZMP_PROTOCOL_OVERVIEW.md`](ZMP_PROTOCOL_OVERVIEW.md) — 공통 ZMP 전송 형식,
> [`ZMP_REQUEST_REPLY_PROTOCOL.md`](ZMP_REQUEST_REPLY_PROTOCOL.md) — 현재 request-reply 기준안,
> [`SOCKET_REQUEST_REPLY_API_SPEC.md`](SOCKET_REQUEST_REPLY_API_SPEC.md) — socket 레벨 request-reply API
>
> 이 문서의 설계안은 현재 채택하지 않았다.
> 현재 기준은 message-level field 가 아니라
> ZMP request-reply protocol envelope 와 socket request API 다.
> 이 문서의 `msg_type`, `correlation_id`, `EXTENDED_HEADER` 기반 설명은
> 현재 source-of-truth 와 충돌하는 폐기안이다.
> 구현이나 테스트를 진행할 때는 이 문서를 기준으로 삼지 않는다.

---

## 1. 목적

`zlink_msg_t`에 request-reply 필드(msg_type, correlation_id)를 추가한다.
이 필드는 바인딩 레이어의 request-reply 패턴이 사용하는 기반 기능이다.

core는 다음만 책임진다:
- 메시지에 msg_type과 correlation_id를 설정/조회하는 C API
- send 시 request-reply envelope를 wire에 자동 직렬화
- recv 시 envelope를 자동 파싱하여 msg_type/correlation_id 복원
- DATA 메시지와의 100% 하위 호환

core는 다음을 책임지지 않는다:
- correlation 매칭, pending map, timeout, dispatch 등 request-reply 로직
  (바인딩 레이어 담당)

---

## 2. 기존 `zlink_msg_t` 구조

```c
typedef struct zlink_msg_t
{
    unsigned char _[64] __attribute__((aligned(sizeof(void *))));
} zlink_msg_t;
```

- 64바이트 고정 크기 opaque storage
- 플랫폼별 정렬 보장 (8바이트 또는 4바이트)
- 바인딩은 by-value로 다루며 내부 레이아웃에 의존하지 않음
- 공개 선언과 크기/정렬 계약은 변경하지 않음

---

## 3. 추가되는 내부 상태

`zlink_msg_t`의 내부 구현(`msg_t`)에 아래 필드를 추가한다.
공개 `zlink_msg_t` 선언은 변경하지 않는다.

| 필드 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `msg_type` | `uint8_t` | `0` (DATA) | 메시지 유형: DATA / REQUEST / REPLY |
| `correlation_id` | `uint64_t` | `0` | request-reply 매칭용 식별자 |

- 64바이트 opaque storage 내에 9바이트를 추가로 사용한다.
  기존 내부 구현의 여유 공간이 충분한지 확인이 필요하다.
- `zlink_msg_init()` 호출 시 두 필드는 기본값(0)으로 초기화된다.
- `zlink_msg_copy()` / `zlink_msg_move()` 시 두 필드도 함께 복사/이동된다.
- `zlink_msg_close()` 시 특별한 정리가 필요하지 않다 (값 타입).

---

## 4. 상수

`core/include/zlink.h`에 추가:

```c
#define ZLINK_MSG_TYPE_DATA     0
#define ZLINK_MSG_TYPE_REQUEST  1
#define ZLINK_MSG_TYPE_REPLY    2
```

---

## 5. 추가 C API 함수

### 5.1 `zlink_msg_set_request`

```c
/**
 * @brief 메시지를 REQUEST로 설정한다.
 *
 * msg_type을 ZLINK_MSG_TYPE_REQUEST로, correlation_id를 지정된 값으로
 * 동시에 설정한다. send 시 core가 자동으로 request-reply envelope를
 * wire에 직렬화한다.
 *
 * @param msg_             초기화된 메시지. NULL이면 EINVAL.
 * @param correlation_id_  request-reply 매칭용 식별자.
 * @return 성공 시 0, 실패 시 -1 (errno 설정).
 *
 * 오류:
 * - EINVAL: msg_가 NULL이거나 초기화되지 않은 메시지.
 */
ZLINK_EXPORT int zlink_msg_set_request (zlink_msg_t *msg_,
                                        uint64_t correlation_id_);
```

### 5.2 `zlink_msg_set_reply`

```c
/**
 * @brief 메시지를 REPLY로 설정한다.
 *
 * msg_type을 ZLINK_MSG_TYPE_REPLY로, correlation_id를 지정된 값으로
 * 동시에 설정한다. send 시 core가 자동으로 request-reply envelope를
 * wire에 직렬화한다.
 *
 * @param msg_             초기화된 메시지. NULL이면 EINVAL.
 * @param correlation_id_  원본 request의 correlation_id와 동일한 값.
 * @return 성공 시 0, 실패 시 -1 (errno 설정).
 *
 * 오류:
 * - EINVAL: msg_가 NULL이거나 초기화되지 않은 메시지.
 */
ZLINK_EXPORT int zlink_msg_set_reply (zlink_msg_t *msg_,
                                      uint64_t correlation_id_);
```

### 5.3 `zlink_msg_get_request_info`

```c
/**
 * @brief 메시지의 request-reply 정보를 조회한다.
 *
 * msg_type과 correlation_id를 한 번의 호출로 동시에 반환한다.
 * type_out이 ZLINK_MSG_TYPE_DATA(0)이면 correlation_id_out은 무의미하다.
 *
 * recv 후 이 함수를 호출하면 core가 envelope에서 파싱한 msg_type과
 * correlation_id를 얻을 수 있다. 바인딩은 이 값으로 dispatch만 하면 된다.
 *
 * @param msg_               조회할 메시지. NULL이면 EINVAL.
 * @param type_out_          [out] msg_type. NULL이면 무시.
 *                           ZLINK_MSG_TYPE_DATA (0) / REQUEST (1) / REPLY (2).
 * @param correlation_id_out_ [out] correlation_id. NULL이면 무시.
 * @return 성공 시 0, 실패 시 -1 (errno 설정).
 *
 * 오류:
 * - EINVAL: msg_가 NULL이거나 초기화되지 않은 메시지.
 */
ZLINK_EXPORT int zlink_msg_get_request_info (const zlink_msg_t *msg_,
                                             uint8_t *type_out_,
                                             uint64_t *correlation_id_out_);
```

---

## 6. Wire Envelope

### 6.1 직렬화 규칙

`msg_type`이 `ZLINK_MSG_TYPE_REQUEST` 또는 `ZLINK_MSG_TYPE_REPLY`인
메시지를 전송할 때, core의 send 경로가 request-reply envelope를 자동으로
payload와 함께 wire에 직렬화한다.

- `ZLINK_MSG_TYPE_DATA`(0)인 메시지는 envelope를 생성하지 않는다.
  기존 wire format과 100% 호환된다.
- envelope는 사용자 payload 바이트를 변경하지 않는다.
- envelope는 ZMP frame header의 flag bit로 감지한다.
  사용자 payload의 바이트 패턴을 보고 추정하지 않는다.

### 6.2 감지 메커니즘: ZMP Frame Flag

ZMP 프로토콜의 frame header에 이미 flags 바이트가 존재한다.

```
ZMP Frame Header (8 bytes):
  [magic: 0x5A] [version: 0x01] [flags: 1 byte] [size: ...]

flags byte (기존):
  bit 0 (0x01): MORE       — multipart 계속
  bit 1 (0x02): CONTROL    — 제어 메시지
  bit 2 (0x04): IDENTITY   — routing_id
  bit 3 (0x08): SUBSCRIBE  — 구독
  bit 4 (0x10): CANCEL     — 취소
  bit 5-7:      미사용 (예약)

추가:
  bit 5 (0x20): EXTENDED_HEADER — extended header 존재
```

- `EXTENDED_HEADER` flag가 켜져 있으면 payload 앞에 extended header가
  존재한다. Decoder가 payload를 읽기 전에 extended header를 먼저 파싱한다.
- flag가 꺼져 있으면 기존 payload 그대로. 추가 검사 없음.
- transport 레벨에서 감지하므로 payload 바이트 패턴과 무관하다.
  false positive가 구조적으로 불가능하다.

extended header에는 request-reply envelope, per-message metadata 등
여러 확장이 함께 들어갈 수 있다. 자세한 내용은
[`MSG_METADATA_SPEC.md`](MSG_METADATA_SPEC.md) 참조.

### 6.3 역직렬화 규칙

core의 recv 경로가 ZMP frame flag를 확인하여 envelope를 파싱한다.

- `EXTENDED_HEADER` flag가 켜져 있으면 extended header를 파싱하여
  `msg_type`과 `correlation_id`를 메시지 내부 상태에 복원하고,
  header를 strip하여 사용자에게는 payload만 전달한다.
- flag가 꺼져 있으면 `msg_type=DATA`, `correlation_id=0`으로
  설정한다 (기존 동작과 동일).

### 6.4 Encoding 세부사항

extended header 내부의 request-reply envelope encoding:

- `msg_type`(uint8, 1바이트) + `correlation_id`(uint64, 8바이트) = 9바이트 고정.
- extended header 내에서 request-reply 필드와 metadata 필드는 TLV 또는
  type prefix로 구분한다 (core 내부 구현).

의미 계약:

- DATA 메시지의 wire payload는 변경되지 않는다.
- REQUEST/REPLY 메시지의 사용자 payload는 변경되지 않는다.
- `EXTENDED_HEADER` flag로 감지하므로 payload와 혼동되지 않는다.
- multipart 메시지에서 extended header는 사용자 part count에 영향을 주지 않는다.

---

## 7. 기존 API와의 상호작용

### 7.1 `zlink_msg_init` / `zlink_msg_init_size` / `zlink_msg_init_data`

- 초기화 후 `msg_type=DATA`, `correlation_id=0`.
- 기존 동작 변경 없음.

### 7.2 `zlink_msg_copy` / `zlink_msg_move`

- `msg_type`과 `correlation_id`도 함께 복사/이동된다.

### 7.3 `zlink_msg_close`

- 추가 정리 없음. 값 타입이므로 별도 해제 불필요.

### 7.4 `zlink_msg_data` / `zlink_msg_size`

- 사용자 payload만 반환한다. envelope 데이터는 포함하지 않는다.

### 7.5 `zlink_msg_gets`

- request-reply 필드는 `zlink_msg_gets()`로 조회할 수 없다.
  `zlink_msg_get_request_info()` 전용 API를 사용한다.
- 기존 property("Socket-Type", "Identity" 등)에 영향 없음.

### 7.6 send / recv

- `zlink_send()` / `zlink_recv()` 경로에서 envelope 직렬화/역직렬화가
  자동으로 수행된다.
- `zlink_send()` 시 `msg_type != DATA`이면 envelope를 wire에 추가한다.
- `zlink_recv()` 시 envelope를 감지하면 `msg_type`/`correlation_id`를
  복원하고 envelope를 strip한다.
- Router 소켓의 경우: routing_id 추출 후 첫 번째 payload frame에서
  envelope 검사를 수행한다.

### 7.7 type 재설정

- `msg_type`을 `DATA`로 되돌리는 별도 API는 제공하지 않는다.
- 새 메시지를 `zlink_msg_init()`으로 생성하면 기본값이 `DATA`이다.
- `zlink_msg_set_request()` 후 `zlink_msg_set_reply()`를 호출하면
  type이 `REPLY`로 덮어써진다 (마지막 호출이 우선).

---

## 8. 오류 처리

| 함수 | 오류 조건 | errno |
|------|----------|-------|
| `zlink_msg_set_request` | msg가 NULL 또는 미초기화 | `EINVAL` |
| `zlink_msg_set_reply` | msg가 NULL 또는 미초기화 | `EINVAL` |
| `zlink_msg_get_request_info` | msg가 NULL 또는 미초기화 | `EINVAL` |

- `type_out_` 또는 `correlation_id_out_`이 NULL이면 해당 출력만 생략한다.
  오류가 아니다.
- 모든 함수는 성공 시 `0`, 실패 시 `-1`을 반환하고 `errno`를 설정한다.
  기존 `zlink_msg_*` API의 오류 반환 규칙과 동일하다.

---

## 9. 구현 변경 지점

### 9.1 헤더 파일

- `core/include/zlink.h`:
  - `ZLINK_MSG_TYPE_DATA`, `ZLINK_MSG_TYPE_REQUEST`, `ZLINK_MSG_TYPE_REPLY` 상수
  - `zlink_msg_set_request()`, `zlink_msg_set_reply()`,
    `zlink_msg_get_request_info()` 선언

### 9.2 내부 메시지 구현

- `core/src/core/msg.hpp` (`msg_t` 클래스):
  - `uint8_t msg_type_` 필드 추가 (기본값 0)
  - `uint64_t correlation_id_` 필드 추가 (기본값 0)
  - `init()` / `close()` / `copy()` / `move()` 에서 두 필드 처리

### 9.3 Send 경로

- `core/src/core/session_base.cpp` 또는 해당 send 경로:
  - `msg_type != DATA`이면 envelope 직렬화 후 wire 전송
  - `msg_type == DATA`이면 기존 경로 그대로

### 9.4 Recv 경로

- `core/src/core/session_base.cpp` 또는 해당 recv 경로:
  - 수신 메시지에서 envelope 감지 및 파싱
  - envelope 존재 시 `msg_type` / `correlation_id` 복원, envelope strip
  - envelope 미존재 시 기존 동작 (msg_type=DATA)

### 9.5 64바이트 opaque storage 여유 확인

- 현재 `msg_t` 내부 구현이 64바이트 중 몇 바이트를 사용하는지 확인
- `uint8_t msg_type_`(1) + `uint64_t correlation_id_`(8) + padding 포함
  9~16바이트 추가 필요
- 여유가 부족하면 내부 레이아웃 재배치 또는 포인터 기반 확장 검토

---

## 10. 테스트 요구사항

### 단위 테스트

- `zlink_msg_init()` 후 `get_request_info` → type=DATA, correlation_id=0
- `set_request(msg, 42)` → `get_request_info` → type=REQUEST, correlation_id=42
- `set_reply(msg, 42)` → `get_request_info` → type=REPLY, correlation_id=42
- `set_request` 후 `set_reply` → type=REPLY (마지막 호출 우선)
- `msg_copy` → 복사된 메시지도 동일한 type/correlation_id
- `msg_move` → 이동된 메시지도 동일한 type/correlation_id
- `msg_data()` / `msg_size()` → envelope가 포함되지 않음
- NULL msg 전달 시 EINVAL 반환
- `type_out` NULL 전달 시 correlation_id만 정상 반환
- `correlation_id_out` NULL 전달 시 type만 정상 반환

### Wire 테스트

- REQUEST 메시지 send → recv → get_request_info → type=REQUEST, 동일 correlation_id
- REPLY 메시지 send → recv → get_request_info → type=REPLY, 동일 correlation_id
- DATA 메시지 send → recv → get_request_info → type=DATA (기존 동작 유지)
- DATA 메시지의 wire payload가 변경되지 않음 확인
- REQUEST/REPLY 메시지의 사용자 payload가 변경되지 않음 확인
- multipart REQUEST 메시지 → recv 후 사용자 part count 보존 확인
- Router 소켓에서 REQUEST/REPLY → routing_id + payload 정상 수신 확인

### 호환성 테스트

- `set_request`/`set_reply`를 호출하지 않은 메시지는 기존과 동일하게 동작
- 기존 테스트 suite 전체가 회귀 없이 통과

---

## 현재 변경 방향

위 본문은 기존에 검토했던
`zlink_msg_t` 내부 필드 기반 request-reply 안을 설명한 기록으로 남긴다.

다만 현재 기준에서는 request-reply 를
`message` 레벨이 아니라 `ZMP` 위의 상위 프로토콜 레벨에서 처리한다.

정리:

- `msg_type`, `correlation_id` 를 `zlink_msg_t` 내부 필드로 두는 안은 내린다
- `zlink_msg_set_request()`, `zlink_msg_set_reply()`,
  `zlink_msg_get_request_info()` 같은 message-level API 는
  기준안으로 채택하지 않는다
- request-reply 구분과 correlation 정보는
  request-reply protocol envelope 에서 정의한다
- core `message` 계층은 payload 컨테이너 역할에 집중한다

지원 방향:

- request-reply 는 `ZMP` transport 위의 별도 protocol envelope 로 지원한다
- 이 envelope 는 최소한 protocol id, version, message type,
  correlation id, payload 시작 위치를 정의해야 한다
- multipart 메시지에서는 envelope 와 payload 경계를
  request-reply 문서에서 분명히 정의해야 한다
- `ROUTER` 계열에서는 transport `routing_id` 와
  request-reply correlation 의미를 분리해서 다룬다

즉 이 문서는 "기존 검토안 기록"으로 유지하고,
실제 구현 기준은
[`ZMP_REQUEST_REPLY_PROTOCOL.md`](ZMP_REQUEST_REPLY_PROTOCOL.md)
와
[`SOCKET_REQUEST_REPLY_API_SPEC.md`](SOCKET_REQUEST_REPLY_API_SPEC.md)
에서 다시 정리한다.
