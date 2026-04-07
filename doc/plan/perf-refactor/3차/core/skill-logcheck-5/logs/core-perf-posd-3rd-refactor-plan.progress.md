# 진행표 - `core/perf` POSD 3차 리팩토링 계획

- source_spec_path: /home/hep7/project/kairos/zlink/doc/plan/perf-refactor/3차/core/skill-logcheck-5/core-perf-posd-3rd-refactor-plan.ko.md
- manager_guide_path: /home/hep7/project/kairos/zlink/doc/plan/perf-refactor/3차/core/skill-logcheck-5/logs/core-perf-posd-3rd-refactor-plan.manager.md
- guide_status: approved
- status: in_progress
- remaining_tasks: 8
- completion_verified: false
- 현재 활성 단계: 단계 1. `core/perf` - C1 문서/테스트 수렴
- 다음 허용 작업: 단계 1 대상 파일과 현재 변경 상태를 대조해 열린 구조 이슈를 확정
- blocked_reason:
- latest_verified_evidence: manager guide 1차 승인 완료, 단계 순서와 full gate 경계 확인

| 단계 | 현재 상태 | 핵심 확인 항목 | 남은 구조 이슈 수 | 구조 완료 확인 | cheap/local 검증 | 단계 커밋 준비 | full gate 허용 | 다음 단계 진행 가능 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 단계 1. `core/perf` - C1 문서/테스트 수렴 | in_review | README/정책 테스트가 5개 metric 기준과 일치하는지 확인 | 3 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 2. `core/perf` - C2 queue/probe 계층 감사 후 축소 | blocked | queue/probe 공용 계층의 실제 필수성 감사 | 3 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 3. `core/perf` - C3 stream common client 정리 | blocked | warmup/old lifecycle contract 잔재 제거 | 3 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 4. `core/perf` - C4 runner entrypoint 단순화 | blocked | shell/python ownership 재정렬 | 3 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 5. `bindings/<lang>/perf` - 언어별 순차 정렬 | blocked | 언어별 drift 감사와 고정 순서 유지 | 3 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 6. `core/bench/with_zmq` 정리 | blocked | with_zmq 로컬 계약과 core perf 경계 확정 | 3 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 7. `core/bench/with_stream` 정리 | blocked | old stream contract 잔재와 경계 확정 | 3 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
| 단계 8. 공통 tracked artifact 정책 정리 | blocked | tracked artifact 분류와 저장 위치 규칙 확정 | 3 | 아니오 | 아니오 | 아니오 | 아니오 | 아니오 |
