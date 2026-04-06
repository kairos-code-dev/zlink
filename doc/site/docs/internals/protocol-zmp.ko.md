
# ZMP v1.0 프로토콜 상세

## 왜 ZMTP 대신 ZMP인가?

ZMP(zlink Message Protocol)는 ZMTP를 대체하는 전용 와이어 프로토콜이다.
ZMTP의 가변 길이 크기 인코딩, 다단계 greeting/handshake 협상, 하위 호환
기구는 zlink에 불필요한 파싱 복잡도와 프레임당 오버헤드를 추가한다. ZMP는
고정 8바이트 헤더, 버전 협상 없는 2회 왕복 핸드셰이크, zlink의 라우팅/구독/제어
의미에 맞춘 플래그 세트를 사용한다. 그 결과 파싱이 단순하고, 프레임당
오버헤드가 작으며, 핸드셰이크가 더 적은 왕복으로 완료된다.

## 1. 설계 철학
- ZMTP 비호환 (zlink 전용 최적화)
- 8B 고정 헤더 (가변 길이 인코딩 배제)
- 최소 핸드셰이크

## 2. 프레임 구조

### 2.1 헤더 레이아웃 (8 Bytes 고정)
```
 Byte:   0         1         2         3         4    5    6    7
      +---------+---------+---------+---------+---------------------+
      |  MAGIC  | VERSION |  FLAGS  |RESERVED |   PAYLOAD SIZE      |
      |  (0x5A) |  (0x01) |         | (0x00)  |   (32-bit BE)       |
      +---------+---------+---------+---------+---------------------+
```

Fields:
| 필드 | 오프셋 | 크기 | 설명 |
|------|--------|------|------|
| MAGIC | 0 | 1 | 0x5A ('Z') |
| VERSION | 1 | 1 | 0x01 |
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

```mermaid
sequenceDiagram
    participant C as Client
    participant S as Server

    C->>S: HELLO (greeting)
    S->>C: HELLO (greeting)
    C->>S: READY (metadata)
    S->>C: READY (metadata)
    C->>S: Data Exchange
    S->>C: Data Exchange
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
