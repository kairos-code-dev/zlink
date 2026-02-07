# ZMP v2.0 프로토콜 상세

## 1. 설계 철학
- ZMTP 비호환 (zlink 전용 최적화)
- 8B 고정 헤더 (가변 길이 인코딩 배제)
- 최소 핸드셰이크

## 2. 프레임 구조

### 2.1 헤더 레이아웃 (8 Bytes 고정)
```
 Byte:   0         1         2         3         4    5    6    7
      ┌─────────┬─────────┬─────────┬─────────┬─────────────────────┐
      │  MAGIC  │ VERSION │  FLAGS  │RESERVED │   PAYLOAD SIZE      │
      │  (0x5A) │  (0x02) │         │ (0x00)  │   (32-bit BE)       │
      └─────────┴─────────┴─────────┴─────────┴─────────────────────┘
```

Fields:
| 필드 | 오프셋 | 크기 | 설명 |
|------|--------|------|------|
| MAGIC | 0 | 1 | 0x5A ('Z') |
| VERSION | 1 | 1 | 0x02 |
| FLAGS | 2 | 1 | 프레임 플래그 |
| RESERVED | 3 | 1 | 0x00 |
| PAYLOAD SIZE | 4-7 | 4 | Big Endian |

### 2.2 FLAGS 비트 정의
| 비트 | 이름 | 값 | 설명 |
|------|------|-----|------|
| 0 | MORE | 0x01 | 멀티파트 계속 |
| 1 | CONTROL | 0x02 | 제어 프레임 |
| 2 | IDENTITY | 0x04 | 라우팅 ID 포함 |
| 3 | SUBSCRIBE | 0x08 | 구독 요청 |
| 4 | CANCEL | 0x10 | 구독 취소 |

## 3. 핸드셰이크

### 3.1 시퀀스
```
Client                              Server
   │                                   │
   │─────── HELLO (greeting) ─────────►│
   │◄────── HELLO (greeting) ──────────│
   │─────── READY (metadata) ─────────►│
   │◄────── READY (metadata) ──────────│
   │◄─────── Data Exchange ───────────►│
```

### 3.2 HELLO 프레임
- control_type (1B)
- socket_type (1B)
- routing_id_len (1B)
- routing_id (0~255B)

### 3.3 READY 프레임
- Socket-Type 속성 (항상)
- Identity 속성 (DEALER/ROUTER만)

## 4. WebSocket 프레이밍
- RFC 6455 Binary frame (Opcode=0x02)
- Payload = ZMP Frame
- Beast 라이브러리 기반
