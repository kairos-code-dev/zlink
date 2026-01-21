# RFC: ZMP Protocol v1 (Draft)

**Date:** 2026-01-22  
**Owner:** 팀장님  
**Status:** Implemented (current behavior)

---

## 1. Abstract

ZMP v1은 zlink 전용 커스텀 메시지 프로토콜이다. ZMTP와 호환되지
않으며, 고정 길이 헤더와 단순 플래그로 핫패스를 단순화한다. v1은
READY/ERROR 제어 프레임, 선택적 메타데이터, heartbeat TTL/Context를
추가한다.

---

## 2. Motivation

- ZMTP 핫패스 복잡도 제거
- 핸드셰이크 실패 원인 가시화
- 운영/모니터링을 위한 메타데이터 전달

---

## 3. Terminology

- Frame: 헤더(8 bytes) + 바디
- Control Frame: CONTROL 플래그가 설정된 프레임
- Identity Frame: IDENTITY 플래그가 설정된 프레임

---

## 4. Protocol at a Glance (ASCII)

```
+-----+-----+-----+-----+-----------------------------+
| 0   | 1   | 2   | 3   | 4..7                        |
|MAGIC|VER  |FLAGS|RSVD | BODY_LEN (uint32, BE)       |
+-----+-----+-----+-----+-----------------------------+

FLAGS bits:
  bit0 MORE | bit1 CONTROL | bit2 IDENTITY | bit3 SUB | bit4 CANCEL
  bit5..7 reserved (0)

Control frame body:
+--------+------------------------------+
| TYPE   | payload (type-specific)      |
+--------+------------------------------+
```

---

## 5. Frame Format

### 5.1 Header (8 bytes)

- Byte 0: Magic = 0x5A
- Byte 1: Version = 0x02
- Byte 2: Flags
- Byte 3: Reserved (0)
- Byte 4..7: Body length (uint32, big-endian)

### 5.2 Body

- `body_len` bytes
- Max size: 4GB-1 (uint32 max)
- body_len=0인 데이터 프레임은 허용
- SUBSCRIBE/CANCEL에서 body_len=0은 전체 토픽 의미

---

## 6. Flag Rules

허용 조합:
- 단일 플래그
- MORE + IDENTITY

금지 조합:
- CONTROL + IDENTITY
- CONTROL + MORE (제어 프레임은 단일 프레임만 허용)
- SUBSCRIBE/CANCEL과 다른 플래그 결합
- SUBSCRIBE + CANCEL 동시 사용
- 예약 비트(5~7) 사용

---

## 7. Control Types

| Type | Name | Body | Notes |
|---|---|---|---|
| 0x01 | HELLO | type + socket_type + identity | 필수 핸드셰이크 |
| 0x02 | HEARTBEAT | legacy(1B) or TTL/ctx | 옵션 |
| 0x03 | HEARTBEAT_ACK | ctx echo | 옵션 |
| 0x04 | READY | metadata(optional) | v1 |
| 0x05 | ERROR | error_code + reason | v1 |

---

## 8. Handshake

### 8.1 Flow

```
Client -> Server: HELLO
Client -> Server: READY (HELLO 직후 연속 전송 가능)
Server -> Client: HELLO
Server -> Client: READY (HELLO 직후 연속 전송 가능)
(then DATA, 양측 READY 수신 이후)
```

READY 이전에 데이터 프레임을 수신하면 EPROTO로 처리한다.

### 8.2 HELLO Format

- Byte 0: Control type = 0x01
- Byte 1: Socket type (1 byte)
- Byte 2: Identity length (uint8)
- Byte 3..: Identity bytes

### 8.3 READY Format

- Byte 0: Control type = 0x04
- Byte 1..: metadata(optional)

### 8.4 ERROR Format

- Byte 0: Control type = 0x05
- Byte 1: error_code (u8)
- Byte 2: reason_len (u8)
- Byte 3..: reason bytes (ASCII)

권장 error_code:
- 0x01: MALFORMED
- 0x02: UNSUPPORTED
- 0x03: INCOMPATIBLE
- 0x04: AUTH
- 0x7F: INTERNAL

---

## 9. Metadata Encoding (READY)

메타데이터는 ZMTP property 인코딩을 재사용한다.

```
name_len(u8) | name(bytes) | value_len(u32, network order) | value(bytes)
```

기본 프로퍼티(권장):
- Socket-Type
- Identity (DEALER/ROUTER만)

확장 프로퍼티(선택):
- Resource (네임스페이스 포함 문자열 권장)

제약:
- 총 메타데이터 길이 상한 권장: 4KB
- 알 수 없는 프로퍼티는 무시하거나 보관
- 메타데이터 전송은 기본 비활성, `ZMQ_ZMP_METADATA=1`에서만 활성

---

## 10. Heartbeat TTL/Context

### 10.1 HEARTBEAT (0x02)

- Byte 0: control_type = 0x02
- Byte 1..2: ttl_deciseconds (u16, network order)
- Byte 3: ctx_len (u8, 0..16 권장)
- Byte 4..: ctx bytes

### 10.2 HEARTBEAT_ACK (0x03)

- Byte 0: control_type = 0x03
- Byte 1: ctx_len
- Byte 2..: ctx bytes

### 10.3 Semantics

- ttl_deciseconds는 "이 시간 안에 신호가 없으면 종료 가능"을 의미
- TTL 합의 규칙(권장): min(local, remote)
- ctx는 왕복 확인/지연 측정용 불투명 값

### 10.4 Compatibility

- 바디 길이 1인 레거시 HEARTBEAT 허용
- ctx_len 비정상 값은 EPROTO 처리 권장

---

## 11. ROUTER Identity

- IDENTITY 프레임은 ROUTER 수신 경로 전용
- 메시지 첫 프레임에서만 허용
- MORE와 조합 가능(멀티파트 허용)

---

## 12. SUBSCRIBE/CANCEL

- 데이터 프레임 플래그로 처리
- 바디는 구독 토픽 바이트열
- XPUB/XSUB은 기존 ZMQ 구독 규칙 유지

---

## 13. Error Handling

| Code | Name | Action |
|---|---|---|
| 0x01 | INVALID_MAGIC | Disconnect |
| 0x02 | VERSION_MISMATCH | Disconnect |
| 0x03 | FLAGS_INVALID | Disconnect |
| 0x04 | BODY_TOO_LARGE | Disconnect |
| 0x05 | SOCKET_TYPE_MISMATCH | Disconnect |
| 0x06 | HANDSHAKE_TIMEOUT | Disconnect |

Unknown values:
- 알 수 없는 control type: Disconnect
- 예약 비트가 0이 아님: Disconnect

---

## 14. Security

- ZMP는 보안 메커니즘을 정의하지 않음
- 보안은 전송 계층 정책에 따름(TLS 권장)
- ZMQ 보안/인증 옵션은 제거 대상

---

## 15. Compatibility

- ZMTP와 완전 비호환
- v1 이외 버전은 연결 종료
- zlink ↔ zlink 전용

### 15.1 Socket Type Compatibility Matrix

| Client | Server | Valid | Notes |
|---|---|---|---|
| PAIR | PAIR | yes | 동등 패턴 |
| DEALER | ROUTER | yes | 기본 패턴 |
| DEALER | DEALER | yes | 동등 패턴 |
| ROUTER | ROUTER | yes | 양방향 라우팅 |
| PUB | SUB | yes | 구독 필수 |
| XPUB | XSUB | yes | 내부 확장 |
| DEALER | PUB | no | 타입 불일치 |
| SUB | ROUTER | no | 타입 불일치 |

---

## 16. IANA Considerations

None.

---

## 17. Message Sequences

### 17.1 PAIR ↔ PAIR

```
Client -> Server: HELLO (PAIR)
Server -> Client: HELLO (PAIR)
Client -> Server: READY
Server -> Client: READY
Client -> Server: DATA
Server -> Client: DATA
```

### 17.2 DEALER -> ROUTER

```
DEALER -> ROUTER: HELLO (DEALER)
ROUTER -> DEALER: HELLO (ROUTER)
DEALER -> ROUTER: READY
ROUTER -> DEALER: READY
DEALER -> ROUTER: DATA
```
