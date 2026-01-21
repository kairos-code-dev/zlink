# Phase 1 Implementation Notes (ZMP v1)

**Date:** 2026-01-22  
**Owner:** 팀장님  
**Status:** Implemented

---

## Encoder/Decoder

- 고정 8바이트 헤더 처리
- flags 조합 규칙 완화(MORE+IDENTITY)
- CONTROL 프레임은 단일 프레임만 허용(MORE 금지)
- body_len=0 처리
- max body_len=4GB-1(uint32 최대값) 유지

---

## Handshake/HELLO/READY/ERROR

- READY는 로컬 HELLO 송신 직후 연속 전송 가능(파이프라인 허용)
- READY 메타데이터는 옵션 활성 시에만 포함
- ERROR 발생 시 이유 전달 후 종료
- 핸드셰이크 타임아웃: `ZMQ_HANDSHAKE_IVL` 적용
- READY 이전 데이터 프레임 수신 시 EPROTO 처리

---

## Metadata

- `src/zmp_metadata.hpp`의 property 인코딩/파싱 사용
- 기본 프로퍼티: Socket-Type, Identity
- 확장 프로퍼티: Resource 등

---

## Heartbeat TTL/Context

- HEARTBEAT 확장형 파싱 및 TTL 합의 적용
- ACK는 ctx 에코
- 레거시 1바이트 HEARTBEAT 허용

---

## ROUTER Identity

- IDENTITY 프레임은 ROUTER 수신 경로 전용
- 메시지 첫 프레임으로 제한
- MORE와 조합 가능

---

## PUB/SUB/XPUB/XSUB

- SUBSCRIBE/CANCEL 플래그 처리
- 바디는 토픽 바이트열
- XPUB/XSUB 경로에 전달

---

## Tests

- decoder 플래그/최대 길이 단위 테스트
- metadata 인코딩/파싱 단위 테스트
- TTL 합의(helper) 단위 테스트
- ZMP metadata on/off + ERROR(잘못된 HELLO) 통합 테스트
- heartbeat TTL/ctx 통합 테스트
- ZMP WS/WSS PAIR 통합 테스트

## Test Results

- `ctest --output-on-failure` (64 tests passed, fuzzers skipped)

---

## Reference Sources

- `src/asio/asio_zmp_engine.cpp`
- `src/asio/asio_engine.cpp`
- `src/zmp_encoder.cpp`
- `src/zmp_decoder.cpp`
- `src/zmp_protocol.hpp`
- `src/mechanism.cpp`
- `tests/test_*.cpp`
- `unittests/unittest_*.cpp`
