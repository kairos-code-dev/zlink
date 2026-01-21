# ZMP v1 적용 내용 정리

Date: 2026-01-21
Branch: feature/zmp-protocol-only
Scope: ZMP v1 확장 + 보안/인증 옵션 제거 + 테스트/문서 정리

## 적용 내용
- ZMP v1 컨트롤: READY/ERROR, heartbeat TTL/Context 확장, MORE+IDENTITY 허용.
- READY 메타데이터 옵션(`ZMQ_ZMP_METADATA`) 추가, 기본 프로퍼티(Socket-Type, Identity) 전송.
- heartbeat TTL 합의 규칙 적용(min(local, remote)).
- ZMP/WS 엔진에 ERROR 프레임 송신 및 handshake 오류 코드 전달.
- ASIO 엔진 종료 경로 안전화(termination 중 write 재시작 차단, delete 지연).
- 문서에서 PLAIN/ZAP/MECHANISM 옵션 제거 및 ZMP 옵션 반영.

## API/옵션 변경
- Added: `ZMQ_ZMP_METADATA`
- Removed: `ZMQ_MECHANISM`, `ZMQ_PLAIN_*`, `ZMQ_ZAP_DOMAIN`, `ZMQ_ZAP_ENFORCE_DOMAIN`

## 테스트
- `ctest --output-on-failure` (63 tests passed, fuzzers skipped)

## 벤치마크
- 요약: `docs/team/20260120_zmp-protocol/benchmarks.md`
- Raw baseline:
  - `benchwithzmq/baseline/20260121/bench_ALL_20260121_141453_zmp_v1.txt`
  - `benchwithzmq/baseline/20260121/bench_ALL_20260121_141758.txt`
  - `benchwithzmq/baseline/20260121/bench_ALL_20260121_152423.txt`

## 주요 변경 위치
- `src/zmp_protocol.hpp`, `src/zmp_metadata.hpp`, `src/zmp_decoder.cpp`
- `src/asio/asio_zmp_engine.cpp`, `src/asio/asio_ws_engine.cpp`
- `src/asio/asio_engine.cpp`
- `include/zmq.h`, `src/options.cpp`, `doc/zmq_getsockopt.txt`, `doc/zmq_setsockopt.txt`
