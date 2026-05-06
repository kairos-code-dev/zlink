# Blockers

- 날짜: 2026-05-06
- 대상: spot-entry-transport-queues
- 수행한 명령: `git status --short`
- 확인한 draft spec 절: 전체 gate 전 준비
- 발견한 문제: baseline CTest에서 `test_xpub_nodrop` 1개가 실패했다
- 수정한 파일: N/A
- 검증 결과: baseline build는 성공했고, baseline test는 102개 중 101개 통과했다
- 남은 위험: `test_xpub_nodrop` 실패가 이후 pub/sub queue 변경 검증에 영향을 줄 수 있다. 현재는 작업 전 baseline 실패로 기록하고 진행한다
- 다음 확인: pub/sub 단계에서 해당 실패가 draft 계약과 관련되는지 재평가한다

## 2026-05-06 Stage 1 Test Notes

- 날짜: 2026-05-06
- 대상: core full CTest
- 수행한 명령:
  - `ctest --test-dir core/build --output-on-failure`
  - `ctest --test-dir core/build -R 'test_helper_more_bad_send|test_reconnect_ivl' --output-on-failure`
- 확인한 draft spec 절: Public C API 변경 요약, Public API 변경
- 발견한 문제: full CTest 1차에서 `test_helper_more_bad_send` timeout, `test_reconnect_ivl` subprocess abort가 발생했다
- 수정한 파일: N/A
- 검증 결과: 두 실패 테스트를 단독 재실행했을 때 모두 통과했다
- 남은 위험: 두 테스트는 단계 1 변경 표면과 직접 관련이 없지만, 이후 전체 테스트 gate에서 반복 재현 여부를 계속 확인해야 한다
- 다음 확인: 단계 2 구현 뒤 full CTest 재실행

## 2026-05-06 Actor Table Ownership Refactor Note

- 날짜: 2026-05-06
- 대상: Stage 3 `Actor table을 SpotNode가 소유`
- 수행한 명령:
  - `cmake --build core/build --target test_spot_actor_dispatch`
- 확인한 draft spec 절: Component diagram, Actor 생성과 Entry Spot dispatch, Remote join process
- 발견한 문제: 현재 Actor runtime implementation은 anonymous namespace 내부 `actor_handle_t`와 global coordination table에 강하게 묶여 있다. 이 타입을 바로 `SpotNode` state header에 노출하면 anonymous namespace 타입과 global forward declaration이 분리되어 C++ 타입이 달라지고 빌드가 깨진다.
- 수정한 파일: N/A
- 검증 결과: 시도한 header-level table 이전은 되돌렸고, `test_spot_actor_dispatch`와 `test_spot_dispatch_event` 단독 실행은 다시 통과했다.
- 남은 위험: Stage 3 ownership 항목은 실제 내부 타입 분리 리팩토링이 필요하므로 아직 닫지 않는다.
- 다음 확인: Actor runtime state를 별도 internal header로 분리한 뒤 `SpotNode` owned table로 옮긴다.
