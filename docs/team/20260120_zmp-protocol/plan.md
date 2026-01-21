# ZMP v1 확장 및 정리 계획

**Date:** 2026-01-22  
**Owner:** 팀장님  
**Author:** Codex (Planning Agent)  
**Scope:** ZMP 전용 경로 강화 + 보안/인증 옵션 제거  
**Primary Goal:** 성능 유지/개선 + 운영 가시성 향상

---

## 1. 목표 (Goals)

1) ZMP v1 확장 기능 도입(READY/ERROR, metadata, heartbeat TTL/ctx)
2) 핸드셰이크 안정성 및 실패 원인 가시화
3) 핫패스 비용 최소화(핸드셰이크 1회 비용만 허용)
4) 보안/인증 옵션 제거로 API 단순화
5) 기존 소켓 패턴(PAIR/DEALER/ROUTER/PUB/SUB/XPUB/XSUB) 유지

---

## 2. 비목표 (Non-Goals)

- 대용량 프레임 확장(64-bit length)
- ZMTP 상호운용/호환성 확보
- 버전 호환성 유지(구버전과의 공존)
- 라우팅/구독 동작의 근본적 변경

---

## 3. 변경 사항 요약 (v1)

### 3.1 프레임/헤더
- Version byte = 0x02
- 헤더 구조는 v0와 동일(8 bytes 고정)
- body_len 상한은 4GB-1(uint32 최대값) 유지

### 3.2 제어 프레임 확장
- READY(0x04): 핸드셰이크 완료 명시
- ERROR(0x05): 실패 이유 전달

### 3.3 READY 메타데이터(옵션)
- ZMTP property 인코딩 재사용
- 기본: Socket-Type, Identity
- 확장: Resource 등(네임스페이스 권장)
- 기본 비활성(옵션으로 enable)

### 3.4 Heartbeat TTL/Context(옵션)
- HEARTBEAT에 TTL(u16) + ctx 추가
- ACK에 ctx 에코
- 레거시 1바이트 heartbeat 허용

### 3.5 플래그 조합 허용
- MORE + IDENTITY
- 나머지 조합은 명시적으로 금지

---

## 4. 보안/인증 옵션 제거 범위

제거 대상(공개 API에서 삭제):
- `ZMQ_MECHANISM`
- `ZMQ_PLAIN_SERVER`
- `ZMQ_PLAIN_USERNAME`
- `ZMQ_PLAIN_PASSWORD`
- `ZMQ_ZAP_DOMAIN`

정리 범위:
- `include/zmq.h` 옵션 정의 제거
- `src/options.*` set/get 처리 제거
- 관련 문서/테스트 정리
- 내부 메커니즘 코드는 단계적으로 제거(필요 시 별도 작업)

---

## 5. 구현 단계 (Phases)

### Phase 1. 프로토콜 업데이트
- `src/zmp_protocol.hpp`에 v1 상수/제어 타입 추가
- `src/zmp_decoder.cpp` 플래그 조합 규칙 완화
- 컨트롤 프레임 길이 검증 강화

### Phase 2. 핸드셰이크/메타데이터
- `src/asio/asio_zmp_engine.cpp`에 READY/ERROR 처리 추가
- 메타데이터 인코딩/파싱은 `src/mechanism.cpp` 재사용
- 메타데이터 전송 on/off 옵션 추가(기본 off)

### Phase 3. Heartbeat TTL/Context
- HEARTBEAT/ACK 확장 파싱 및 TTL 합의 적용
- 기존 heartbeat interval/timeout 옵션과 연동

### Phase 4. 보안/인증 옵션 제거
- 옵션 정의/처리 제거
- 문서/테스트 업데이트
- 필요 시 내부 메커니즘 코드 정리

### Phase 5. 검증/벤치
- 핸드셰이크/메타데이터/heartbeat 단위 테스트 추가
- 기존 벤치 재실행(성능 회귀 확인)

---

## 6. 수정 대상 파일 (후보)

핵심
- `src/zmp_protocol.hpp`
- `src/zmp_encoder.cpp`
- `src/zmp_decoder.cpp`
- `src/asio/asio_zmp_engine.cpp`
- `src/asio/asio_engine.cpp`

보조
- `src/mechanism.cpp` (메타데이터 인코딩/파싱 재사용)
- `include/zmq.h` (옵션 제거)
- `src/options.cpp` / `src/options.hpp` (옵션 제거)

테스트/문서
- `tests/test_*.cpp`
- `unittests/unittest_*.cpp`
- `docs/team/20260120_zmp-protocol/*.md`

---

## 7. 리스크 및 대응

- v1 적용 시 구버전과 공존 불가
  - 대응: 배포 시점에 일괄 업그레이드
- 핸드셰이크 흐름 변경으로 초기 연결 실패 위험
  - 대응: ERROR 프레임 이유 명확화, 통합 테스트 추가
- 옵션 제거로 API 변경
  - 대응: 문서/릴리즈 노트에 명확히 공지

---

## 8. 산출물

- `docs/team/20260120_zmp-protocol/zmp_v1_extension_spec.md`
- 업데이트된 구현 및 테스트
- 변경 요약 문서

---

## 9. 다음 단계 (Immediate Next)

1) Phase 1 구현 상세 설계 확정
2) 메타데이터 옵션 명칭 확정
3) 코드 변경 착수
