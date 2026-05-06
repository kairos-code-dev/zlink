# POSD Refactor Log

## 2026-05-06 pre-release POSD loop

- 날짜: 2026-05-06
- 대상: POSD 기반 전체 리팩토링 루프
- 수행한 명령:
  - `sed -n '1,240p' doc/principal/software-design-principles.md`
  - `rg -n "#define zlink_actor|#define zlink_spot_node_actor_new|#define zlink_spot_node_destroy_remote_actor|zlink_actor_destroy\\(|zlink_actor_get_ref\\(|zlink_actor_join_spot\\(|zlink_actor_leave_spot\\(|zlink_actor_recv_part\\(|zlink_spot_node_destroy_remote_actor\\(" core/tests/integration/test_spot_actor_dispatch.cpp`
  - `rg -n "TODO|FIXME|HACK|temporary|compat|adapter|for_testing|pending|global|static std::map|static std::set" core/src/api/service_spot_actor_api.cpp core/tests/integration/test_spot_actor_dispatch.cpp core/src/services/spot -g "*.cpp" -g "*.hpp"`
  - `rg -n "joined_spot\\b" core/src/api/service_spot_actor_api.cpp`
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build -R '^test_spot_actor_dispatch$' --output-on-failure`
  - `cmake --build core/build && ctest --test-dir core/build --output-on-failure`
- 확인한 draft spec 절: Actor는 항상 정확히 하나의 Spot에 속한다, Entry Spot,
  Actor readable dispatch subject, Actor join/leave, STREAM session과 Actor 연결
- 발견한 문제:
  - 위험 신호 1: `actor_handle_t`가 current Spot을 `joined_spot_state`와
    `joined_spot` 두 표현으로 동시에 보관했다. 이는 정보 은닉 위반이며 facade 교체 때
    보정 코드가 필요해 change amplification을 만든다.
  - 대안 A: facade 포인터를 계속 유지하고 destroy/lookup 경로에서 동기화한다.
  - 대안 B: logical Spot state를 단일 기준으로 삼고 dispatch 때 필요한 facade를 찾는다.
  - 선택: 대안 B. Actor의 소속은 logical state 하나가 소유하고, facade는 callback
    전달에 필요한 순간에만 찾는 쪽이 더 깊은 모듈이다.
  - 위험 신호 2: `service_spot_actor_api.cpp`가 큰 translation unit이고 actor runtime
    table, join operation, session binding을 함께 다룬다.
  - 대안 A: 지금 actor runtime을 별도 module로 분리한다.
  - 대안 B: public ABI와 release 직전 안전성을 우선해 이번 루프에서는 새 public
    복잡성을 추가하지 않고, 다음 구조 작업 후보로 기록한다.
  - 선택: 대안 B. 현재 변경은 public 계약과 release 안전성 영향이 크므로, 이번 루프는
    새 interface 없이 중복 상태 제거에 한정한다.
  - 위험 신호 3: `test_spot_actor_dispatch.cpp`에 old Actor handle API 이름을 흉내 낸
    test-local adapter가 남아 있다.
  - 대안 A: 테스트 전체를 ref-native helper로 즉시 재작성한다.
  - 대안 B: public surface에는 노출하지 않고, binding 순차 적용과 test helper 정리 때
    제거한다.
  - 선택: 대안 B. 테스트 범위가 크고 현재 public header/core/docs에는 제거 API가 없으므로
    안전성 기준으로 유지 사유를 남긴다.
- 수정한 파일:
  - `core/src/api/service_spot_actor_api.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
  - `doc/plan/spot-entry-transport-queues/logs/posd-refactor-log.ko.md`
- 검증 결과:
  - `joined_spot` 중복 상태 제거 뒤 `test_spot_actor_dispatch` 통과.
  - 전체 core build 성공.
  - 전체 CTest 102/102 통과.
  - 두 번째 스캔에서 새 pre-release POSD 수정 후보는 발견하지 못했다.
- 남은 위험:
  - binding source의 언어별 POSD는 core release와 native library 갱신 뒤 bindings gate에서
    언어 순서대로 수행한다.
  - actor runtime module 분리는 public 계약 안정화 뒤 별도 구조 작업 후보로 둔다.
- 다음 확인: 정식 문서 반영과 3회 리뷰
