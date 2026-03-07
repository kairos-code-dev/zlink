# 2026-03-07 Perf Policy Audit

## Status Update

- This audit captured the in-progress failures and policy gaps observed earlier on
  2026-03-07.
- The work described here has since been completed and revalidated on
  `core/v4.0.2`.
- Final root causes, fixes, and passing validation runs are summarized in:
  [2026-03-07-perf-policy-stabilization-v402.md](/home/hep7/project/kairos/zlink/doc/bug/2026-03-07-perf-policy-stabilization-v402.md)

## 범위

- `doc/perf`
- `core/perf`
- 대상 정책:
  - 공통 원칙
  - single = `blocking send + blocking recv + nonblocking drain`
  - multi = `PollIn + nonblocking drain`, `send(..., DONTWAIT) 1회 + pending + PollOut on-demand`

## 현재 확인된 문제

### 1. GATEWAY multi client readiness가 `connection_count()`에 과도하게 의존

- 위치:
  - `core/perf/multi/src/perf_gateway_client.cpp`
  - `core/src/services/gateway/gateway.cpp`
- 증상:
  - direct smoke에서 `gateway client: gateway connection ready 0/2`
  - wrapper smoke에서 `throughput/bandwidth/latency` 전부 `FAIL`
- 원인:
  - perf client가 실제 송신 가능 여부를 `gateway_connection_count()`로 선판정한다.
  - 이 readiness는 discovery snapshot/monitor event/peer ready 전파 timing에 민감하다.
  - 측정 정책 관점에서도 `send(..., DONTWAIT)` 기반 pending 제어보다 강한 사전 조건이다.
- 수정 상태:
  - `gateway.cpp`의 lazy pool refresh task wakeup 누락은 수정 완료
  - 그러나 perf smoke는 여전히 실패하며, perf readiness 기준 자체를 policy에 맞게 재설계해야 한다.
  - direct client/server 재현에서 preflight 단계 `gateway_send`가 `EFSM` (`Operation cannot be accomplished in current state`)로 실패하는 것도 확인했다.
  - 즉 현재 GATEWAY multi client는 "ready 판정"과 "실제 송신 시작" 모두를 다시 설계해야 한다.

### 2. multi helper가 hot loop에서 backoff/yield를 사용

- 위치:
  - `core/perf/multi/common/perf_client_helpers.hpp`
  - `core/perf/multi/src/perf_spot_client.cpp`
  - `core/perf/multi/src/perf_spot_server.cpp`
  - `core/perf/multi/src/perf_pubsub_server.cpp`
  - `core/perf/multi/src/perf_router_router_server.cpp`
  - `core/perf/multi/src/perf_dealer_router_server.cpp`
  - `core/perf/multi/src/perf_gateway_server.cpp`
  - stream server 계열
- 증상:
  - `std::this_thread::yield()`
  - `sleep_for(1ms/5ms/10ms)`
  - idle backoff
- 정책 위반:
  - hot loop 안 `sleep / yield` 금지
  - poll-driven event loop가 기준이어야 함

### 3. multi send path가 blocking send 또는 사실상 즉시 재시도를 사용

- 위치:
  - `core/perf/multi/common/perf_client_helpers.hpp`
  - `core/perf/multi/src/perf_dealer_dealer_client.cpp`
  - `core/perf/multi/src/perf_gateway_client.cpp`
  - `core/perf/multi/src/perf_gateway_server.cpp`
  - `core/perf/multi/src/perf_pubsub_server.cpp`
  - `core/perf/multi/src/perf_spot_server.cpp`
  - stream server 계열
- 증상:
  - `zlink_send(..., 0)` blocking send
  - `while(send failed)` 류 재시도
  - pending을 poller state로 표현하지 않음
- 정책 위반:
  - shared event loop에서 blocking send 금지
  - `DONTWAIT 1회 + pending + PollOut on-demand` 필요

### 4. multi recv path에 batch cap / helper cap이 남아 있음

- 위치:
  - `core/perf/multi/src/perf_spot_client.cpp`
- 증상:
  - `PERF_SPOT_RECV_BATCH`
  - `recv_batch_limit`
- 정책 위반:
  - readiness 이후 recv drain은 `EAGAIN`까지 무제한이어야 함
  - recv cap 금지

### 5. single policy와 다르게 poller/timeout 중심 recv가 남아 있음

- 위치:
  - `core/perf/single/src/perf_router_router_poll.cpp`
  - 일부 single helper/패턴
- 증상:
  - `ROUTER_ROUTER_POLL`이 이름 그대로 poll 기반 구현을 유지
- 정책 위반:
  - single 기본 메커니즘은 poller가 아니라 blocking recv + nonblocking drain

### 6. hot loop 안 문자열/로그/동적 컨테이너 의존이 남아 있음

- 위치:
  - `core/perf/multi/src/perf_gateway_server.cpp`
  - `core/perf/multi/src/perf_gateway_client.cpp`
  - 기타 패턴 파일
- 증상:
  - pending queue에 `std::deque<gateway_request_t>`
  - 오류/상태 문자열 조합
  - 일부 loop 주변 동적 상태 변경
- 정책 위반:
  - hot loop 안 heap alloc / 문자열 생성 / 로그 출력 금지

## 이번 턴에서 반영한 내용

### 문서

- `doc/perf/PERF_POLICY.md`
  - 공통 원칙 추가
  - pattern 해석 규칙 추가
- `doc/perf/PERF_SINGLE_TEST_POLICY.md`
  - single 핵심 정책 명시
- `doc/perf/PERF_MULTI_TEST_POLICY.md`
  - multi 핵심 정책 명시

### core bug fix

- `core/src/services/gateway/gateway.cpp`
  - service pool 최초 생성 시 refresh task wakeup 누락 수정
- `core/tests/discovery/test_gateway.cpp`
  - discovery에 서비스가 먼저 존재하는 상태에서 `gateway_connection_count()`가 초기 refresh를 수행하는 회귀 테스트 추가

## 검증 결과

### 통과

- `test_gateway`
- `test_gateway_handover`
- `test_spot_mode_split`

### 실패

- `./core/perf/run_benchmarks_multi.sh --pattern GATEWAY --transports tcp --msg-sizes 64 --runs 1 --clients 2 --warmup 0 --duration 1 --reuse-build`
- `./core/perf/run_benchmarks_multi.sh --pattern SPOT --transports tcp --msg-sizes 64 --runs 1 --clients 2 --warmup 0 --duration 1 --reuse-build`

실패 결과 파일:

- `core/perf/results/multi/report/perf_linux_20260307_121434.txt`
- `core/perf/results/multi/report/perf_linux_20260307_072802.txt`
- `core/perf/results/multi/report/perf_linux_20260307_125913.txt`

direct 재현 로그:

- `comp_src_gateway_client` direct run
  - `gateway client: send failed: Operation cannot be accomplished in current state`
  - `gateway client: preflight warmup failed`

## 다음 수정 순서

1. `multi/common/perf_client_helpers.hpp`
   - `yield/backoff` 제거
   - echo helper를 `pending + PollOut on-demand` 구조로 교체
2. `perf_dealer_dealer_client.cpp`
   - blocking send 루프 제거
3. `perf_gateway_client.cpp`
   - `connection_count()` readiness 의존 제거
   - preflight/active loop를 policy형 send pending으로 교체
4. `perf_gateway_server.cpp`
   - pending deque 기반 재시도 구조를 policy형 event loop로 단순화
5. `perf_spot_client.cpp`
   - recv batch cap 제거
   - `yield/sleep` 제거
6. `perf_spot_server.cpp`, `perf_pubsub_server.cpp`, stream server 계열
   - send path를 `DONTWAIT + PollOut on-demand`로 전환
7. single 패턴 정리
   - `ROUTER_ROUTER_POLL`을 이름만 유지하고 정책상 blocking recv 경로로 통일
