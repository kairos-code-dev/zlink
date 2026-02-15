# 멀티 클라이언트 벤치마크 확장 계획 (실행-ready)

## 목적

- `benchwithzlink`와 `benchwithzmq` 벤치에서 기존 1:1 패턴을 멀티 클라이언트 패턴으로 확장한다.
- 서버-클라이언트 구성을 그대로 유지하고, 메시지 크기는 기존 `BENCH_MSG_SIZES` 흐름과 동일하게 사용한다.
- `PAIR` 제외, 멀티 PATTERN은 `MULTI_*` 이름으로 분리한다.

## 기준 설계

- `MULTI_*`는 공통적으로 `1`개 서버 소켓 + `BENCH_MULTI_CLIENTS`개 클라이언트 소켓으로 구성한다.
- 각 멀티 벤치는 기존 결과 출력 규격 `RESULT,lib,pattern,transport,size,metric,value`를 유지한다.
- 스로틀은 `BENCH_MULTI_INFLIGHT`, 시간창은 `BENCH_MULTI_WARMUP_SECONDS`/`BENCH_MULTI_MEASURE_SECONDS`로 제한한다.
- 동시 접속 폭주를 막기 위해 `BENCH_MULTI_CONNECT_CONCURRENCY`를 적용한다.
- 메시지 크기는 기존 시나리오와 동일한 `BENCH_MSG_SIZES` 사용(`BENCH_MULTI_STREAM_MSG_SIZES`는 `MULTI_STREAM` 전용).

## 포함 범위

공통 1:1 확장 멀티 패턴: `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `ROUTER_ROUTER_POLL`, `PUBSUB`

zlink-only 멀티 패턴: `MULTI_GATEWAY`, `MULTI_SPOT`

기타 포함: `MULTI_STREAM`

제외 패턴: `PAIR`

## 파일/타깃 매핑

### `core/bench/benchwithzlink`

| Pattern | Current | Baseline | 위치 |
|---|---|---|---|
| MULTI_DEALER_DEALER | `comp_current_multi_dealer_dealer` | `comp_baseline_multi_dealer_dealer` | `current/bench_current_multi_dealer_dealer.cpp`, `baseline/bench_baseline_multi_dealer_dealer.cpp` |
| MULTI_DEALER_ROUTER | `comp_current_multi_dealer_router` | `comp_baseline_multi_dealer_router` | `current/bench_current_multi_dealer_router.cpp`, `baseline/bench_baseline_multi_dealer_router.cpp` |
| MULTI_ROUTER_ROUTER | `comp_current_multi_router_router` | `comp_baseline_multi_router_router` | `current/bench_current_multi_router_router.cpp`, `baseline/bench_baseline_multi_router_router.cpp` |
| MULTI_ROUTER_ROUTER_POLL | `comp_current_multi_router_router_poll` | `comp_baseline_multi_router_router_poll` | `current/bench_current_multi_router_router_poll.cpp`, `baseline/bench_baseline_multi_router_router_poll.cpp` |
| MULTI_PUBSUB | `comp_current_multi_pubsub` | `comp_baseline_multi_pubsub` | `current/bench_current_multi_pubsub.cpp`, `baseline/bench_baseline_multi_pubsub.cpp` |
| MULTI_GATEWAY | `comp_current_multi_gateway` | `comp_baseline_multi_gateway` | `current/bench_current_multi_gateway.cpp`, `baseline/bench_baseline_multi_gateway.cpp` |
| MULTI_SPOT | `comp_current_multi_spot` | `comp_baseline_multi_spot` | `current/bench_current_multi_spot.cpp`, `baseline/bench_baseline_multi_spot.cpp` |

- 공통 유틸: `core/bench/benchwithzlink/common/bench_common_multi.hpp`

### `core/bench/benchwithzmq`

| Pattern | zlink 타겟 | libzmq 타겟 | 위치 |
|---|---|---|---|
| MULTI_DEALER_DEALER | `comp_zlink_multi_dealer_dealer` | `comp_std_zmq_multi_dealer_dealer` | `zlink/bench_zlink_multi_dealer_dealer.cpp`, `libzmq/bench_zmq_multi_dealer_dealer.cpp` |
| MULTI_DEALER_ROUTER | `comp_zlink_multi_dealer_router` | `comp_std_zmq_multi_dealer_router` | `zlink/bench_zlink_multi_dealer_router.cpp`, `libzmq/bench_zmq_multi_dealer_router.cpp` |
| MULTI_ROUTER_ROUTER | `comp_zlink_multi_router_router` | `comp_std_zmq_multi_router_router` | `zlink/bench_zlink_multi_router_router.cpp`, `libzmq/bench_zmq_multi_router_router.cpp` |
| MULTI_ROUTER_ROUTER_POLL | `comp_zlink_multi_router_router_poll` | `comp_std_zmq_multi_router_router_poll` | `zlink/bench_zlink_multi_router_router_poll.cpp`, `libzmq/bench_zmq_multi_router_router_poll.cpp` |
| MULTI_PUBSUB | `comp_zlink_multi_pubsub` | `comp_std_zmq_multi_pubsub` | `zlink/bench_zlink_multi_pubsub.cpp`, `libzmq/bench_zmq_multi_pubsub.cpp` |

## 런처/라우팅 수정

- `core/bench/benchwithzlink/run_comparison.py` 추가: `MULTI_*` 패턴 목록 추가, `select_transports`에서 `MULTI_GATEWAY`/`MULTI_SPOT`를 `STREAM_TRANSPORTS`로 강제, `PATTERN=ALL` 동작은 `STREAM` 및 `MULTI_*` 포함
- `core/bench/benchwithzmq/run_comparison.py` 추가: `MULTI_*` 패턴 목록 추가 및 멀티 전용 타임아웃/환경변수 사용
- `core/bench/benchwithzlink/CMakeLists.txt` 추가: `comp_current_multi_*`, `comp_baseline_multi_*` 타깃 등록
- `core/bench/benchwithzmq/CMakeLists.txt` 추가: `comp_zlink_multi_*`, `comp_std_zmq_multi_*` 타깃 등록
- `core/bench/benchwithzlink/run_benchmarks.sh`, `core/bench/benchwithzmq/run_benchmarks.sh` 추가: 멀티 환경변수 옵션 노출(`--multi-*`), `PATTERN=ALL` 사용 시 멀티 패턴 포함 문서 표기

## 환경변수 계약

공통 멀티 변수:

| 변수 | 기본값 |
|---|---|
| `BENCH_MULTI_CLIENTS` | `100` |
| `BENCH_MULTI_INFLIGHT` | `30` |
| `BENCH_MULTI_CONNECT_CONCURRENCY` | `128` |
| `BENCH_MULTI_WARMUP_SECONDS` | `3` |
| `BENCH_MULTI_MEASURE_SECONDS` | `10` |
| `BENCH_MULTI_DRAIN_MS` | `300` |

메시지 크기: `BENCH_MSG_SIZES` 기존 규격 동일

## 실행/검증 기준

- 기본 검증: `BENCH_MULTI_CLIENTS=100`, `BENCH_MULTI_WARMUP_SECONDS=3`, `BENCH_MULTI_MEASURE_SECONDS=10`
- 기본 검증 명령: `core/bench/benchwithzlink/run_benchmarks.sh --pattern ALL --runs 1`
- 기본 검증 명령: `core/bench/benchwithzmq/run_benchmarks.sh --pattern ALL --runs 1`
- 부하 검증: `BENCH_MULTI_CLIENTS=1000`로 재실행
- 수집/안정성 점검: 타임아웃/`no_data` 오류 없이 각 패턴/전송에서 `throughput` 라인 수집
- 수집/안정성 점검: `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `ROUTER_ROUTER_POLL`, `PUBSUB`가 지원하는 transport를 모두 수집
- 수집/안정성 점검: 기존 1:1 패턴(`PAIR`, `PUBSUB`, `DEALER_DEALER`, `ROUTER_ROUTER`, `STREAM`) 결과 포맷 및 baseline 비교 동작이 변하지 않음

## 리스크

- `GATEWAY`/`SPOT` 계열은 `STREAM_TRANSPORTS`(기본 `tcp/tls/ws/wss`)만 사용하며 `inproc/ipc`는 제외됨
- `SPOT` 멀티는 기존 스레딩 시그널/큐 특성상 latency가 불안정할 수 있어 throughput을 1차 지표로 사용한다
- `benchmark_multi_*` 파일은 타임아웃 상한에 민감하므로 고부하 반복 실행 시 소켓 한도 조정이 필요할 수 있다

## 완료 후 상태

- 멀티 패턴이 `run_comparison`과 빌드 타깃에서 동작 가능한 완성 상태
- zlink 패턴군에서 `PAIR`는 1:1 벤치로 유지
- 결과 파서 호환성을 위해 라인 포맷 변경 없음
