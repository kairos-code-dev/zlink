# Phase 0 Findings (ZMP v1)

**Date:** 2026-01-22  
**Owner:** 팀장님  
**Status:** Draft

---

## Summary

- ZMP v1은 고정 8바이트 헤더를 유지하고, READY/ERROR로
  핸드셰이크 가시성을 높인다.
- READY 메타데이터는 옵션으로 제공해 운영 정보를 전달한다.
- Heartbeat는 TTL/Context 확장을 허용하되 레거시 1바이트 형식을
  유지한다.
- 보안/인증 옵션(PLAIN/ZAP/MECHANISM)은 제거 대상이다.
- body_len 상한은 4GB-1(uint32 최대값)으로 확대한다.

---

## Baseline Performance

수집 계획
- 벤치 도구: `benchwithzmq/run_benchmarks.sh`
- 환경변수: `ZLINK_PROTOCOL=zmp`
- 기준 출력: throughput, latency
- 비교는 동일 조건의 ZMTP 기준선과 병행

실행 예시
- `ZLINK_PROTOCOL=zmp benchwithzmq/run_benchmarks.sh --runs 3 --reuse-build`
- `ZLINK_PROTOCOL=zmtp benchwithzmq/run_benchmarks.sh --runs 3 --reuse-build`

---

## Hot Path Notes

송신 경로
- socket -> pipe -> session -> engine -> encoder -> transport

수신 경로
- transport -> decoder -> engine -> session -> pipe -> socket

---

## Handshake/Metadata Notes

- HELLO 교환 후 READY 교환이 완료 조건
- READY는 메타데이터 유무와 관계없이 1회 교환
- ERROR 발생 시 즉시 종료

---

## Risks

- v1 적용 시 구버전과 공존 불가
- 핸드셰이크 규칙 위반 시 초기 연결 실패 위험
- 옵션 제거로 API 변경 발생

---

## Reference Sources

- `src/asio/asio_zmp_engine.cpp`
- `src/asio/asio_engine.cpp`
- `src/zmp_protocol.hpp`
- `benchwithzmq/run_benchmarks.sh`
