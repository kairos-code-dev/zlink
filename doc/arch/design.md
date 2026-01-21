# ZMP 프로토콜 설계 문서 (v1)

**Date:** 2026-01-22  
**Owner:** 팀장님  
**Author:** Codex (Planning Agent)  
**Scope:** zlink 전용 커스텀 프로토콜, ZMTP 완전 비호환

---

## 1. 목표

- 핫패스 단순화(고정 헤더 + 최소 분기)
- 핸드셰이크 안정성 강화(READY/ERROR)
- 운영 가시성 향상(메타데이터 교환 옵션)
- 기존 ZMQ 소켓 패턴 유지
- 보안/인증 옵션 제거(전송 계층 정책으로 전환)

---

## 2. 프레임 구조

### 2.1 고정 헤더 (8 bytes)

- Byte 0: Magic (0x5A)
- Byte 1: Version (0x02)
- Byte 2: Flags
  - bit0: MORE
  - bit1: CONTROL
  - bit2: IDENTITY
  - bit3: SUBSCRIBE
  - bit4: CANCEL
  - bit5~7: reserved (0)
- Byte 3: Reserved (0)
- Byte 4..7: Body length (uint32, big-endian)

### 2.2 바디

- body_len 바이트 raw payload
- 최대 허용 크기: 4GB-1 (uint32 최대값)

---

## 3. 플래그 조합 규칙

허용:
- 단일 플래그
- MORE + IDENTITY

금지:
- CONTROL + IDENTITY
- CONTROL + MORE (제어 프레임은 단일 프레임만 허용)
- SUBSCRIBE/CANCEL과 다른 플래그 결합
- SUBSCRIBE + CANCEL 동시 사용
- 예약 비트(5~7) 사용

---

## 4. 핸드셰이크

### 4.1 HELLO (CONTROL)

- 연결 직후 양측 1회 교환
- 바디 포맷:
  - Byte 0: Control type (0x01)
  - Byte 1: Socket type (1 byte)
  - Byte 2: Identity length (uint8)
  - Byte 3..: Identity bytes

### 4.2 READY/ERROR

- READY는 로컬 HELLO 송신 직후 연속 전송 가능(파이프라인 허용)
- READY 수신 시 핸드셰이크 완료
- 오류 발생 시 ERROR 송신 후 연결 종료
- READY 이전 데이터 프레임 수신 시 EPROTO 처리

---

## 5. READY 메타데이터(옵션)

- ZMTP property 인코딩 규격 재사용
- 기본 프로퍼티: Socket-Type, Identity
- 확장 프로퍼티: Resource 등(네임스페이스 권장)
- 기본 비활성(옵션으로 enable)
  - 옵션: `ZMQ_ZMP_METADATA` (0/1)

---

## 6. Heartbeat TTL/Context(옵션)

- HEARTBEAT에 TTL(u16) + ctx 추가
- ACK는 ctx 에코
- 레거시 1바이트 HEARTBEAT 허용

---

## 7. ROUTER Identity 처리

- IDENTITY 프레임은 ROUTER 수신 경로 전용
- 메시지 첫 프레임에서만 허용
- MORE와 조합 가능(라우팅 + 멀티파트 허용)

---

## 8. SUBSCRIBE/CANCEL 처리

- SUBSCRIBE/CANCEL은 데이터 프레임 플래그
- 바디는 구독 토픽 바이트열
- XPUB/XSUB은 기존 구독 규칙 유지

---

## 9. 오류 처리

- Magic/Version/Reserved 불일치: 연결 종료
- body_len 상한 초과: 연결 종료
- CONTROL 규칙 위반: 연결 종료
- 재동기화 없음

---

## 10. 런타임 모드

- `ZLINK_PROTOCOL=zmp|zmtp`
- 보안은 전송 계층 정책에 따름(TLS 권장)

---

## 11. 참고 소스 위치

- `src/asio/asio_zmp_engine.cpp`
- `src/asio/asio_zmp_engine.hpp`
- `src/zmp_encoder.cpp`
- `src/zmp_decoder.cpp`
- `src/zmp_protocol.hpp`
- `src/zmp_metadata.hpp`
- `src/session_base.cpp`
- `src/pipe.cpp`
