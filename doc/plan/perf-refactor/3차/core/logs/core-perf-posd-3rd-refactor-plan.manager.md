# 관리자 가이드 - `core/perf` POSD 3차 리팩토링 계획

- source_spec_path: /home/hep7/project/kairos/zlink/doc/plan/perf-refactor/3차/core/core-perf-posd-3rd-refactor-plan.ko.md
- source_spec_refs:
  - `5.0 작업 순서 원칙`
  - `4.1 core/perf 작업`
  - `4.2 bindings/<lang>/perf 작업`
  - `4.3 core/bench/with_zmq 작업`
  - `4.4 core/bench/with_stream 작업`
  - `4.5 공통 tracked artifact 정리 기준 수립`
  - `6. 검증 방법`
  - `6.1 Full Test Gate 정의`
  - `7. 완료 정의`
  - `8. 비범위`
- guide_status: approved
- guide_review_status: 승인 완료
- review_pass_count: 2
- remaining_guide_corrections: 0
- unresolved_structural_issues: 7
- status: in_progress
- remaining_tasks: 8
- completion_verified: false
- blocked_reason:
- last_review_summary: 단계 1 README/tests 정리와 cheap/local 검증을 직접 확인했다. 단계 1은 구조적으로 닫혔고 full gate 대기 상태다.

## 1. 범위와 비범위

- 범위:
  - `core/perf` 문서, runner, 테스트, shared stream client, queue/probe/common surface 정리
  - `bindings/<lang>/perf`의 정책/phase/metric surface 정렬
  - `core/bench/with_zmq`, `core/bench/with_stream`의 로컬 bench 계약 정렬
  - tracked perf artifact 정리 기준 수립
- 비범위: `8. 비범위`
  - throughput/bandwidth/latency 계산식 변경 금지
  - one-way vs echo 의미 변경 금지
  - `RESULT` 포맷 확장 금지
  - backpressure 검증을 perf 기본 surface로 복귀시키는 작업 금지
  - transport별 성능 수치 튜닝 금지

## 2. 고정 순서와 공통 규칙

- source_spec_ref: `5.0 작업 순서 원칙`, `공통 작업 루프`, `6.1 Full Test Gate 정의`
- 고정 순서:
  1. 단계 1. `core/perf` - C1 문서/테스트 수렴
  2. 단계 2. `core/perf` - C2 queue/probe 계층 감사 후 축소
  3. 단계 3. `core/perf` - C3 stream common client 정리
  4. 단계 4. `core/perf` - C4 runner entrypoint 단순화
  5. 단계 5. `bindings/<lang>/perf` - 언어별 순차 정렬
  6. 단계 6. `core/bench/with_zmq` 정리
  7. 단계 7. `core/bench/with_stream` 정리
  8. 단계 8. 공통 tracked artifact 정책 정리
- 공통 작업 루프:
  1. 정책 문서 기준 불일치 확인
  2. POSD 리팩토링 수행
  3. dead code/dead branch/dead file까지 포함해 추가 구조 단순화 여지 재확인
  4. 기능 확인과 cheap/local 검증 수행
  5. 위 1~4가 정리된 뒤에만 stage별 full test gate 수행
  6. full test gate 무실패일 때만 다음 단계 진행
- 금지 규칙:
  - full gate를 중간 상태 확인 용도로 사용하지 않음
  - `known fail`, `나중에 수정`, `partial success`를 이유로 다음 단계 진행 금지
  - 한 단계라도 fail 항목이 남으면 다음 단계 진행 금지
  - 단계가 `complete` 되기 전에는 후속 단계를 `blocked`로 유지

## 3. 검증 정책

- source_spec_ref: `6. 검증 방법`, `6.1 Full Test Gate 정의`
- cheap/local 검증:
  - `python3 -m py_compile core/perf/run_comparison.py core/perf/single/run_comparison.py`
  - `pytest -q core/perf/single/tests`
  - `bash -n core/perf/run_benchmarks.sh`
  - `bash -n core/perf/run_benchmarks_multi.sh`
  - `cmake --build core/build -j4 --target comp_src_spot_server comp_src_stream_server`
  - `rg -n "cpu_pct|mem_mb|server_cpu_pct|server_mem_mb|warmup|Q\\.Snd|Q\\.Rcv" core/perf core/bench/with_stream core/bench/with_zmq bindings doc/perf`
- full test gate:
  1. `python3 -m py_compile core/perf/run_comparison.py core/perf/single/run_comparison.py`
  2. `pytest -q core/perf/single/tests`
  3. `bash -n core/perf/run_benchmarks.sh`
  4. `bash -n core/perf/run_benchmarks_multi.sh`
  5. `cmake --build core/build -j4 --target comp_src_spot_server comp_src_stream_server`
  6. `./core/perf/run_benchmarks.sh`
  7. `./core/perf/run_benchmarks_multi.sh`
- gate 전제:
  - 해당 단계의 POSD 리팩토링과 dead code 정리가 더 이상 남지 않았다고 직접 확인되어야 함
  - `doc/perf` 정책 위반 상태가 아니어야 함
  - 결과의 fail 항목 수가 0이어야 함
  - bindings/bench 단계도 대상 전용 검증을 추가하되 core perf full gate를 생략하지 않음

## 4. 단계별 운영 정의

## 단계 1. `core/perf` - C1 문서/테스트 수렴

- source_spec_ref: `4.1 C1`, `5 단계 1`
- stage_scope:
  - `core/perf/README.md`
  - `core/perf/README_KO.md`
  - `core/perf/single/tests/test_multi_run_comparison_policy.py`
  - `core/perf/single/tests/test_run_comparison_policy.py`
- stage_structural_goal:
  - README를 정책 요약 index 수준으로 축소
  - warmup, single `recv`, `cpu/mem` 등 제거된 계약을 문서/fixture에서 제거
  - policy fixture를 필수 5개 metric 기준으로 단순화
- stage_completion_definition:
  - README가 policy와 모순되지 않음
  - 테스트 fixture가 제거된 metric을 더 이상 사용하지 않음
  - 제거된 계약을 되살리는 얕은 fixture/wrapper가 남지 않음
- stage_gate_prerequisites:
  - README/tests에 legacy 용어가 남지 않는지 `rg` 확인
  - cheap/local 검증 완료
  - 구조 이슈가 0개로 줄었는지 직접 검토
- stage_unresolved_issues:
  - 없음. README 2종에서 legacy recv/warmup/cpu-mem 설명 제거 확인.
  - 없음. policy test 2종이 Tier 1 5개 metric 및 callback 중심 현재 계약으로 수렴했고 `pytest -q core/perf/single/tests` 통과 확인.

## 단계 2. `core/perf` - C2 queue/probe 계층 감사 후 축소

- source_spec_ref: `4.1 C2`, `5 단계 2`
- stage_scope:
  - `core/perf/multi/common/perf_multi_metrics.hpp`
  - `core/perf/single/common/perf_single_queue_probe.hpp`
  - `core/perf/single/common/bench_common.hpp`
  - `core/perf/multi/src/perf_multi_spot_client.cpp`
  - `core/perf/multi/common/perf_multi_spot_control.hpp`
- stage_structural_goal:
  - queue probe, queue stats, debug helper가 기본 perf 계약에 직접 기여하지 않으면 공통 surface에서 제거
  - 꼭 필요하면 SPOT/local diagnostic 범위로 국소화
  - 패턴 파일이 불필요한 내부를 보지 않게 include 경계 축소
- stage_completion_definition:
  - 기본 perf 실행에 queue probe 공용 계층이 필수가 아님
  - queue/backpressure helper가 공통 hot path 설명 복잡도를 올리지 않음
- stage_gate_prerequisites:
  - call graph와 공통/public 노출 경계 검토 완료
  - cheap/local 검증 완료
  - 제거 또는 국소화 후 dead include/dead helper 없음
- stage_unresolved_issues:
  - queue/debug helper가 공통 hot path에서 완전히 분리되었는지 미확인

## 단계 3. `core/perf` - C3 stream common client 정리

- source_spec_ref: `4.1 C3`, `5 단계 3`
- stage_scope:
  - `core/perf/common/streamclient/README.md`
  - `core/perf/common/streamclient/README_KO.md`
  - `core/perf/common/streamclient/perf_stream_bench_client.hpp`
  - `core/perf/common/streamclient/perf_stream_client_options.hpp`
  - `core/perf/common/streamclient/perf_stream_client_session.hpp`
- stage_structural_goal:
  - shared stream client에서 warmup phase와 `--warmup` 옵션 잔재 제거
  - lifecycle을 `ready -> active` 기준으로 정렬
  - downstream perf/bench/bindings에 old contract 재주입 차단
- stage_completion_definition:
  - 문서와 코드에 warmup contract가 남지 않음
  - shared client가 core perf policy와 같은 phase/metric surface를 따름
- stage_gate_prerequisites:
  - public option/README/세션 lifecycle 표면 일치
  - cheap/local 검증 완료
  - shared client를 통하는 하위 경로의 old contract 잔재 없음
- stage_unresolved_issues:
  - stream shared client의 old warmup/measure/drain contract 잔재 여부 미확인

## 단계 4. `core/perf` - C4 runner entrypoint 단순화

- source_spec_ref: `4.1 C4`, `5 단계 4`
- stage_scope:
  - `core/perf/run_benchmarks.sh`
  - `core/perf/run_benchmarks_multi.sh`
  - `core/perf/run_benchmarks.ps1`
  - `core/perf/run_benchmarks_multi.ps1`
  - `core/perf/run_comparison.py`
  - `core/perf/single/run_comparison.py`
- stage_structural_goal:
  - shell은 option normalization과 build gate까지만 담당
  - orchestration 책임은 Python engine으로 더 직접 연결
  - multi shell이 single shell을 다시 호출하는 구조 축소 또는 제거
- stage_completion_definition:
  - entrypoint 책임이 shell 2단 위임으로 퍼져 있지 않음
  - single/multi 실행 흐름이 문서 한 문단으로 설명 가능함
- stage_gate_prerequisites:
  - 공식 entrypoint 정의가 문서와 코드에서 일치
  - cheap/local 검증 완료
  - shell 재호출 경로와 얕은 wrapper 잔재 없음
- stage_unresolved_issues:
  - shell/python ownership 경계와 재호출 경로 단순화 여부 미확인

## 단계 5. `bindings/<lang>/perf` - 언어별 순차 정렬

- source_spec_ref: `4.2`, `5 단계 5`
- stage_scope:
  - `bindings/cpp/perf`
  - `bindings/dotnet/perf`
  - `bindings/java/perf`
  - `bindings/rust/perf`
  - `bindings/go/perf`
  - `bindings/node/perf`
  - `bindings/python/perf`
- stage_structural_goal:
  - `cpp -> dotnet -> java -> rust -> go -> node -> python` 순으로 하나씩 정렬
  - old warmup/cpu-mem/queue/debug contract 제거
  - phase/metric/result semantics를 core와 정렬하면서 shallow wrapper와 dead code 축소
- stage_completion_definition:
  - bindings perf와 core perf가 phase/metric/RESULT 의미에서 모순되지 않음
  - 각 bindings가 변경된 `doc/perf` 정책을 실제 코드와 실행 surface에서 따름
- stage_gate_prerequisites:
  - 현재 언어 대상의 정책 감사 완료
  - 해당 언어 전용 검증과 core perf full gate 통과
  - 다음 언어 시작 전 현재 언어 구조 이슈 0건
- stage_unresolved_issues:
  - 언어별 순차 재개 지점과 남은 old contract 잔재 미확인

## 단계 6. `core/bench/with_zmq` 정리

- source_spec_ref: `4.3`, `5 단계 6`
- stage_scope:
  - `core/bench/with_zmq`
- stage_structural_goal:
  - `with_zmq` 로컬 bench 계약과 `doc/perf` 공통 원칙의 경계 확정
  - warmup 의존과 충돌하는 warmup/metric/result contract 분리 또는 제거
  - bench 전용 개념과 perf 본계약 경계 명확화
- stage_completion_definition:
  - 로컬 bench 정책을 유지하면서 core perf 공통 측정 원칙과 모순되지 않음
  - tmp/results/debug 잔재가 비교 surface 설명 복잡도를 과도하게 올리지 않음
- stage_gate_prerequisites:
  - `bash -n core/bench/with_zmq/run_benchmarks.sh`
  - `bash -n core/bench/with_zmq/run_benchmarks_multi.sh`
  - `pytest -q core/bench/with_zmq/single/tests/test_run_comparison_policy.py`
  - `./core/bench/with_zmq/run_benchmarks.sh`
  - `./core/bench/with_zmq/run_benchmarks_multi.sh`
  - core perf full gate 통과
- stage_unresolved_issues:
  - with_zmq 로컬 계약과 core perf 경계, warmup 잔재, tracked noise 미확인

## 단계 7. `core/bench/with_stream` 정리

- source_spec_ref: `4.4`, `5 단계 7`
- stage_scope:
  - `core/bench/with_stream`
- stage_structural_goal:
  - `with_stream` 로컬 bench 정책과 shared stream client/core perf 공통 원칙의 경계 확정
  - old STREAM warmup/measure/drain 잔재 제거
  - stack별 공통/전용 책임 재정렬
- stage_completion_definition:
  - 로컬 bench 정책을 유지하면서 shared client/core perf 공통 원칙을 다시 오염시키지 않음
  - 비교 surface로서 설명 가능한 구조를 유지함
- stage_gate_prerequisites:
  - `python3 -m py_compile core/bench/with_stream/run_comparison.py`
  - `bash -n core/bench/with_stream/run_benchmarks.sh`
  - `./core/bench/with_stream/run_benchmarks.sh`
  - core perf full gate 통과
- stage_unresolved_issues:
  - with_stream의 old contract 잔재와 stack별 책임 경계 미확인

## 단계 8. 공통 tracked artifact 정책 정리

- source_spec_ref: `4.5`, `5 단계 8`
- stage_scope:
  - `core/perf/baseline`
  - tracked sample / analysis artifact
- stage_structural_goal:
  - tracked baseline/result를 남길 기준 명시
  - old baseline을 제거 또는 archive로 이동
  - 공식 runtime output root인 `core/perf/results`는 유지
- stage_completion_definition:
  - 코드/정책/실행 산출물이 같은 레벨에서 섞여 있지 않음
  - tracked perf artifact는 git에 남아야 하는 최소 집합만 유지
  - `core/perf/results`는 tracked cleanup 대상과 혼동되지 않음
- stage_gate_prerequisites:
  - 저장 위치 규칙과 `.gitignore` 반영
  - cheap/local 검증 완료
  - 최종 core perf full gate 통과
- stage_unresolved_issues:
  - tracked baseline/sample/analysis artifact의 분류 기준과 반영 범위 미확인

## 5. 완료 조건

- source_spec_ref: `7. 완료 정의`, `9. 메모`
- 모든 단계가 고정 순서대로 `complete`
- `core/perf` 문서, runner, 테스트가 현재 policy surface와 일치
- 제거된 metric/phase가 README, fixture, shared client, bindings, bench surface에 다시 남지 않음
- queue/debug/probe, shallow wrapper, 과대 공통 계층, dead code, dead option, dead file이 구조 이슈 목록에서 제거됨
- runner entrypoint 구조가 더 짧고 설명 가능해짐
- tracked perf artifact 기준이 문서화되고 코드 트리에 반영됨
- POSD 기준으로 추가 제거 후보가 남지 않거나, 남더라도 제거 이득이 작다는 근거를 설명 가능함
