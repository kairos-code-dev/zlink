# 진행표 - `core/perf` POSD 3차 리팩토링 계획

- source_spec_path: /home/hep7/project/kairos/zlink/doc/plan/perf-refactor/3차/core/core-perf-posd-3rd-refactor-plan.ko.md
- manager_guide_path: /home/hep7/project/kairos/zlink/doc/plan/perf-refactor/3차/core/logs/core-perf-posd-3rd-refactor-plan.manager.md
- guide_status: approved
- status: in_progress
- remaining_tasks: 8
- completion_verified: false
- 현재 활성 단계: 단계 1. `core/perf` - C1 문서/테스트 수렴
- 다음 허용 작업: 단계 1 full test gate 실행 및 모니터링
- blocked_reason:
- latest_verified_evidence: 단계 1 README/tests 정리 확인, `python3 -m py_compile`, `pytest -q core/perf/single/tests`, `bash -n core/perf/run_benchmarks*.sh`, README/tests audit 모두 통과.

| 단계 | 현재 상태 | 핵심 확인 항목 | 남은 구조 이슈 수 | 구조 완료 확인 | cheap/local 검증 | 단계 커밋 준비 | full gate 허용 | 다음 단계 진행 가능 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 단계 1. `core/perf` - C1 문서/테스트 수렴 | gate_pending | README/tests의 old contract 잔재와 5-metric fixture 확인 | 0 | 예 | 예 | 아니오 | 예 | 아니오 |
| 단계 2. `core/perf` - C2 queue/probe 계층 감사 후 축소 | blocked | queue/probe 공통 surface 제거 또는 국소화 확인 | 1 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 3. `core/perf` - C3 stream common client 정리 | blocked | warmup/measure/drain contract 잔재 확인 | 1 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 4. `core/perf` - C4 runner entrypoint 단순화 | blocked | shell/python ownership과 재호출 경로 확인 | 1 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 5. `bindings/<lang>/perf` - 언어별 순차 정렬 | blocked | 언어별 재개 지점과 old contract 잔재 확인 | 1 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 6. `core/bench/with_zmq` 정리 | blocked | 로컬 bench 계약 경계와 warmup 잔재 확인 | 1 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 7. `core/bench/with_stream` 정리 | blocked | old STREAM contract와 책임 경계 확인 | 1 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 8. 공통 tracked artifact 정책 정리 | blocked | tracked artifact 분류 기준과 코드 반영 확인 | 1 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
