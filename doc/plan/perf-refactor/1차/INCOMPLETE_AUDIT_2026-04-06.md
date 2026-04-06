# perf-refactor 완료 여부 코드 감사

작성일: 2026-04-06
최종 갱신: 2026-04-06

## 감사 기준

- 확인 대상: `doc/plan/perf-refactor/*.md`
- 대조 기준: 계획 문서의 `완료 정의`, `완료 상태`, `PROGRESS.md`
- 실제 확인 범위:
  - 코드: `core/perf`, `bindings/*/perf`
  - 저장된 결과 파일
  - 직접 실행 및 스모크 결과
- 제외:
  - 전 타깃 전체 빌드/전체 스모크를 전부 다시 돌리는 방식은 사용하지 않음

## 결론

초기 감사 시점에 `미완료`로 분류했던 9개 항목은 현재 모두 코드와 결과 파일 기준으로 완료 판정이다.
이 문서는 각 항목의 현재 상태와 검증 근거를 남기는 최신 정리본이며, 현재 기준으로 남은 미완료 항목은 없다.

## 항목별 현재 상태

### 1. core/perf: `bench_common.hpp <= 400줄` 목표 달성

- 현재 상태: 완료
- 확인 내용:
  - `core/perf/single/common/bench_common.hpp`는 더 이상 대형 단일 파일이 아니고, 공통 런타임 로직이 분리되어 있다.
  - `bench_common.hpp`는 헤더 책임만 유지하고, 런타임 구현은 별도 파일로 이동했다.
- 검증 근거:
  - 코드 확인: [core/perf/single/common/bench_common.hpp](/home/hep7/project/kairos/zlink/core/perf/single/common/bench_common.hpp)
  - 코드 확인: [core/perf/single/common/bench_common_runtime.hpp](/home/hep7/project/kairos/zlink/core/perf/single/common/bench_common_runtime.hpp)
  - grep 확인: `rg -n "PERF_UNUSED_SPOT_INTERNAL|wait_for_spot_ready_settle" /home/hep7/project/kairos/zlink/core/perf` 결과 없음
  - smoke 결과: [perf_linux_callback_20260406_170044.txt](/home/hep7/project/kairos/zlink/core/perf/results/single/report/perf_linux_callback_20260406_170044.txt)

### 2. core/perf: SPOT 내부 환경 변수 주입 제거

- 현재 상태: 완료
- 확인 내용:
  - single 및 multi 러너에서 SPOT 내부 env 주입이 제거되었다.
  - 관련 셋업 코드와 잔존 참조가 모두 정리되었다.
- 검증 근거:
  - 코드 확인: [core/perf/run_benchmarks.sh](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)
  - 코드 확인: [core/perf/run_benchmarks_multi.sh](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh)
  - grep 확인: `rg -n "PERF_UNUSED_SPOT_INTERNAL|wait_for_spot_ready_settle" /home/hep7/project/kairos/zlink/core/perf` 결과 없음
  - smoke 결과: [perf_linux_callback_20260406_170044.txt](/home/hep7/project/kairos/zlink/core/perf/results/single/report/perf_linux_callback_20260406_170044.txt)

### 3. bindings/cpp/perf: `settle` 삭제 완료

- 현재 상태: 완료
- 확인 내용:
  - `wait_for_spot_ready_settle()` 제거가 반영되었다.
  - `PERF_MULTI_SPOT_READY_SETTLE_MS` 경로도 남지 않았다.
- 검증 근거:
  - 코드 확인: [bindings/cpp/perf/multi/src/perf_spot_client.cpp](/home/hep7/project/kairos/zlink/bindings/cpp/perf/multi/src/perf_spot_client.cpp)
  - grep 확인: `rg -n "wait_for_spot_ready_settle|PERF_MULTI_SPOT_READY_SETTLE_MS" /home/hep7/project/kairos/zlink/bindings/cpp/perf` 결과 없음
  - smoke 결과: [perf_linux_callback_20260406_165859.txt](/home/hep7/project/kairos/zlink/bindings/cpp/perf/results/multi/report/perf_linux_callback_20260406_165859.txt)

### 4. bindings/dotnet/perf: phase drain/settle 삭제 완료

- 현재 상태: 완료
- 확인 내용:
  - `settle` 관련 필드와 호출 경로가 제거되었다.
  - `PerfEnv.cs`로 환경 변수 해석이 모였다.
- 검증 근거:
  - 코드 확인: [bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfEnv.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfEnv.cs)
  - 코드 확인: [bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfOptions.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfOptions.cs)
  - 코드 확인: [bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfSpot.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfSpot.cs)
  - grep 확인: `rg -n "settle|drainMs|warmupDrainMs|ResolveMultiWarmupDrainMs|ResolveMultiDrainMs|PERF_SETTLE_MS|SingleSettleTimeMs|settleMs" /home/hep7/project/kairos/zlink/bindings/dotnet/perf -g '*.cs'` 결과 없음
  - smoke 결과: [perf_linux_callback_20260406_170213_audit.txt](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/results/single/report/perf_linux_callback_20260406_170213_audit.txt), [perf_linux_callback_20260406_170222_audit.txt](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/results/multi/report/perf_linux_callback_20260406_170222_audit.txt)

### 5. bindings/dotnet/perf: env var 해석 집중 완료

- 현재 상태: 완료
- 확인 내용:
  - 환경 변수 해석은 `PerfEnv.cs`로 집중되었다.
  - single/multi 공통 코드에서 별도 환경 변수 파싱 분산이 사라졌다.
- 검증 근거:
  - 코드 확인: [bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfEnv.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfEnv.cs)
  - 코드 확인: [bindings/dotnet/perf/single/Zlink.BindingBench/common/PerfCommon.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/single/Zlink.BindingBench/common/PerfCommon.cs)
  - 코드 확인: [bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/common/PerfCommon.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/common/PerfCommon.cs)
  - grep 확인: `rg -n "Environment.GetEnvironmentVariable\\(" /home/hep7/project/kairos/zlink/bindings/dotnet/perf -g '*.cs'` 결과가 `PerfEnv.cs`로 집중됨
  - smoke 결과: [perf_linux_callback_20260406_170213_audit.txt](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/results/single/report/perf_linux_callback_20260406_170213_audit.txt), [perf_linux_callback_20260406_170222_audit.txt](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/results/multi/report/perf_linux_callback_20260406_170222_audit.txt)

### 6. bindings/python/perf: 유틸리티 중복 제거 완료

- 현재 상태: 완료
- 확인 내용:
  - 공통 유틸리티가 `perf_metrics.py`로 모였다.
  - single/multi 공통 진입점은 공통 모듈을 재사용하고 있다.
- 검증 근거:
  - 코드 확인: [bindings/python/perf/perf_metrics.py](/home/hep7/project/kairos/zlink/bindings/python/perf/perf_metrics.py)
  - 코드 확인: [bindings/python/perf/single/perf_common.py](/home/hep7/project/kairos/zlink/bindings/python/perf/single/perf_common.py)
  - 코드 확인: [bindings/python/perf/multi/perf_multi_common.py](/home/hep7/project/kairos/zlink/bindings/python/perf/multi/perf_multi_common.py)
  - grep 확인: `rg -n "class CallbackMetrics|def tcp_endpoint|def result_metrics|def print_result_lines|def latency_us_from_message" /home/hep7/project/kairos/zlink/bindings/python/perf /home/hep7/project/kairos/zlink/bindings/python/perf/single /home/hep7/project/kairos/zlink/bindings/python/perf/multi -g '*.py'`
  - smoke 결과: [perf_linux_callback_20260406_171250.txt](/home/hep7/project/kairos/zlink/bindings/python/perf/results/multi/report/perf_linux_callback_20260406_171250.txt)

### 7. bindings/python/perf: single callback-only 정렬 완료

- 현재 상태: 완료
- 확인 내용:
  - single 패턴들은 callback 기반 경로로 정리되었다.
  - `Poller` 기반 패턴 루프와 `safe_poll` 의존 경로는 single 구현에서 제거되었다.
- 검증 근거:
  - 코드 확인: [bindings/python/perf/single/perf_pair.py](/home/hep7/project/kairos/zlink/bindings/python/perf/single/perf_pair.py)
  - 코드 확인: [bindings/python/perf/single/perf_pubsub.py](/home/hep7/project/kairos/zlink/bindings/python/perf/single/perf_pubsub.py)
  - 코드 확인: [bindings/python/perf/single/perf_dealer_router.py](/home/hep7/project/kairos/zlink/bindings/python/perf/single/perf_dealer_router.py)
  - 코드 확인: [bindings/python/perf/single/perf_spot.py](/home/hep7/project/kairos/zlink/bindings/python/perf/single/perf_spot.py)
  - grep 확인: `rg -n "Poller|safe_poll|wait_socket_event|wait_pubsub_ready|wait_connected_pair|wait_send_ready" /home/hep7/project/kairos/zlink/bindings/python/perf/single -g '*.py'`
  - smoke 결과: [perf_linux_callback_20260406_171256_blockerfix_single.txt](/home/hep7/project/kairos/zlink/bindings/python/perf/results/single/report/perf_linux_callback_20260406_171256_blockerfix_single.txt)

### 8. bindings/node/perf: 전체 패턴/전체 사이즈 정상 동작 완료

- 현재 상태: 완료
- 확인 내용:
  - 문제로 지적되던 대형 메시지 결과 0.00 패턴이 해소되었다.
  - 관련 callback burst/drain 수정 후 재검증이 통과했다.
- 검증 근거:
  - 코드 확인: [bindings/node/perf/common/perf_metrics.ts](/home/hep7/project/kairos/zlink/bindings/node/perf/common/perf_metrics.ts)
  - 코드 확인: [bindings/node/perf/single/perf_pubsub.ts](/home/hep7/project/kairos/zlink/bindings/node/perf/single/perf_pubsub.ts)
  - 코드 확인: [bindings/node/perf/single/perf_router_router.ts](/home/hep7/project/kairos/zlink/bindings/node/perf/single/perf_router_router.ts)
  - 코드 확인: [bindings/node/perf/single/perf_spot.ts](/home/hep7/project/kairos/zlink/bindings/node/perf/single/perf_spot.ts)
  - smoke 결과: [perf_linux_callback_20260406_170059.txt](/home/hep7/project/kairos/zlink/bindings/node/perf/results/single/report/perf_linux_callback_20260406_170059.txt)

### 9. bindings/rust/perf: Rust 컴파일 경고 0건 목표 달성

- 현재 상태: 완료
- 확인 내용:
  - `cargo check` 결과에서 경고가 남지 않도록 정리되었다.
  - single/multi 모두 현재 상태에서 경고 없이 통과한다.
- 검증 근거:
  - 코드 확인: [bindings/rust/perf/single/src/common.rs](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/common.rs)
  - 코드 확인: [bindings/rust/perf/multi/src/common.rs](/home/hep7/project/kairos/zlink/bindings/rust/perf/multi/src/common.rs)
  - 직접 실행: `cargo check --manifest-path /home/hep7/project/kairos/zlink/bindings/rust/perf/single/Cargo.toml`
  - 직접 실행: `cargo check --manifest-path /home/hep7/project/kairos/zlink/bindings/rust/perf/multi/Cargo.toml`

## 문서 불일치 메모

- 초기 감사 시점에는 `PROGRESS.md`와 코드 상태가 어긋나 있었다.
- 현재는 각 항목을 코드와 결과 파일 기준으로 다시 검증했으며, 초기 미완료 9건은 모두 완료 판정으로 정리되었다.

## 미완료 항목

현재 기준 미완료 없음.

