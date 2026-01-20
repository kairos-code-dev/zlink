# Asio I/O 대용량 최적화 계획

**Date:** 2026-01-22  
**Owner:** 팀장님  
**Author:** Codex (Planning Agent)  
**Scope:** Asio I/O 경로(특히 64KB 이상 대용량 전송) 성능 개선  
**Primary Goal:** 64KB 이상 구간에서 libzmq 대비 성능 저하 완화  

---

## 1. 목표 (Goals)

1) 64KB 이상 메시지에서 throughput 저하 원인 규명
2) Asio 기반 구조 유지
3) 프로토콜(ZMP/ZMTP Lite) 변경 최소화
4) 기존 테스트/벤치 통과

---

## 2. 범위 / 비범위 (Scope / Non-goals)

- 범위
  - TCP/IPC 전송 경로의 write 경로 최적화
  - Asio 엔진의 출력 파이프라인 개선
  - 대용량 메시지(>=64KB) 전송 시 구조 개선

- 비범위
  - Asio 이외 poller 도입
  - 소켓 패턴 변경
  - 프로토콜 규격 변경(ZMP/ZMTP 스펙 변경)

---

## 3. 전제 및 성공 기준

- 전제
  - `ZLINK_PROTOCOL` 기본값은 ZMP
  - 벤치마크는 `benchwithzmq/run_benchmarks.sh` 사용
  - 64KB 이상 구간에서 TCP/IPC 성능 저하가 핵심 문제
  - 반복 실행 시 libzmq 기준선은 캐시(`benchwithzmq/libzmq_cache.json`) 재사용

- 성공 기준
  - ROUTER_ROUTER 기준 64KB 이상 throughput이 현재 대비 유의미하게 개선(목표: +10% 이상)
  - ROUTER_ROUTER 기준 1KB 이하 구간 성능 회귀 없음
  - `ctest --output-on-failure` 통과

### 3.1 실행 시간/타임아웃 대응

- 기본: `--runs 10`, 전체 사이즈 유지
- 타임아웃 발생 시:
  - `--runs 5` 또는 `--runs 3`로 축소
  - 대용량만 확인 시 `--msg-sizes 65536,131072,262144` 사용
- 결과는 `docs/team/20260122_asio-io-optimizations/results/` 아래 파일로 저장

### 3.2 핵심 가설 (Root Cause Hypothesis)

- 헤더/바디 분리 전송으로 메시지당 syscall이 늘어남
  - ZMP/ZMTP 인코더가 헤더와 바디를 분리 출력
  - 엔진이 이를 단일 버퍼로만 전송하여 write가 2회 이상 발생
- libzmq는 `writev/sendmsg`로 헤더+바디를 한 번에 전송
- encoder buffer lifetime 규칙 때문에 `encode()` 두 번 호출로
  헤더/바디를 묶는 방식은 위험 (async 생명주기 문제)

핵심 파일
- `src/zmp_encoder.cpp`, `src/v3_1_encoder.cpp`, `src/encoder.hpp`
- `src/asio/asio_engine.cpp`

---

## 4. 작업 순서 (Work Order)

### Step 0. 기준선 재수집 (Baseline)

1) 현재 `main` 기준 ROUTER_ROUTER만 재실행 및 결과 저장  
   - 명령: `benchwithzmq/run_benchmarks.sh --pattern ROUTER_ROUTER --runs 10 --reuse-build --output docs/team/20260122_asio-io-optimizations/results/baseline_router_router.txt`
2) 결과 파일 분리 저장 (ROUTER_ROUTER, tcp/ipc/inproc별)

참고 소스/스크립트
- `benchwithzmq/run_benchmarks.sh`

---

### Step 1. Vector I/O 지원 추가 (writev/sendmsg)

1) 전송 계층에 vectored write API 추가
   - `async_writev` / `writev` 형태로 확장
2) TCP/IPC에서 `writev` 또는 `sendmsg` 기반 구현
3) 지원 불가 환경은 기존 단일 버퍼 경로로 fallback

참고 소스
- `src/asio/i_asio_transport.hpp`
- `src/asio/tcp_transport.cpp`
- `src/asio/ipc_transport.cpp`

---

### Step 2. 헤더+바디 Gather Fast Path (>=64KB)

1) 대용량 메시지는 헤더+바디를 iovec로 묶어 단일 전송
2) 헤더 버퍼는 엔진 내부 고정 버퍼로 유지 (async 생명주기 보장)
3) `_write_pending` 동안 encoder/state 머신이 다시 돌지 않도록 보장
4) 적용 범위는 ROUTER_ROUTER부터 시작

참고 소스
- `src/asio/asio_engine.cpp`
- `src/asio/asio_zmp_engine.cpp`
- `src/asio/asio_zmtp_engine.cpp`
- `src/zmp_encoder.cpp`
- `src/v3_1_encoder.cpp`
- `src/encoder.hpp`

---

### Step 3. I/O 경로 계측 (Instrumentation)

1) write 경로에서 다음 지표 수집
   - `async_write` 호출 횟수/바이트
   - `write_some` 호출 횟수/바이트/EAGAIN
   - vectored write 호출 횟수/바이트
2) 계측은 env 플래그로 켜고 끔 (성능 영향 최소화)

참고 소스
- `src/asio/asio_engine.cpp`
- `src/asio/tcp_transport.cpp`
- `src/asio/ipc_transport.cpp`
- `src/asio/i_asio_transport.hpp`

---

### Step 4. Async Write 루프 최적화 (보조)

1) `async_write`(composed op) 대신 `async_write_some` 루프 사용 검토
2) 부분 전송 시 자체 루프로 이어쓰기 (handler/스케줄링 비용 절감)

참고 소스
- `src/asio/tcp_transport.cpp`
- `src/asio/ipc_transport.cpp`
- `src/asio/asio_engine.cpp`

---

### Step 5. 배치/버퍼 튜닝 (보조)

1) `out_batch_size` 상향 또는 대용량 기준 동적 확장
2) read buffer 크기 상향 (64KB 이상 구간 영향 확인)

참고 소스
- `src/options.cpp`
- `src/asio/asio_engine.hpp`

---

### Step 6. 검증 및 벤치 재측정

1) `ctest --output-on-failure` 실행  
2) Step 0과 동일한 조건으로 ROUTER_ROUTER만 벤치 재수집  
3) 개선 여부 기록 및 비교표 생성  
4) 개선 효과가 확인되면 다른 패턴으로 확장 실행

참고 명령
- 캐시 재사용: `benchwithzmq/run_benchmarks.sh --pattern ROUTER_ROUTER --runs 10 --reuse-build --skip-libzmq --output docs/team/20260122_asio-io-optimizations/results/router_router_candidate.txt`

참고 소스/스크립트
- `benchwithzmq/run_benchmarks.sh`
- `tests/test_*.cpp`
- `unittests/unittest_*.cpp`

---

### Step 7. 결과 정리 및 롤백 플래그

1) 개선이 미미할 경우 원인과 한계 문서화
2) 신규 fast path는 환경변수로 끄기 가능하도록 유지

참고 소스
- `src/asio/asio_engine.cpp`
- `src/options.hpp` (옵션/플래그 후보)

---

## 5. 리스크 및 대응

- 리스크: 버퍼 생명주기 오류, 부분 전송 처리 누락, 플랫폼별 차이  
- 대응: env 플래그로 기능 on/off, 단계적 적용, 테스트/벤치 반복

---

## 6. 관련 소스 위치 (요약)

- 엔진: `src/asio/asio_engine.cpp`, `src/asio/asio_zmp_engine.cpp`, `src/asio/asio_zmtp_engine.cpp`
- 전송: `src/asio/tcp_transport.cpp`, `src/asio/ipc_transport.cpp`, `src/asio/i_asio_transport.hpp`
- 인코더: `src/zmp_encoder.cpp`, `src/v3_1_encoder.cpp`
- 벤치: `benchwithzmq/run_benchmarks.sh`

## 7. 관련 코드 위치 (상세)

- 출력 파이프라인
  - `src/asio/asio_engine.cpp`: `start_async_write`, `speculative_write`, `on_write_complete`, `process_output`
  - `src/asio/asio_zmp_engine.cpp`: `restart_output`, `start_async_write`
  - `src/asio/asio_zmtp_engine.cpp`: `restart_output`, `start_async_write`
- 전송 계층 write 경로
  - `src/asio/tcp_transport.cpp`: `async_write_some`, `write_some`, `supports_speculative_write`
  - `src/asio/ipc_transport.cpp`: `async_write_some`, `write_some`, `supports_speculative_write`
  - `src/asio/i_asio_transport.hpp`: write API 추상화 확장 지점
- 인코더 버퍼/프레이밍
  - `src/zmp_encoder.cpp`: ZMP 헤더/바디 인코딩
  - `src/v3_1_encoder.cpp`: ZMTP Lite 인코딩
  - `src/encoder.hpp`: encoder buffer lifetime 규칙
- 결과/벤치
  - `benchwithzmq/run_benchmarks.sh`
  - `docs/team/20260122_asio-io-optimizations/results/` (결과 저장 경로)
