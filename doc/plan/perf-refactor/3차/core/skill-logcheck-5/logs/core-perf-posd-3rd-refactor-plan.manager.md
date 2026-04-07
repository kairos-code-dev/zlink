# 관리자 가이드 - `core/perf` POSD 3차 리팩토링 계획

- source_spec_path: /home/hep7/project/kairos/zlink/doc/plan/perf-refactor/3차/core/skill-logcheck-5/core-perf-posd-3rd-refactor-plan.ko.md
- guide_status: approved
- guide_review_status: spec 대조 완료
- review_pass_count: 1
- remaining_guide_corrections: 0
- unresolved_structural_issues: 8
- status: in_progress
- remaining_tasks: 8
- completion_verified: false
- blocked_reason:
- last_review_summary: 단계 순서, full gate 시점, 완료 정의, 비범위를 source spec 기준으로 가이드에 반영함

## 1. 운영 기준

- source_spec_ref:
  - `## 5. 단계별 실행 계획`
  - `## 6. 검증 방법`
  - `### 6.1 Full Test Gate 정의`
  - `## 7. 완료 정의`
  - `## 8. 비범위`
- 고정 순서:
  1. 단계 1. `core/perf` - C1 문서/테스트 수렴
  2. 단계 2. `core/perf` - C2 queue/probe 계층 감사 후 축소
  3. 단계 3. `core/perf` - C3 stream common client 정리
  4. 단계 4. `core/perf` - C4 runner entrypoint 단순화
  5. 단계 5. `bindings/<lang>/perf` - 언어별 순차 정렬
  6. 단계 6. `core/bench/with_zmq` 정리
  7. 단계 7. `core/bench/with_stream` 정리
  8. 단계 8. 공통 tracked artifact 정책 정리
- 범위:
  - 정책 정합성 확인 후 POSD 기준으로 dead code, dead branch, dead option, dead file, 얕은 wrapper, 과대 공통 계층을 제거한다.
  - 각 단계는 구조 정리와 cheap/local 검증이 끝난 뒤에만 해당 단계 full gate를 수행한다.
  - bindings 단계는 `cpp -> dotnet -> java -> rust -> go -> node -> python` 순서를 고정한다.
- 비범위:
  - throughput/bandwidth/latency 계산식 변경
  - one-way vs echo 의미 변경
  - `RESULT` 포맷 확장
  - backpressure 검증의 perf 기본 surface 복귀
  - transport별 성능 수치 튜닝

## 2. 비타협 제약

- source_spec_ref:
  - `### 5.0 작업 순서 원칙`
  - `#### 공통 작업 루프`
  - `### 6.1 Full Test Gate 정의`
  - `## 7. 완료 정의`
- 한 번에 한 단계만 활성화한다. 현재 단계가 `complete`가 아니면 이후 단계는 모두 `blocked`다.
- 각 단계는 다음 순서를 고정한다.
  1. 정책 문서와 현재 구현 불일치 확인
  2. POSD 리팩토링과 dead code/dead branch/dead file 정리
  3. 추가 구조 단순화 여지 재검토
  4. 기능 확인과 cheap/local 검증
  5. 단계 구조 완료가 확인된 뒤 full test gate 수행
  6. gate fail 0건일 때만 다음 단계 진행
- `known fail`, `나중에 수정`, `partial success`로 다음 단계 진행을 허용하지 않는다.
- full test gate는 중간 진행 확인용이 아니라, 해당 단계 구조 정리가 끝났다고 판단된 시점의 최종 검증이다.
- POSD 완료 판단은 테스트 pass만으로 충분하지 않다. 구조 이슈 목록이 비고, 설명 가능한 ownership/surface로 단순화되어야 한다.

## 3. 검증 경계

- source_spec_ref:
  - `## 6. 검증 방법`
  - `### 6.1 Full Test Gate 정의`
- cheap/local 검증:
  - `python3 -m py_compile core/perf/run_comparison.py core/perf/single/run_comparison.py`
  - `pytest -q core/perf/single/tests`
  - `bash -n core/perf/run_benchmarks.sh`
  - `bash -n core/perf/run_benchmarks_multi.sh`
  - `cmake --build core/build -j4 --target comp_src_spot_server comp_src_stream_server`
  - 단계별 README/policy text 감사와 `rg` 기반 legacy contract 잔재 점검
- full gate:
  1. `python3 -m py_compile core/perf/run_comparison.py core/perf/single/run_comparison.py`
  2. `pytest -q core/perf/single/tests`
  3. `bash -n core/perf/run_benchmarks.sh`
  4. `bash -n core/perf/run_benchmarks_multi.sh`
  5. `cmake --build core/build -j4 --target comp_src_spot_server comp_src_stream_server`
  6. `./core/perf/run_benchmarks.sh`
  7. `./core/perf/run_benchmarks_multi.sh`
- bindings/bench 단계는 대상 전용 검증을 추가하되 core perf full gate를 생략하지 않는다.
- full gate 진입 전 확인:
  - 현재 단계 구조 이슈가 0건인가
  - 측정 의미를 해치지 않았는가
  - `doc/perf` 정책 위반이 없는가
  - cheap/local 검증이 먼저 통과했는가

## 4. 단계별 운영 메모

## 단계 1. `core/perf` - C1 문서/테스트 수렴

- source_spec_ref:
  - `#### C1. 정책 surface와 충돌하는 문서/테스트 정리`
  - `### 단계 1. core/perf - C1 문서/테스트 수렴`
- stage_scope:
  - `core/perf/README.md`
  - `core/perf/README_KO.md`
  - `core/perf/single/tests/test_multi_run_comparison_policy.py`
  - `core/perf/single/tests/test_run_comparison_policy.py`
- stage_structural_goal:
  - README를 policy index 수준으로 축약하고 제거된 metric/phase 설명을 삭제한다.
  - policy fixture를 필수 5개 metric 기준으로 재작성하고 old metric 부재를 직접 검증하는 이름으로 정리한다.
  - 문서/테스트 주변의 얕은 fixture 및 wrapper를 되살리지 않도록 정리한다.
- stage_completion_definition:
  - README가 현재 policy와 모순되지 않는다.
  - 테스트 fixture가 제거된 metric/phase를 다시 사용하지 않는다.
  - `rg`로 warmup/cpu-mem/legacy recv 문구가 README/tests에 남지 않는다.
- stage_gate_prerequisites:
  - 문서/테스트 정리가 구조적으로 완료되었고 추가 dead text나 얕은 fixture 잔재가 없다.
  - cheap/local 검증과 `rg` 감사가 끝난 뒤에만 단계 1 full gate를 수행한다.
- stage_unresolved_issues:
  - README가 아직 정책 요약 index 수준으로 축약됐는지 확인 필요
  - policy test fixture가 5개 metric 기준으로 정렬됐는지 확인 필요
  - old metric/warmup/legacy recv 문구 잔재 확인 필요

## 단계 2. `core/perf` - C2 queue/probe 계층 감사 후 축소

- source_spec_ref:
  - `#### C2. queue/probe 공통 surface 축소`
  - `### 단계 2. core/perf - C2 queue/probe 계층 감사 후 축소`
- stage_scope:
  - `core/perf/multi/common/perf_multi_metrics.hpp`
  - `core/perf/single/common/perf_single_queue_probe.hpp`
  - `core/perf/single/common/bench_common.hpp`
  - `core/perf/multi/src/perf_multi_spot_client.cpp`
  - `core/perf/multi/common/perf_multi_spot_control.hpp`
- stage_structural_goal:
  - queue probe, queue stats, queue/debug helper가 기본 perf 계약에 직접 기여하지 않으면 공통 helper에서 제거한다.
  - 꼭 필요한 진단 코드는 SPOT/local helper 범위로 축소해 ownership을 명확히 한다.
  - 패턴 파일이 필요 이상 내부를 보지 않도록 include surface를 줄인다.
- stage_completion_definition:
  - 기본 perf 실행에 queue probe 공용 계층이 필수가 아니다.
  - queue/backpressure helper가 공통 hot path 설명 복잡도를 올리지 않는다.
- stage_gate_prerequisites:
  - call graph와 ownership이 정리되어 남은 공통 queue/probe 잔재가 없다고 확인된 뒤 cheap/local 검증 수행
  - cheap/local 검증 완료 후 단계 2 full gate 수행
- stage_unresolved_issues:
  - queue/probe 공용 계층의 실제 필수성 감사 미완료
  - debug/queue helper의 소유권 국소화 필요 여부 미확정
  - `bench_common.hpp` include 축소 여부 미확정

## 단계 3. `core/perf` - C3 stream common client 정리

- source_spec_ref:
  - `#### C3. stream common client 정렬`
  - `### 단계 3. core/perf - C3 stream common client 정리`
- stage_scope:
  - `core/perf/common/streamclient/README.md`
  - `core/perf/common/streamclient/README_KO.md`
  - `core/perf/common/streamclient/perf_stream_bench_client.hpp`
  - `core/perf/common/streamclient/perf_stream_client_options.hpp`
  - `core/perf/common/streamclient/perf_stream_client_session.hpp`
- stage_structural_goal:
  - warmup phase와 `--warmup` 옵션 잔재를 제거한다.
  - lifecycle을 `ready -> active` 기준으로 재정렬한다.
  - downstream perf/bench/bindings에 old stream contract를 다시 주입하지 않게 한다.
- stage_completion_definition:
  - stream common client 문서와 코드에 warmup contract가 남지 않는다.
  - shared STREAM client가 core perf policy와 같은 phase/metric surface를 따른다.
- stage_gate_prerequisites:
  - 공용 stream client 기준이 먼저 고정되고 downstream으로 old contract가 재주입되지 않는다는 점검 완료
  - cheap/local 검증 완료 후 단계 3 full gate 수행
- stage_unresolved_issues:
  - warmup/old lifecycle contract 잔재 확인 필요
  - shared client option surface와 문서 정렬 여부 확인 필요
  - downstream 재주입 경로 감사 필요

## 단계 4. `core/perf` - C4 runner entrypoint 단순화

- source_spec_ref:
  - `#### C4. runner entrypoint 책임 재정렬`
  - `### 단계 4. core/perf - C4 runner entrypoint 단순화`
- stage_scope:
  - `core/perf/run_benchmarks.sh`
  - `core/perf/run_benchmarks_multi.sh`
  - `core/perf/run_benchmarks.ps1`
  - `core/perf/run_benchmarks_multi.ps1`
  - `core/perf/run_comparison.py`
  - `core/perf/single/run_comparison.py`
- stage_structural_goal:
  - shell은 option normalization과 build gate만 맡기고 orchestration은 Python engine으로 직접 연결한다.
  - multi shell이 single shell을 재호출하는 얕은 위임 구조를 줄이거나 제거한다.
  - 공식 entrypoint 정의를 문서와 코드에서 동일하게 맞춘다.
- stage_completion_definition:
  - entrypoint 책임이 shell 2단 위임으로 퍼져 있지 않다.
  - single/multi 실행 흐름이 문서 한 문단으로 설명 가능하다.
- stage_gate_prerequisites:
  - shell/python ownership 표와 실제 호출 경로 단순화가 확인되고 dead wrapper가 남지 않는다.
  - cheap/local 검증 완료 후 단계 4 full gate 수행
- stage_unresolved_issues:
  - shell 재호출 경로 실태 확인 필요
  - option ownership 정리 필요
  - 문서/코드 공식 entrypoint 불일치 확인 필요

## 단계 5. `bindings/<lang>/perf` - 언어별 순차 정렬

- source_spec_ref:
  - `### 4.2 bindings/<lang>/perf 작업`
  - `#### B1. bindings perf 정책 정렬`
  - `### 단계 5. bindings/<lang>/perf - 언어별 순차 정렬`
- stage_scope:
  - `bindings/cpp/perf`
  - `bindings/dotnet/perf`
  - `bindings/java/perf`
  - `bindings/rust/perf`
  - `bindings/go/perf`
  - `bindings/node/perf`
  - `bindings/python/perf`
- stage_structural_goal:
  - 각 언어를 `cpp -> dotnet -> java -> rust -> go -> node -> python` 순으로 하나씩 정렬한다.
  - old warmup/cpu-mem/queue/debug contract를 제거하고 runner/result/report 구조를 새 정책에 맞춘다.
  - 얕은 wrapper, dead code, 과대 공통 계층을 언어별로 줄인다.
- stage_completion_definition:
  - 각 언어가 policy surface와 일치하고 해당 language 검증과 core perf full gate를 함께 통과한다.
- stage_gate_prerequisites:
  - 현재 언어의 구조 이슈가 비고 해당 언어 검증이 끝난 뒤 core perf full gate 수행
  - 한 언어가 complete 되기 전 다음 언어는 blocked 유지
- stage_unresolved_issues:
  - 언어별 drift 현황 미감사
  - 고정 순서 준수 상태 미확인
  - language-specific wrapper/dead code 정리 범위 미확정

## 단계 6. `core/bench/with_zmq` 정리

- source_spec_ref:
  - `### 4.3 core/bench/with_zmq 작업`
  - `### 단계 6. core/bench/with_zmq 정리`
- stage_scope:
  - `core/bench/with_zmq`
  - `core/bench/with_zmq/README.md`
  - `core/bench/with_zmq/run_benchmarks.sh`
  - `core/bench/with_zmq/run_benchmarks_multi.sh`
  - `core/bench/with_zmq/single/tests/test_run_comparison_policy.py`
- stage_structural_goal:
  - `with_zmq` 로컬 bench 계약과 `doc/perf` 공통 원칙 경계를 확정한다.
  - core perf와 충돌하는 warmup/metric/result contract를 분리하거나 제거한다.
  - bench 전용 개념이 perf 본계약으로 역류하지 않게 경계를 명확히 한다.
- stage_completion_definition:
  - 비교 bench 기능을 유지하면서 perf 본계약과 충돌하는 계약이 정리된다.
- stage_gate_prerequisites:
  - `with_zmq` 로컬 계약 유지 여부를 cheap/local 검증으로 먼저 확인
  - 대상 전용 검증 후 core perf full gate 수행
- stage_unresolved_issues:
  - with_zmq와 core perf 경계 미확정
  - warmup/metric/result contract 충돌 경로 미감사
  - bench 전용 개념 역류 차단 상태 미확인

## 단계 7. `core/bench/with_stream` 정리

- source_spec_ref:
  - `### 4.4 core/bench/with_stream 작업`
  - `### 단계 7. core/bench/with_stream 정리`
- stage_scope:
  - `core/bench/with_stream`
  - `core/bench/with_stream/README.md`
  - `core/bench/with_stream/run_comparison.py`
  - `core/bench/with_stream/run_benchmarks.sh`
- stage_structural_goal:
  - `with_stream` 로컬 bench 계약과 `doc/perf` 공통 원칙 경계를 확정한다.
  - shared STREAM client 정렬 이후 남은 old stream contract를 제거한다.
  - 비교 stack surface는 유지하되 perf 본계약과 충돌하지 않게 한다.
- stage_completion_definition:
  - 비교 bench 기능이 유지되고 old stream contract 잔재가 제거된다.
- stage_gate_prerequisites:
  - old contract 잔재 제거와 로컬 계약 유지 확인 후 대상 전용 검증 수행
  - 대상 전용 검증 후 core perf full gate 수행
- stage_unresolved_issues:
  - with_stream old contract 잔재 미감사
  - shared client 정렬 영향 범위 미확정
  - 비교 surface와 perf 본계약 경계 미확정

## 단계 8. 공통 tracked artifact 정책 정리

- source_spec_ref:
  - `### 4.5 공통 tracked artifact 정리 기준 수립`
  - `### 단계 8. 공통 tracked artifact 정책 정리`
- stage_scope:
  - tracked baseline/results 후보
  - 관련 문서와 `.gitignore`
  - 공식 runtime output root 규칙
- stage_structural_goal:
  - tracked artifact와 공식 runtime output root의 경계를 명확히 한다.
  - 문서 증거로 남길 파일과 실행 산출물을 분리한다.
  - 필요한 `.gitignore` 및 저장 위치 규칙을 반영한다.
- stage_completion_definition:
  - tracked perf artifact 정리 기준이 문서화되고 코드 트리에 반영된다.
  - 결과 저장/보존 동작이 정책과 모순되지 않는다.
- stage_gate_prerequisites:
  - artifact 분류 기준과 저장 위치 규칙이 구조적으로 정리된 뒤 cheap/local 확인
  - 최종 full test gate는 이 단계에서만 허용
- stage_unresolved_issues:
  - tracked artifact 분류 기준 미정
  - runtime output root 경계 미정
  - `.gitignore` 및 저장 위치 규칙 반영 상태 미확인

## 5. 종료 기준

- source_spec_ref:
  - `## 7. 완료 정의`
  - `## 9. 메모`
- 아래가 모두 만족될 때만 `status: complete`로 바꾼다.
  - `core/perf` 문서, runner, 테스트가 현재 policy surface와 일치한다.
  - 제거된 metric/phase를 README나 테스트 fixture가 다시 들고 오지 않는다.
  - queue/debug/probe 계층이 공통 surface에 불필요하게 남지 않는다.
  - `core/perf/common/streamclient`가 old warmup contract를 유지하지 않는다.
  - runner entrypoint 구조가 더 짧고 설명 가능해진다.
  - bindings, `with_stream`, `with_zmq`가 core perf와 충돌하는 phase/metric surface를 되살리지 않는다.
  - tracked artifact 정책이 문서와 코드 트리에 반영된다.
  - 추가 POSD 정리 여지가 남지 않았거나, 남겨도 복잡도 감소 이득이 작다는 근거를 설명할 수 있다.
