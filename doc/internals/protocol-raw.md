# RAW (STREAM) 프로토콜 상세

## 1. 개요
STREAM 소켓 전용 프로토콜. ZMP를 사용하지 않는 외부 클라이언트와의 통신용.

## 2. Wire Format
```
┌──────────────────────┬─────────────────────────────┐
│  Length (4 Bytes)    │     Payload (N Bytes)       │
│  (Big Endian)        │                             │
└──────────────────────┴─────────────────────────────┘
```

- Length: 페이로드 순수 길이 (4B Big Endian)
- Payload: 애플리케이션 데이터

## 3. 설계 의도
- 클라이언트 구현 단순화: `read(4) → read(len)` 패턴
- 스트림 투명성: 최소 프레이밍 오버헤드
- 핸드셰이크 없음 (즉시 데이터 송수신)

## 4. STREAM 소켓 내부 API (멀티파트)

### 4.1 송신 (zlink_send)
```
Frame 1: [Routing ID (4 bytes)] + MORE flag
Frame 2: [Payload (N bytes)]
```

### 4.2 수신 (zlink_recv)
```
Frame 1: [Routing ID (4 bytes)] + MORE flag
Frame 2: [Payload (N bytes)]
```

### 4.3 이벤트 메시지
- Connect: [Routing ID] + MORE, [0x01]
- Disconnect: [Routing ID] + MORE, [0x00]

## 5. 엔진 구현
- asio_raw_engine_t 사용
- raw_encoder_t: routing_id + Length-Prefix 인코딩
- raw_decoder_t: Length-Prefix → msg_t 디코딩
