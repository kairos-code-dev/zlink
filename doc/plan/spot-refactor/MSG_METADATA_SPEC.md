# Message Metadata — Core C API Spec

> **상태**: Historical Draft
> **관련 문서**:
> [`ZMP_PROTOCOL_OVERVIEW.md`](ZMP_PROTOCOL_OVERVIEW.md) — 공통 ZMP 전송 형식
>
> 이 문서의 설계안은 현재 채택하지 않았다.
> 현재 기준은 message-level metadata 를 유지하지 않는 방향이다.
> 현재 구현 기준은 관련 ZMP protocol 문서와 계획 문서를 참고한다.

---

## 1. 목적

`zlink_msg_t`에 범용 per-message metadata 기능을 추가한다.
각 메시지에 application 수준의 속성을 설정하여 wire를 통해 전달할 수 있게 한다.

이 기능은 기존 metadata 메커니즘과 완전히 별개다.

### 기존 metadata와의 차이

zlink에는 이미 세 종류의 "metadata"가 존재한다.
이 스펙에서 추가하는 per-message metadata는 네 번째이며, 나머지 셋과
범위, 시점, API가 모두 다르다.

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. ZMP 프로토콜 메타데이터 (core/src/protocol/metadata.hpp)      │
│    - 범위: 연결 (connection) 수준                                │
│    - 시점: ZMP 핸드셰이크 시 1회 교환                             │
│    - 내용: Socket-Type, Identity, Peer-Address, User-Id 등       │
│    - API:  zlink_msg_gets(msg, "Socket-Type")                   │
│    - 특징: 연결 후 모든 메시지에 동일한 값이 부착됨.               │
│           개별 메시지마다 다른 값을 보낼 수 없음.                  │
├─────────────────────────────────────────────────────────────────┤
│ 2. Discovery 서비스 메타데이터 (zlink_discovery_set_metadata)    │
│    - 범위: 서비스 인스턴스 수준                                   │
│    - 시점: 서비스 등록/업데이트 시                                 │
│    - 내용: opaque binary blob (라우팅 가중치, 서비스 속성 등)      │
│    - API:  zlink_discovery_set_metadata / get_metadata           │
│    - 특징: 메시지와 무관. 서비스 발견/라우팅 정책용.               │
├─────────────────────────────────────────────────────────────────┤
│ 3. Request-Reply 필드 (MSG_REQUEST_REPLY_SPEC)                  │
│    - 범위: 개별 메시지 수준                                       │
│    - 시점: 매 send마다                                           │
│    - 내용: msg_type(u8) + correlation_id(u64) — 고정 구조        │
│    - API:  zlink_msg_set_request / set_reply / get_request_info  │
│    - 특징: request-reply 패턴 전용. 사용자 정의 속성 불가.        │
├─────────────────────────────────────────────────────────────────┤
│ 4. Per-Message Metadata (이 스펙)                                │
│    - 범위: 개별 메시지 수준                                       │
│    - 시점: 매 send마다                                           │
│    - 내용: 사용자 정의 key(u16)-value 쌍 — 가변 구조             │
│    - API:  zlink_msg_set_metadata / get_metadata                 │
│    - 특징: 범용. 사용자가 자유롭게 key를 정의하여 사용.           │
│           메시지마다 다른 값을 보낼 수 있음.                      │
└─────────────────────────────────────────────────────────────────┘
```

핵심 차이:
- **ZMP 프로토콜 메타데이터**는 연결 시 한 번 교환되고 이후 변경 불가.
  `zlink_msg_gets()`로 조회한다. 개별 메시지에 다른 값을 설정할 수 없다.
- **이 스펙의 per-message metadata**는 메시지마다 다른 값을 설정할 수 있다.
  `zlink_msg_set_metadata()` / `zlink_msg_get_metadata()`로 설정/조회한다.
- 두 API의 namespace는 완전히 분리되어 있다. `zlink_msg_gets("Socket-Type")`과
  `zlink_msg_get_metadata(0x0100)`은 서로 다른 저장소에서 조회한다.

### 사용 예

- 분산 추적 (trace-id, span-id)
- 메시지 우선순위
- 전송 시각
- application 정의 라우팅 힌트
- 사용자 정의 속성

---

## 2. 설계 원칙

- metadata가 없는 메시지는 **wire overhead가 0**이다. 기존 동작과 100% 호환.
- metadata가 있는 메시지만 header가 생성되어 wire에 실린다.
- `zlink_msg_t`의 공개 ABI(64바이트 opaque storage)는 변경하지 않는다.
- key는 `uint16_t` 정수다. 사용자는 application 측에서 enum으로 정의하여
  사용한다. string key 대비 wire overhead가 작고, 조회가 빠르며,
  컴파일 타임에 오타를 방지할 수 있다.

---

## 3. Key 대역

| 대역 | 범위 | 용도 |
|------|------|------|
| zlink 내부 예약 | `0x0000` ~ `0x00FF` | zlink 내부 기능 확장용. 사용자 설정 시 `EINVAL` |
| 사용자 정의 | `0x0100` ~ `0xFFFF` | application 자유 사용 |

사용자는 `0x0100` 이상의 key를 application enum으로 정의하여 사용한다:

```c
// 사용자 코드 예시
enum my_metadata_keys {
    MY_META_TRACE_ID   = 0x0100,
    MY_META_PRIORITY   = 0x0101,
    MY_META_TIMESTAMP  = 0x0102,
};
```

```java
// Java 사용자 코드
enum MyMetaKey {
    TRACE_ID(0x0100),
    PRIORITY(0x0101),
    TIMESTAMP(0x0102);

    final int value;
    MyMetaKey(int value) { this.value = value; }
}
```

---

## 4. 상수

`core/include/zlink.h`에 추가:

```c
/** 사용자 metadata key 최소값 (이 값 이상만 사용 가능) */
#define ZLINK_MSG_METADATA_KEY_USER_MIN   0x0100

/** 메타데이터 값 최대 길이 (단일 항목, 바이트) */
#define ZLINK_MSG_METADATA_VALUE_MAX      65535
```

---

## 5. C API 함수

### 5.1 `zlink_msg_set_metadata`

```c
/**
 * @brief 메시지에 metadata key-value를 설정한다.
 *
 * 같은 key가 이미 존재하면 값을 덮어쓴다.
 * 이 함수를 한 번이라도 호출하면 send 시 metadata header가 wire에 추가된다.
 *
 * @param msg_        초기화된 메시지. NULL이면 EINVAL.
 * @param key_        metadata key (uint16_t).
 *                    0x0000~0x00FF는 zlink 예약 대역이므로 EINVAL.
 *                    0x0100~0xFFFF 사용 가능.
 * @param value_      값 바이너리. NULL이면 길이 0으로 처리 (키 존재 자체가 의미).
 * @param value_size_ 값 길이 (바이트). 최대 ZLINK_MSG_METADATA_VALUE_MAX.
 * @return 성공 시 0, 실패 시 -1 (errno 설정).
 *
 * 오류:
 * - EINVAL: msg_ NULL, key_ < 0x0100, value 길이 초과.
 * - ENOMEM: heap 할당 실패.
 */
ZLINK_EXPORT int zlink_msg_set_metadata (zlink_msg_t *msg_,
                                         uint16_t key_,
                                         const void *value_,
                                         size_t value_size_);
```

### 5.2 `zlink_msg_get_metadata`

```c
/**
 * @brief 메시지에서 metadata 값을 조회한다.
 *
 * @param msg_   조회할 메시지. NULL이면 NULL 반환.
 * @param key_   조회할 key.
 * @param size_  [out] 값 길이. NULL이면 무시.
 * @return 값 포인터. 키가 없으면 NULL.
 *         반환된 포인터는 메시지가 유효한 동안만 사용할 수 있다.
 *         메시지 close, move, 또는 동일 key에 set_metadata 호출 시 무효.
 */
ZLINK_EXPORT const void *zlink_msg_get_metadata (const zlink_msg_t *msg_,
                                                 uint16_t key_,
                                                 size_t *size_);
```

- `has_metadata`, `remove_metadata`, `metadata_count`는 제공하지 않는다.
  - 존재 확인: `get_metadata()`가 NULL을 반환하면 해당 key가 없는 것이다.
  - 삭제: 메시지를 새로 생성하면 metadata가 비어 있다.
  - 개수 조회: 사용자가 조회할 필요가 없다. wire header 생성 여부는
    내부에서 관리한다.

---

## 6. 메시지 구조와 Wire Layout

### 6.1 `zlink_msg_t` 내부 상태

```
zlink_msg_t (64 bytes opaque storage)
┌──────────────────────────────────────────────────────────┐
│  기존 내부 상태 (payload ptr, size, refcnt, flags 등)    │
│  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─   │
│  + msg_type (uint8)              ← REQUEST_REPLY_SPEC   │
│  + correlation_id (uint64)       ← REQUEST_REPLY_SPEC   │
│  + metadata_ptr (pointer)        ← 이 스펙 (NULL이면 없음) │
└──────────────────────────────────────────────────────────┘
              │                              │
              │ msg_type/correlation_id      │ metadata_ptr
              │ (고정 크기, inline)            │ (가변, heap)
              ▼                              ▼
   ┌─────────────────┐          ┌─────────────────────┐
   │ request-reply    │          │ metadata storage    │
   │ (9 bytes inline) │          │ (heap, key-value)   │
   │ type=1, id=42    │          │ 0x0100 → [bytes]   │
   └─────────────────┘          │ 0x0101 → [bytes]   │
                                 └─────────────────────┘
                                  ※ set_metadata() 최초
                                    호출 시에만 할당
```

### 6.2 Wire Layout — 케이스별

**Case 1: DATA 메시지 (metadata 없음, request-reply 없음)**
```
ZMP frame: flags = 0x00 (EXTENDED_HEADER 꺼짐)
wire:
  [routing_id (Router만)]
  [payload part 0]
  [payload part 1]
  ...
  → extended header 없음. 기존과 동일. overhead 0.
```

**Case 2: REQUEST/REPLY 메시지만 (metadata 없음)**
```
ZMP frame: flags |= 0x20 (EXTENDED_HEADER 켜짐)
wire:
  [routing_id (Router만)]
  [extended header: request-reply envelope]  ← core 자동 추가
  [payload part 0]
  [payload part 1]
  ...
```

**Case 3: Metadata만 (request-reply 없음)**
```
ZMP frame: flags |= 0x20 (EXTENDED_HEADER 켜짐)
wire:
  [routing_id (Router만)]
  [extended header: metadata entries]        ← core 자동 추가
  [payload part 0]
  [payload part 1]
  ...
```

**Case 4: REQUEST/REPLY + Metadata 모두 있음**
```
ZMP frame: flags |= 0x20 (EXTENDED_HEADER 켜짐)
wire:
  [routing_id (Router만)]
  [extended header: request-reply + metadata] ← 둘 다 포함
  [payload part 0]
  [payload part 1]
  ...
```

### 6.3 Metadata Header 내부 구조

```
metadata header:
┌──────────────────────────────────────────────────┐
│  header marker (core 내부 식별용)                 │
├──────────────────────────────────────────────────┤
│  entry count (uint16)                            │
├──────────────────────────────────────────────────┤
│  entry 0:                                        │
│    ┌────────────┬─────────────┬────────────────┐ │
│    │ key (u16)  │ val_len(u16)│ value (bytes)  │ │
│    │ 0x0100     │ 16          │ [trace-id...]  │ │
│    └────────────┴─────────────┴────────────────┘ │
│  entry 1:                                        │
│    ┌────────────┬─────────────┬────────────────┐ │
│    │ key (u16)  │ val_len(u16)│ value (bytes)  │ │
│    │ 0x0101     │ 1           │ [0x03]         │ │
│    └────────────┴─────────────┴────────────────┘ │
│  ...                                             │
└──────────────────────────────────────────────────┘

entry 1개당 wire overhead: 4 bytes (key 2 + val_len 2) + value bytes
```

### 6.4 Request-Reply Envelope과의 관계 요약

```
┌──────────────────────────────────────────────────────┐
│               MSG_REQUEST_REPLY_SPEC                 │
│                                                      │
│  zlink_msg_set_request(msg, correlation_id)          │
│  zlink_msg_set_reply(msg, correlation_id)            │
│  zlink_msg_get_request_info(msg, &type, &id)         │
│                                                      │
│  ┌────────────────────────────────────┐              │
│  │ request-reply envelope (wire)     │              │
│  │  msg_type(u8) + correlation_id(u64)│              │
│  └────────────────────────────────────┘              │
│         전용 API. 고정 크기. inline 저장.              │
└──────────────────────┬───────────────────────────────┘
                       │ 독립적으로 동작
                       │ 한 메시지에 둘 다 존재 가능
┌──────────────────────┴───────────────────────────────┐
│               MSG_METADATA_SPEC (이 문서)             │
│                                                      │
│  zlink_msg_set_metadata(msg, key, value, size)       │
│  zlink_msg_get_metadata(msg, key, &size)             │
│                                                      │
│  ┌────────────────────────────────────┐              │
│  │ metadata header (wire)            │              │
│  │  N개의 (key u16 + value) entries   │              │
│  └────────────────────────────────────┘              │
│         범용 API. 가변 크기. heap 저장.               │
└──────────────────────────────────────────────────────┘

recv 시 core가 두 header를 모두 파싱하여 각각의 내부 상태에 복원하고,
사용자에게는 payload만 전달한다.
```

---

## 7. Wire Header 세부 규칙

### 7.1 감지 메커니즘: ZMP Frame Flag

extended header(metadata, request-reply envelope)의 존재는 ZMP 프로토콜의
frame header에 있는 flag bit로 감지한다. payload 바이트 패턴을 보고 추정하지
않는다.

```
ZMP Frame Header (8 bytes):
  [magic: 0x5A] [version: 0x01] [flags: 1 byte] [size: ...]

flags byte:
  bit 0 (0x01): MORE           — multipart 계속
  bit 1 (0x02): CONTROL        — 제어 메시지
  bit 2 (0x04): IDENTITY       — routing_id
  bit 3 (0x08): SUBSCRIBE      — 구독
  bit 4 (0x10): CANCEL         — 취소
  bit 5 (0x20): EXTENDED_HEADER — extended header 존재  ← 추가
  bit 6-7:      미사용 (예약)
```

- `EXTENDED_HEADER` flag가 켜져 있으면 payload 앞에 extended header가 있다.
- flag가 꺼져 있으면 기존 payload 그대로. 추가 검사 없음. overhead 0.
- transport 레벨에서 감지하므로 false positive가 구조적으로 불가능하다.

extended header 안에 metadata entries와 request-reply envelope가 함께
들어갈 수 있다. 상세는 [`MSG_REQUEST_REPLY_SPEC.md`](MSG_REQUEST_REPLY_SPEC.md) 참조.

### 7.2 생성 조건

- metadata가 1개 이상 설정된 메시지를 send할 때 `EXTENDED_HEADER` flag를
  켜고 metadata를 extended header에 직렬화한다.
- metadata가 없는 메시지는 flag가 꺼져 있다. 기존 wire format과 100% 호환.

### 7.3 직렬화

send 시 core가 metadata를 extended header에 자동으로 직렬화한다.

- 사용자 payload 바이트는 변경되지 않는다.
- multipart 메시지에서 extended header는 사용자 part count에 영향을 주지 않는다.
- wire 상의 각 metadata entry: `uint16_t key` + `uint16_t value_size` + `value bytes`.
  총 entry당 overhead: 4바이트 + value 크기.

### 7.4 역직렬화

recv 시 core가 `EXTENDED_HEADER` flag를 확인하여 파싱한다.

- flag가 켜져 있으면 extended header에서 metadata entries를 파싱하여
  메시지 내부 상태에 복원하고, header를 strip하여 payload만 전달한다.
- flag가 꺼져 있으면 metadata는 비어 있다 (기존 동작).

### 7.5 의미 계약

- metadata가 없는 메시지의 wire payload는 변경되지 않는다.
- metadata가 있는 메시지의 사용자 payload는 변경되지 않는다.
- `EXTENDED_HEADER` flag로 감지하므로 payload와 혼동되지 않는다.
- multipart에서 extended header에 의한 추가 part가 사용자에게 보이지 않는다.

### 7.5 Request-Reply 필드와의 관계

- request-reply 필드(`msg_type`, `correlation_id`)는 별도 전용 API
  (`zlink_msg_set_request`/`zlink_msg_set_reply`)로 관리한다.
- metadata key-value와 request-reply 필드는 독립적으로 동작한다.
- 한 메시지에 request-reply 필드와 metadata가 모두 존재할 수 있다.
  이 경우 wire에 두 header가 모두 실린다.
- 상세는 [`MSG_REQUEST_REPLY_SPEC.md`](MSG_REQUEST_REPLY_SPEC.md) 참조.

---

## 8. 기존 API와의 상호작용

### 8.1 `zlink_msg_init` / `zlink_msg_init_size` / `zlink_msg_init_data`

- 초기화 후 metadata는 비어 있다 (count == 0).
- 기존 동작 변경 없음.

### 8.2 `zlink_msg_copy`

- metadata도 함께 복사된다.

### 8.3 `zlink_msg_move`

- metadata도 함께 이동된다. source의 metadata는 비워진다.

### 8.4 `zlink_msg_close`

- 내부 metadata 저장소가 해제된다.

### 8.5 `zlink_msg_data` / `zlink_msg_size`

- 사용자 payload만 반환한다. metadata header는 포함하지 않는다.

### 8.6 `zlink_msg_gets`

- `zlink_msg_gets()`는 기존 ZMP 프로토콜 property 전용이다.
  per-message metadata는 `zlink_msg_get_metadata()`로 조회한다.
- 두 API의 namespace는 분리되어 있다.

### 8.7 send / recv

- send 시 metadata가 있으면 header를 wire에 추가한다.
- recv 시 header를 파싱하여 metadata를 복원하고 strip한다.
- Router 소켓: routing_id 추출 후 첫 번째 payload frame에서 header를 검사한다.

---

## 9. 메모리 관리

### 8.1 내부 저장

- metadata는 메시지 내부에서 관리한다.
- key가 uint16이므로 내부적으로 sparse array, small hash map, 또는
  sorted vector 등으로 구현할 수 있다 (구현 재량).
- metadata가 없는 메시지(대부분)는 추가 할당이 발생하지 않는다.

### 8.2 값 포인터 유효 기간

`zlink_msg_get_metadata()`가 반환한 포인터는 아래 상황에서 무효가 된다:
- 메시지 `close`
- 메시지 `move` (source 측)
- 동일 key에 대한 `set_metadata` 재호출

사용자는 값을 장기 보관하려면 복사해야 한다.

### 8.3 크기 제한

| 항목 | 상한 | 이유 |
|------|------|------|
| key 범위 | `0x0100` ~ `0xFFFF` (65280개) | 하위 256개는 zlink 예약 |
| 단일 value 길이 | 65535바이트 | uint16 인코딩 |
| 총 metadata 항목 수 | 제한 없음 (heap 의존) | 실질적 제한은 메모리 |

---

## 10. 오류 처리

| 함수 | 오류 조건 | errno |
|------|----------|-------|
| `set_metadata` | msg NULL | `EINVAL` |
| `set_metadata` | key < `0x0100` (예약 대역) | `EINVAL` |
| `set_metadata` | value 길이 > 65535 | `EINVAL` |
| `set_metadata` | heap 할당 실패 | `ENOMEM` |
| `get_metadata` | msg NULL | 반환 NULL (errno 미설정) |

- `get_metadata`는 조회 전용이므로 NULL 입력 시 errno를 설정하지 않고
  NULL을 반환한다.

---

## 11. 구현 변경 지점

### 10.1 헤더 파일

- `core/include/zlink.h`:
  - `ZLINK_MSG_METADATA_KEY_USER_MIN`, `ZLINK_MSG_METADATA_VALUE_MAX` 상수
  - `zlink_msg_set_metadata()`, `zlink_msg_get_metadata()` 선언

### 10.2 내부 메시지 구현

- `core/src/core/msg.hpp` (`msg_t` 클래스):
  - metadata 저장소 포인터 추가 (기본 NULL — 할당 지연)
  - key가 uint16이므로 sparse array 또는 small sorted vector로 구현 가능
  - `init()`: metadata 포인터 NULL 초기화
  - `close()`: metadata 저장소 해제
  - `copy()`: metadata 깊은 복사
  - `move()`: metadata 포인터 이동, source NULL 처리

### 10.3 Send 경로

- metadata count > 0이면 metadata header를 직렬화하여 wire에 추가
- metadata count == 0이면 기존 경로 그대로

### 10.4 Recv 경로

- 수신 메시지에서 metadata header 감지 및 파싱
- header 존재 시 metadata 복원, header strip
- header 미존재 시 기존 동작

### 10.5 Request-Reply header와의 공존

- 한 메시지에 request-reply header와 metadata header가 모두 존재할 수 있다.
- wire 상에서 두 header의 순서와 구분 방식은 core 내부 구현이 결정한다.
- recv 시 두 header를 모두 파싱하여 각각의 내부 상태에 복원한다.

---

## 12. 바인딩 Surface

각 바인딩은 이 C API를 언어별 typed surface로 노출한다.
key < `0x0100` 설정 시 바인딩 레벨에서도 검증하여 예외를 던져야 한다.

```typescript
// Node 예시
const TRACE_ID = 0x0100;
const PRIORITY = 0x0101;

msg.setMetadata(TRACE_ID, Buffer.from(traceId));
const traceId = msg.getMetadata(TRACE_ID);  // Buffer | null
```

```python
# Python 예시
TRACE_ID = 0x0100
PRIORITY = 0x0101

msg.set_metadata(TRACE_ID, trace_id_bytes)
trace_id = msg.get_metadata(TRACE_ID)  # bytes | None
```

```java
// Java 예시
enum MyMetaKey {
    TRACE_ID(0x0100), PRIORITY(0x0101);
    final int value;
    MyMetaKey(int v) { this.value = v; }
}

msg.setMetadata(MyMetaKey.TRACE_ID.value, traceIdBytes);
byte[] traceId = msg.getMetadata(MyMetaKey.TRACE_ID.value);
```

```cpp
// C++ 예시
enum class MyMeta : uint16_t {
    TraceId  = 0x0100,
    Priority = 0x0101,
};

msg.set_metadata(static_cast<uint16_t>(MyMeta::TraceId), data, size);
auto [ptr, len] = msg.get_metadata(static_cast<uint16_t>(MyMeta::TraceId));
```

---

## 13. 테스트 요구사항

### 단위 테스트

- `msg_init()` 후 `get_metadata(0x0100)` → NULL
- `set_metadata(0x0100, value)` → `get_metadata(0x0100)` → 동일 값
- `set_metadata` 같은 key 재호출 → 값 덮어쓰기 확인
- `get_metadata(0x0200)` (미설정 key) → NULL
- `msg_copy` → 복사된 메시지에도 동일 metadata
- `msg_move` → 이동된 메시지에 metadata, source는 비어 있음
- `msg_close` → metadata 해제 (leak 없음)
- value 길이 65535바이트 → 성공
- value 길이 65536바이트 → EINVAL
- key 0x00FF (예약 대역) → EINVAL
- key 0x0100 (사용자 대역 최소값) → 성공
- NULL msg 입력 시 안전한 동작

### Wire 테스트

- metadata 설정 → send → recv → `get_metadata` → 동일 값 복원
- 여러 key-value → send → recv → 전부 복원
- metadata 없는 메시지 → send → recv → `get_metadata` 전부 NULL (기존 동작)
- metadata 있는 메시지의 사용자 payload 변경 없음 확인
- multipart + metadata → recv 후 사용자 part count 보존
- Router 소켓에서 metadata 메시지 → routing_id + metadata + payload 정상
- request-reply 필드 + metadata 동시 → send → recv → 둘 다 정상 복원

### 호환성 테스트

- metadata API를 호출하지 않은 메시지는 기존과 동일하게 동작
- 기존 테스트 suite 전체가 회귀 없이 통과

### 성능 테스트

- metadata 없는 메시지의 send/recv throughput 변화 없음 확인
- metadata 1개 설정 시 overhead 측정
- metadata 10개 설정 시 overhead 측정

---

## 현재 변경 방향

위 본문은 기존에 검토했던 `zlink_msg_t` 수준의
범용 per-message metadata 안을 설명한 기록으로 남긴다.

다만 현재 기준에서는 이 방향을 채택하지 않는다.

정리:

- core 가 `zlink_msg_t` 수준의 범용 metadata 저장/조회 기능을 제공하는 안은 내린다
- metadata 가 필요하면 application 이 payload 또는 multipart 구조로 직접 표현한다
- request-reply, SPOT routed 같은 상위 의미는
  metadata 나 `zlink_msg_t` 내부 필드가 아니라
  각 프로토콜 레벨에서 처리한다

즉 이 문서는 "기존 검토안 기록"으로 유지하고,
실제 구현 기준 문서로 직접 사용하지 않는다.
