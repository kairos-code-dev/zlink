# perf-refactor 완료 여부 감사 보고서

작성일: 2026-04-06

> 이 문서는 2026-04-06 초기 감사 시점의 스냅샷이다.
> 현재 최신 결론은 [PROGRESS.md](/home/hep7/project/kairos/zlink/doc/plan/perf-refactor/PROGRESS.md)와
> [INCOMPLETE_AUDIT_2026-04-06.md](/home/hep7/project/kairos/zlink/doc/plan/perf-refactor/INCOMPLETE_AUDIT_2026-04-06.md)를 따른다.
> 현재 상태: all 8 targets complete, 현재 기준 미완료 없음.

## 감사 범위

- `doc/plan/perf-refactor/*.md`의 완료 정의, 단계별 작업, 완료 상태
- 실제 구현 코드: `core/perf`, `bindings/*/perf`
- 기존 결과 파일 일부
- 추가 실행:
  - `cd bindings/go/perf && go build ./...`
  - `cd bindings/rust/perf/single && cargo check`
  - `cd bindings/rust/perf/multi && cargo check`

## 결론 요약

초기 감사 시점에는 `ALL 8 TARGETS COMPLETE` 및 각 계획 문서의 `완료 상태`와 달리,
아래 항목들은 코드 또는 결과 파일 기준으로 아직 완료로 보기 어렵다고 판정했다.
이후 수정으로 현재는 모두 완료 상태이며, 이 문서는 당시 판정 근거를 보존하는 아카이브다.

## 미완료 항목 목록

### 1. core/perf: `bench_common.hpp` 400줄 이하 목표 미충족

- 문서 근거:
  - `core-perf-posd-refactor-plan.ko.md` 완료 정의는 `bench_common.hpp`가 400줄 이하라고 명시함.
  - 같은 문서 완료 상태는 다시 `bench_common.hpp 562줄로 분리`라고 적고 있어 내부적으로도 충돌함.
- 실제 코드:
  - `core/perf/single/common/bench_common.hpp` 실제 길이는 562줄.
- 판정:
  - 미완료.
- 근거 위치:
  - `doc/plan/perf-refactor/core-perf-posd-refactor-plan.ko.md:564`
  - `doc/plan/perf-refactor/core-perf-posd-refactor-plan.ko.md:578`
  - `core/perf/single/common/bench_common.hpp:1`

### 2. bindings/cpp/perf: `settle` 삭제 단계 미완료

- 문서 근거:
  - 단계 1은 `phase drain/settle 삭제`를 요구하고, 완료 기준도 `settle()` 함수/변수/env var grep 0건`으로 정의함.
  - 완료 상태는 `phase_drain/settle/drain_warmup 삭제`라고 적음.
- 실제 코드:
  - `wait_for_spot_ready_settle()` 함수가 남아 있음.
  - `PERF_MULTI_SPOT_READY_SETTLE_MS` 환경 변수를 계속 읽고 있음.
  - warmup/active 시작 전 이 settle 대기가 실제 호출되고 있음.
- 판정:
  - 미완료.
- 근거 위치:
  - `doc/plan/perf-refactor/bindings-cpp-perf-posd-refactor-plan.ko.md:149`
  - `doc/plan/perf-refactor/bindings-cpp-perf-posd-refactor-plan.ko.md:173`
  - `bindings/cpp/perf/multi/src/perf_spot_client.cpp:98`
  - `bindings/cpp/perf/multi/src/perf_spot_client.cpp:494`
  - `bindings/cpp/perf/multi/src/perf_spot_client.cpp:495`

### 3. bindings/dotnet/perf: `phase drain/settle 삭제` 단계 미완료

- 문서 근거:
  - 단계 1은 `phase drain/settle 삭제`와 관련 함수/필드/env var 0건을 완료 기준으로 둠.
- 실제 코드:
  - `SingleSettleTimeMs` 상수가 남아 있음.
  - `PerfSpot.cs`에서 `settleMs`를 잡고 `Thread.Sleep(settleMs)`를 수행함.
  - 공통 코드에도 `Thread.Sleep`/`Thread.Yield` 기반 대기 헬퍼가 남아 있음.
- 판정:
  - 미완료.
- 근거 위치:
  - `doc/plan/perf-refactor/bindings-dotnet-perf-posd-refactor-plan.ko.md:118`
  - `doc/plan/perf-refactor/bindings-dotnet-perf-posd-refactor-plan.ko.md:135`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/common/PerfCommon.cs:8`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/common/PerfCommon.cs:110`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/common/PerfCommon.cs:113`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfSpot.cs:22`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfSpot.cs:81`

### 4. bindings/python/perf: single callback-only 정렬 목표 미충족

- 문서 근거:
  - 완료 정의는 `single은 callback-only 모델로 정렬됨 (poller 기반 single 경로 제거)`를 요구함.
  - 완료 상태 문구는 오히려 `패턴 파일은 Poller 방식 유지(on_receive segfault 회피)`라고 적어 완료 정의와 직접 충돌함.
- 실제 코드:
  - single 패턴 구현이 여전히 `zlink.Poller()` + `safe_poll()` + `try_recv()` 루프를 사용함.
- 판정:
  - 미완료.
- 근거 위치:
  - `doc/plan/perf-refactor/bindings-python-perf-posd-refactor-plan.ko.md:177`
  - `doc/plan/perf-refactor/bindings-python-perf-posd-refactor-plan.ko.md:192`
  - `bindings/python/perf/single/perf_pair.py:33`
  - `bindings/python/perf/single/perf_pair.py:39`
  - `bindings/python/perf/single/perf_pair.py:51`

### 5. bindings/node/perf: 전체 패턴/전체 사이즈 정상 동작 목표 미충족

- 문서 근거:
  - 완료 정의는 `전체 패턴/전체 사이즈 정상 동작`을 요구함.
- 실제 결과 파일:
  - 최신 single 결과 파일에서 `PUBSUB 262144`, `ROUTER_ROUTER 262144`, `SPOT 65536/131072/262144`가 모두 `throughput/bandwidth/latency = 0.00`.
  - 즉 적어도 저장된 검증 결과 기준으로는 전체 패턴/전체 사이즈 정상 동작이 아님.
- 판정:
  - 미완료.
- 근거 위치:
  - `doc/plan/perf-refactor/bindings-node-perf-posd-refactor-plan.ko.md:173`
  - `bindings/node/perf/results/single/report/perf_linux_callback_20260406_160711.txt:128`
  - `bindings/node/perf/results/single/report/perf_linux_callback_20260406_160711.txt:218`
  - `bindings/node/perf/results/single/report/perf_linux_callback_20260406_160711.txt:238`
  - `bindings/node/perf/results/single/report/perf_linux_callback_20260406_160711.txt:243`
  - `bindings/node/perf/results/single/report/perf_linux_callback_20260406_160711.txt:248`

### 6. bindings/rust/perf: `Rust 컴파일 경고 0건` 목표 미충족

- 문서 근거:
  - 완료 정의는 `Rust 컴파일 경고 0건`을 요구함.
- 실제 검증:
  - `cargo check`를 single/multi 모두 직접 실행했을 때 경고가 다수 발생함.
  - 예: single `common.rs`의 미사용 필드/메서드, multi `common.rs`의 unused imports, dead_code.
- 판정:
  - 미완료.
- 근거 위치:
  - `doc/plan/perf-refactor/bindings-rust-perf-posd-refactor-plan.ko.md:169`
  - `bindings/rust/perf/single/src/common.rs:115`
  - `bindings/rust/perf/single/src/common.rs:266`
  - `bindings/rust/perf/single/src/common.rs:328`
  - `bindings/rust/perf/multi/src/common.rs:5`
  - `bindings/rust/perf/multi/src/common.rs:22`
  - `bindings/rust/perf/multi/src/common.rs:108`
  - `bindings/rust/perf/multi/src/common.rs:193`

## 명확한 미완료 증거를 찾지 못한 대상

아래 대상은 이번 감사에서 문서와 직접 충돌하는 명확한 미완료 증거를 찾지 못했다. 다만 전 패턴/전 사이즈 스모크를 다시 전부 재실행한 것은 아니므로, 여기의 의미는 `완료 확정`이 아니라 `이번 감사 범위에서 블로킹 불일치 미발견`이다.

- `bindings/go/perf`
  - `go build ./...` 통과
- `bindings/java/perf`

## 메모

- `PROGRESS.md`는 현재 완료 상태를 반영한다.
- 이 문서는 초기 감사 당시의 불일치 기록을 보존한다.
- `core`, `cpp`, `python`, `rust`, `node`에 대한 세부 판정은 당시 산출물 기준 메모로 이해하면 된다.
