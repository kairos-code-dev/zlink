# Implementation Review Log

## 2026-05-06 단계 0

- 날짜: 2026-05-06
- 단계: 단계 0. 기준 상태와 matrix 고정
- 확인한 draft spec 절: 목적, Public C API 변경 요약, 구현 순서, 비목표
- 이번 단계에서 구현할 계약: 없음. 구현 전 contract matrix와 baseline 상태를 고정한다
- 이번 단계에서 구현하지 않는 계약: public C surface, Entry Spot, Actor lifecycle, queue/fanout, bindings 반영
- 관련 회귀 테스트 ID: ENTRY-01..16, ENTRY-ACTOR-01..50, QUEUE-ROUTED-01..03, QUEUE-PUB-01..07, QUEUE-SUB-01..07, QUEUE-CHAN-01..02, QUEUE-SOCKET-01
- 남은 위험: matrix 대조와 baseline build/test가 끝나기 전에는 구현을 시작하지 않는다

## Baseline State

- 날짜: 2026-05-06
- branch: `main`
- HEAD: `2a06cb8da06b37454bd87f7dee6ac5f410fb9ac9`
- draft spec: `doc/spec/draft/spot-entry-transport-queues.ko.md`
- draft hash: `143f0e0b927276283a9c3ccedfa6252f66266720`
- 작업트리: 시작 시 clean
- 기존 public header: `core/include/zlink.h`, `core/include/zlink_enum.h`, `core/include/zlink_errno.h`
- 기존 build root: `core/build`
- 기존 build 명령: `cmake --build core/build`
- 기존 test 명령: `ctest --test-dir core/build --output-on-failure`
- baseline build 결과: 성공
- baseline test 결과: 102개 중 101개 통과, 1개 기존 실패
- baseline 실패: `test_xpub_nodrop`
- 실패 상세: `test_pub_blocking_publish_succeeds_while_subscriber_drains_tcp`에서 `blocking publish timeout: sent=3875 recv=3875`
- contract matrix 검증:
  - test ID 대조: 빈 출력
  - `zlink_*` API 대조: 빈 출력
  - `ZLINK_*` 심볼 대조: 빈 출력
- 다음 확인: 단계 1 public C surface 반영

## 2026-05-06 단계 1

- 날짜: 2026-05-06
- 단계: 단계 1. Public C Surface 반영
- 확인한 draft spec 절: Public C API 변경 요약, Public API 변경
- 이번 단계에서 구현할 계약: public constant, result, type, function prototype을 draft와 맞춘다
- 이번 단계에서 구현하지 않는 계약: Entry Spot logical lifecycle, Spot lookup ref-count, Actor Entry membership, remote handoff, queue/fanout
- 관련 회귀 테스트 ID: ENTRY-ACTOR-42, ENTRY-ACTOR-46, ENTRY-01, ENTRY-08
- 수행한 명령:
  - `cmake --build core/build`
  - `ctest --test-dir core/build -R 'unittest_result_enum_mapping|test_spot_actor_dispatch' --output-on-failure`
  - `rg -n "zlink_actor_destroy\\(|zlink_actor_get_ref\\(|zlink_actor_join_spot\\(|zlink_actor_leave_spot\\(|zlink_actor_recv_part\\(|zlink_spot_node_destroy_remote_actor\\(" core/include`
  - `rg -n "ACTOR.*HWM|HWM.*ACTOR|ACTOR_HWM|actor.*hwm|hwm.*actor" core/include core/src doc/spec/draft/spot-entry-transport-queues.ko.md`
- 수정한 파일:
  - `core/include/zlink.h`
  - `core/include/zlink_errno.h`
  - `core/src/api/config_result_internal.hpp`
  - `core/src/api/service_spot_actor_api.cpp`
  - `core/src/api/service_spot_node_api.cpp`
  - `core/tests/integration/test_spot_actor_dispatch.cpp`
  - `core/tests/unittest/unittest_result_enum_mapping.cpp`
- 검증 결과:
  - core build 성공
  - `unittest_result_enum_mapping` 통과
  - `test_spot_actor_dispatch` 통과
  - public header에서 제거 대상 handle Actor API 선언 없음
  - Actor HWM public option 없음
  - full CTest 1차: 102개 중 100개 통과, `test_helper_more_bad_send` timeout, `test_reconnect_ivl` subprocess abort
  - 실패 2개 재실행: `ctest --test-dir core/build -R 'test_helper_more_bad_send|test_reconnect_ivl' --output-on-failure` 통과
- 남은 위험:
  - `zlink_spot_node_entry_spot()`과 `zlink_spot_node_spot_lookup()`은 단계 2에서 logical Spot 계약에 맞춰 다시 구현해야 한다
  - 기존 actor dispatch 테스트는 새 public surface 빌드 복구를 위한 test-local adapter를 사용한다. ENTRY/ENTRY-ACTOR 테스트로 재작성하면서 제거해야 한다
  - core 구현 파일에는 제거 대상 old exported symbol definition이 아직 남아 있다. 단계 3에서 core 내부 참조와 함께 제거해야 한다
- 다음 확인: 단계 2 Entry Spot과 Spot lookup 구현

## 2026-05-06 단계 2

- 날짜: 2026-05-06
- 단계: 단계 2. SpotNode Logical Spot과 Entry Spot
- 확인한 draft spec 절: Entry Spot, Entry Spot handle, Entry Spot routing id, Spot lookup, Spot facade lifecycle, Snapshot과 monitoring
- 이번 단계에서 구현한 계약: SpotNode 소유 logical Spot state, Entry Spot logical state, Entry Spot facade, rid 기반 Spot lookup, lookup facade reference 유지, rid index 갱신, duplicate rid 거부, Entry Spot snapshot 포함, 마지막 일반 Spot facade close의 joined Actor와 pending join busy 처리
- 이번 단계에서 구현하지 않는 계약: Actor 생성 직후 Entry Spot membership, Entry Spot actor count, Actor message dispatch의 Entry Spot 연결
- 관련 회귀 테스트 ID: ENTRY-01, ENTRY-02, ENTRY-04, ENTRY-05, ENTRY-07, ENTRY-08, ENTRY-09, ENTRY-10, ENTRY-11, ENTRY-12, ENTRY-13, ENTRY-14, ENTRY-15
- 수행한 명령:
  - `cmake --build core/build`
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build -R 'test_spot_actor_dispatch' --output-on-failure`
  - `ctest --test-dir core/build -R 'test_spot_actor_dispatch|test_timer_poller|unittest_result_enum_mapping|unittest_spot_subject_access' --output-on-failure`
- 수정한 파일:
  - `core/src/services/spot/spot_handle.hpp`
  - `core/src/services/spot/spot_node_state.hpp`
  - `core/src/services/spot/spot_node.hpp`
  - `core/src/services/spot/spot_node_access.hpp`
  - `core/src/services/spot/spot_node_access.cpp`
  - `core/src/services/spot/spot_node_lifecycle.cpp`
  - `core/src/services/spot/spot_subject_query.cpp`
  - `core/src/api/service_spot_node_api.cpp`
  - `core/src/api/service_spot_actor_api.cpp`
  - `core/tests/integration/test_spot_actor_dispatch.cpp`
- 검증 결과:
  - core build 성공
  - `test_spot_actor_dispatch` 통과
  - `test_timer_poller` 통과
  - `unittest_result_enum_mapping` 통과
  - `unittest_spot_subject_access` 통과
- 남은 위험:
  - Actor table은 아직 Entry Spot logical state를 직접 가리키지 않고 기존 facade pointer를 사용한다
  - Actor Entry membership과 Entry Spot actor count는 단계 3에서 다시 검증해야 한다
- 다음 확인: 단계 3 Actor ref와 lifecycle 구현

## 2026-05-06 단계 3 부분 진행

- 날짜: 2026-05-06
- 단계: 단계 3. Actor Ref와 Lifecycle
- 확인한 draft spec 절: Actor ref, Actor 생성, Actor 조회, Actor destroy, Actor lifecycle 의미 변경, Snapshot과 monitoring
- 이번 단계에서 구현한 계약: Actor 생성 시 Entry Spot logical membership 설정, Actor snapshot의 `joined = 1`과 current Spot rid, Entry Spot actor snapshot, Entry Spot rid의 Actor 생성 뒤 변경 거부, Entry Spot destroy 허용, user Spot destroy 거부, destroy 뒤 같은 id 재생성 시 새 generation 발급, `generation == 0` unchecked ref 유지, 제거 대상 handle 기반 Actor export 제거
- 이번 단계에서 구현하지 않는 계약: Actor table의 SpotNode 내부 소유 구조 이전, node-local generation counter, remote create-or-get의 Entry Spot 보장, remote existing current Spot 보존의 전체 draft 검증
- 관련 회귀 테스트 ID: ENTRY-06, ENTRY-16, ENTRY-ACTOR-01, ENTRY-ACTOR-08, ENTRY-ACTOR-09, ENTRY-ACTOR-33, ENTRY-ACTOR-42, ENTRY-ACTOR-45, ENTRY-ACTOR-48
- 수행한 명령:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build -R 'test_spot_actor_dispatch' --output-on-failure`
  - `ctest --test-dir core/build -R 'test_spot_actor_dispatch|test_timer_poller|unittest_result_enum_mapping|unittest_spot_subject_access' --output-on-failure`
  - `nm -D core/build/lib/libzlink.so | rg "zlink_actor_destroy|zlink_actor_get_ref|zlink_actor_join_spot|zlink_actor_leave_spot|zlink_actor_recv_part|zlink_actor_send_bound_session|zlink_spot_node_destroy_remote_actor"`
- 수정한 파일:
  - `core/src/api/service_spot_actor_api.cpp`
  - `core/src/services/spot/spot_node_lifecycle.cpp`
  - `core/tests/integration/test_spot_actor_dispatch.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - `test_spot_actor_dispatch` 통과
  - `test_timer_poller` 통과
  - `unittest_result_enum_mapping` 통과
  - `unittest_spot_subject_access` 통과
  - full CTest: `ctest --test-dir core/build --output-on-failure` 102개 모두 통과
- 남은 위험:
  - Actor membership은 logical state와 기존 facade pointer를 함께 보존하는 과도기 구조다
  - remote Actor 생성과 remote handoff는 아직 draft 전체 계약에 맞지 않는다
  - bindings와 정식 문서에는 제거 대상 API 설명이 아직 남아 있다
- 다음 확인: 단계 3 잔여 Actor table/generation/remote create-or-get 정리

## 2026-05-06 단계 4 부분 확인

- 날짜: 2026-05-06
- 단계: 단계 4. Actor Message Dispatch와 Recv
- 확인한 draft spec 절: Actor message queue, Actor readable dispatch, Actor recv, Snapshot과 monitoring
- 이번 단계에서 구현한 계약: Entry Spot Actor readable dispatch, callback subject의 `const zlink_actor_ref_t *` 전달, ref 기반 Actor recv, multipart flag 보존, no-data 처리, non-owner recv 거부, join/leave 전후 FIFO 유지, Actor logical queue HWM option 없음
- 이번 단계에서 구현하지 않는 계약: remote handoff 중 pending target buffer와 visibility point 이후 relay
- 관련 회귀 테스트 ID: ENTRY-03, ENTRY-ACTOR-02, ENTRY-ACTOR-10, ENTRY-ACTOR-46, ENTRY-ACTOR-48, ENTRY-ACTOR-50
- 수행한 명령:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build -R 'test_spot_actor_dispatch' --output-on-failure`
- 수정한 파일:
  - `core/tests/integration/test_spot_actor_dispatch.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - `test_spot_actor_dispatch` 통과
- 남은 위험:
  - remote handoff 관련 Actor queue 테스트는 단계 5 remote join 구현 뒤 닫아야 한다
- 다음 확인: 단계 5 local join/leave와 remote handoff 잔여 계약

## 2026-05-06 단계 3 remote create-or-get 보강

- 날짜: 2026-05-06
- 단계: 단계 3. Actor Ref와 Lifecycle
- 확인한 draft spec 절: remote create-or-get, Actor admission handler, Actor destroy, Discovery active route
- 이번 단계에서 구현한 계약: remote create-or-get, admission handler 등록, 이미 있는 Actor의 `EXISTING` 반환, 기존 Actor current Spot 유지, Actor가 없을 때만 admission handler 호출, remote create 성공만으로 active route를 publish하지 않음, user Spot remote Actor destroy 거부
- 이번 단계에서 구현하지 않는 계약: remote join prepare와 handoff 전용 pending Actor state
- 관련 회귀 테스트 ID: ENTRY-ACTOR-11, ENTRY-ACTOR-12, ENTRY-ACTOR-13
- 수행한 명령:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build -R 'test_spot_actor_dispatch' --output-on-failure`
  - `ctest --test-dir core/build -R 'test_spot_actor_dispatch|test_timer_poller|unittest_result_enum_mapping|unittest_spot_subject_access' --output-on-failure`
- 수정한 파일:
  - `core/tests/integration/test_spot_actor_dispatch.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - `test_spot_actor_dispatch` 통과
  - `test_timer_poller` 통과
  - `unittest_result_enum_mapping` 통과
  - `unittest_spot_subject_access` 통과
- 남은 위험:
  - remote handoff 구현은 아직 남아 있다
- 다음 확인: 단계 5 join/leave와 remote handoff 잔여 계약

## 2026-05-06 단계 5/6 Actor join, remote handoff, STREAM binding 보강

- 날짜: 2026-05-06
- 단계: 단계 5. Actor Join, Leave, Remote Handoff / 단계 6. STREAM Session과 Actor 연결
- 확인한 draft spec 절: Actor join, Local join process, Remote join process, Actor leave, STREAM session과 Actor 연결, Session과 local Actor, Session과 remote Actor
- 이번 단계에서 구현한 계약: bound STREAM session 없는 user Spot join 거부, pending join 중 join/leave/destroy busy, leave stale current Spot check, node-local checked generation counter, local join accept/reject/timeout 유지, remote join accept/reject/timeout handoff, target join info remote flag와 target Actor ref, session Actor list CAS 기준 handoff, source Actor retire, remote join 뒤 session relay target 갱신, explicit unbind user Spot 거부, close bound session 뒤 Entry Spot 이동과 unread dispatch, remote request owner의 stale send fire-and-forget drop
- 이번 단계에서 구현하지 않는 계약: Actor table의 `SpotNode` 내부 소유 구조 이전, remote join disconnect race의 visibility 전후 세부 처리, remote protocol drop counter 공개/진단 집계, Spot transport queue/fanout 구조 정리
- 관련 회귀 테스트 ID: ENTRY-ACTOR-03, ENTRY-ACTOR-04, ENTRY-ACTOR-05, ENTRY-ACTOR-06, ENTRY-ACTOR-07, ENTRY-ACTOR-14, ENTRY-ACTOR-15, ENTRY-ACTOR-16, ENTRY-ACTOR-17, ENTRY-ACTOR-18, ENTRY-ACTOR-19, ENTRY-ACTOR-21, ENTRY-ACTOR-22, ENTRY-ACTOR-24, ENTRY-ACTOR-25, ENTRY-ACTOR-26, ENTRY-ACTOR-30, ENTRY-ACTOR-31, ENTRY-ACTOR-32, ENTRY-ACTOR-34, ENTRY-ACTOR-35, ENTRY-ACTOR-38, ENTRY-ACTOR-39, ENTRY-ACTOR-43, ENTRY-ACTOR-44, ENTRY-ACTOR-47, ENTRY-ACTOR-49
- 수행한 명령:
  - `cmake --build core/build`
  - `ctest --test-dir core/build -R 'test_spot_actor_dispatch' --output-on-failure`
  - `ctest --test-dir core/build -R 'test_spot_actor_dispatch|test_timer_poller|unittest_result_enum_mapping|unittest_spot_subject_access|test_stream|test_discovery' --output-on-failure`
  - `ctest --test-dir core/build --output-on-failure`
- 수정한 파일:
  - `core/src/api/service_spot_actor_api.cpp`
  - `core/tests/integration/test_spot_actor_dispatch.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - `test_spot_actor_dispatch` 통과
  - STREAM/Discovery/Actor focused 묶음 27개 통과
  - full CTest: 102개 모두 통과
- 남은 위험:
  - remote handoff는 현재 in-process control model로 구현되어 있으며, 실제 transport protocol drop counter와 disconnect race 정교화는 남아 있다
  - bindings와 정식 문서에는 제거 대상 API 설명과 binding surface가 아직 남아 있다
- 다음 확인: 단계 7 Spot socket 제거와 Queue/Fanout 구조 정리

## 2026-05-06 단계 7 Spot facade resource ownership 보강

- 날짜: 2026-05-06
- 단계: 단계 7. Spot Socket 제거와 Queue/Fanout
- 확인한 draft spec 절: Spot socket 제거 모델, Routed request 처리, Pub/sub 처리
- 이번 단계에서 구현한 계약: `spot_handle_t`에서 physical `spot_pub_t`/`spot_sub_t` 포인터 제거, logical Spot state가 pub/sub side resource를 보관하도록 이전, 같은 logical Spot의 여러 facade 중 마지막 facade가 아닐 때 shared pub/sub resource를 닫지 않는 destroy reference-count 처리
- 이번 단계에서 구현하지 않는 계약: node-level subscription registry, shared message block fanout ref-count, channel reply queue의 logical state 이전, physical SUB greedy drain 내부 정책
- 관련 회귀 테스트 ID: QUEUE-SOCKET-01 일부, 기존 Spot pub/sub scenario, Spot subject access unittest
- 수행한 명령:
  - `cmake --build core/build`
  - `ctest --test-dir core/build -R 'test_spot_actor_dispatch|unittest_spot_subject_access|test_spot_pubsub_scenario|test_spot_service_introspection' --output-on-failure`
- 수정한 파일:
  - `core/src/services/spot/spot_handle.hpp`
  - `core/src/services/spot/spot_node.hpp`
  - `core/src/services/spot/spot_node_access.hpp`
  - `core/src/services/spot/spot_node_access.cpp`
  - `core/src/services/spot/spot_node_lifecycle.cpp`
  - `core/src/services/spot/spot_subject_access.cpp`
  - `core/src/services/spot/spot_subject_option.cpp`
  - `core/src/services/spot/spot_node_summary.cpp`
  - `core/src/api/service_api.cpp`
  - `core/src/api/service_handler_spot_api.cpp`
  - `core/src/api/service_spot_node_api.cpp`
  - `core/tests/unittest/unittest_spot_subject_access.cpp`
  - `core/tests/e2e/spot/spot_pubsub_scenario_shared.cpp`
  - `core/tests/e2e/spot/test_spot_service_introspection.cpp`
- 검증 결과:
  - core build 통과
  - Spot/Actor/subject focused test 19개 통과
  - `unittest_spot_subject_access`의 lazy-create/socket snapshot 회귀로 QUEUE-SOCKET-01 통과 확인
- 남은 위험:
  - pub/sub transport는 아직 logical Spot state의 side resource 경로를 사용하므로 node-level subscription registry와 shared-block fanout 구현이 필요하다
  - request/reply state는 아직 facade owner index와 logical identity index가 함께 있는 과도기 구조다
- 다음 확인: 단계 7 node-level subscription registry와 local fanout 경로 정리

## 2026-05-06 단계 1 ABI와 유지 API 계약 확인

- 날짜: 2026-05-06
- 단계: 단계 1. Public C Surface 반영
- 확인한 draft spec 절: Public C API 변경 요약, Public API 변경
- 이번 단계에서 구현한 계약: Actor public struct size/alignment 확인, 유지 API(`zlink_spot_node_actor_admission_handler`, `zlink_spot_node_create_remote_actor`, `zlink_spot_actor_join_recv`, `zlink_spot_actor_join_reply`)가 header, core 구현, 정식 spec, 테스트에 남아 있음을 확인
- 이번 단계에서 구현하지 않는 계약: bindings public surface 반영
- 관련 회귀 테스트 ID: ENTRY-ACTOR-11, ENTRY-ACTOR-12, ENTRY-ACTOR-13, ENTRY-ACTOR-14, ENTRY-ACTOR-15, ENTRY-ACTOR-16
- 수행한 명령:
  - `c++ -Icore/include /tmp/zlink_actor_abi_check.cpp -o /tmp/zlink_actor_abi_check && /tmp/zlink_actor_abi_check`
  - `rg -n "zlink_spot_node_actor_admission_handler|zlink_spot_node_create_remote_actor|zlink_spot_actor_join_recv|zlink_spot_actor_join_reply" core/include/zlink.h doc/spec/core/service/spot.ko.md doc/spec/core/service/spot.md core/src/api/service_spot_actor_api.cpp core/tests/integration/test_spot_actor_dispatch.cpp`
- 수정한 파일:
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - `zlink_actor_ref_t size=520 align=8`
  - `zlink_actor_recv_info_t size=1040 align=8`
  - `zlink_actor_join_info_t size=2088 align=8`
  - `zlink_actor_create_result_t size=528 align=8`
- 남은 위험:
  - bindings FFI struct 선언은 아직 새 size/alignment와 맞춰 갱신해야 한다
- 다음 확인: bindings 단계에서 언어별 FFI ABI 재검증

## 2026-05-06 단계 9 비목표 core/header 검증

- 날짜: 2026-05-06
- 단계: 단계 9. 비목표와 제거 대상 검증
- 확인한 draft spec 절: Actor channel API 여부, Channel router에서 Actor로 직접 messaging, 비목표
- 이번 단계에서 구현한 계약: Actor 전용 dispatch context, recv callback, channel request public API, Actor channel router protocol, reliable pub/sub protocol, Actor placement policy, Entry Spot application policy, framework typed Actor 객체가 core/header/정식 문서에 새 public 계약으로 추가되지 않았음을 확인
- 이번 단계에서 구현하지 않는 계약: bindings와 samples의 제거 API 삭제 확인
- 관련 회귀 테스트 ID: ENTRY-ACTOR-42, ENTRY-ACTOR-45, QUEUE-SOCKET-01 일부
- 수행한 명령:
  - `rg -n "zlink_spot_node_actor_request_channel_part|zlink_spot_node_actor_send_channel_part|Actor.*HWM|ACTOR.*HWM|actor.*hwm|actor_request_channel|actor_send_channel" core/include core/src doc/spec/core doc/guide doc/internals`
  - `rg -n "ZLINK_.*ACTOR.*HWM|ACTOR_.*HWM|actor.*capacity|Actor.*capacity" core/include core/src doc/spec/core doc/guide doc/internals`
  - `rg -n "channel.*Actor|Actor.*channel|ACTOR.*CHANNEL|CHANNEL.*ACTOR" core/include core/src doc/spec/core doc/guide doc/internals`
- 수정한 파일:
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - Actor 전용 channel public API 이름은 core/header/정식 문서에 없음
  - Actor HWM/capacity public option은 없음
  - channel과 Actor 직접 protocol 설명은 정식 문서에 없음
- 남은 위험:
  - bindings/samples에는 제거 대상 C API 참조가 아직 남아 있어 최종 stale API gate는 열려 있다
- 다음 확인: bindings/samples 제거 API 정리

## 2026-05-06 단계 7 logical pub/sub queue와 fanout 보강

- 날짜: 2026-05-06
- 단계: 단계 7. Spot Socket 제거와 Queue/Fanout
- 확인한 draft spec 절: Pub/sub 처리, Subscription table, Fanout, Queue와 backpressure
- 이번 단계에서 구현한 계약: logical Spot state에 subscription filter set과 subscribe queue를 추가했고, local publish가 matching Spot queue에 shared message block reference를 enqueue한다. `zlink_subscription_at()`과 `ZLINK_SUB_OPT_TOPICS_COUNT`는 logical filter set과 backing sub filter를 병합하되 중복을 제거한다. `zlink_spot_subscribe_part()`는 logical subscribe queue를 먼저 drain하고, public `zlink_msg_t`는 unread shared block과 독립된 handle로 반환한다.
- 이번 단계에서 구현하지 않는 계약: routed logical queue, physical SUB greedy drain 정책, channel reply logical queue, publish dead Spot/shutdown failure 세부 errno
- 관련 회귀 테스트 ID: QUEUE-PUB-01, QUEUE-PUB-02, QUEUE-PUB-03, QUEUE-PUB-04, QUEUE-PUB-05, QUEUE-PUB-06, QUEUE-SUB-01, QUEUE-SUB-02, QUEUE-SUB-03, QUEUE-SUB-06, QUEUE-SUB-07
- 수행한 명령:
  - `cmake --build core/build`
  - `ZLINK_TEST_CASE=test_queue_pub_local_fanout_shared_block core/build/bin/test_spot_service_introspection`
  - `ZLINK_TEST_CASE=test_queue_sub_exact_pattern_dedupe core/build/bin/test_spot_service_introspection`
  - `ctest --test-dir core/build -R 'test_spot_service_introspection|test_spot_pubsub_scenario|unittest_spot_subject_access|unittest_typed_option' --output-on-failure`
- 수정한 파일:
  - `core/src/services/spot/spot_handle.hpp`
  - `core/src/services/spot/spot_node.hpp`
  - `core/src/services/spot/spot_node_lifecycle.cpp`
  - `core/src/services/spot/spot_subject_publish.cpp`
  - `core/src/services/spot/spot_subject_query.cpp`
  - `core/src/services/spot/spot_subject_option.cpp`
  - `core/src/api/service_spot_api.cpp`
  - `core/tests/e2e/spot/test_spot_service_introspection.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - core build 통과
  - 추가한 QUEUE pub/sub focused test 2개 단독 통과
  - Spot pub/sub/introspection/subject option focused test 19개 통과
- 남은 위험:
  - local publish transport는 아직 existing pub attachment 경로를 같이 사용하므로 node-owned physical transport 순수화 항목은 계속 열어 둔다
  - remote pub/sub ingress를 logical queue로 직접 fanout하는 경로와 physical SUB greedy drain 제어는 별도 구현이 필요하다
- 다음 확인: routed queue와 channel reply queue의 logical state 이전

## 2026-05-06 단계 7 routed logical queue 전환

- 날짜: 2026-05-06
- 단계: 단계 7. Spot Socket 제거와 Queue/Fanout
- 확인한 draft spec 절: Routed request 처리, Queue와 backpressure
- 이번 단계에서 구현한 계약: routed ingress payload를 inproc socket frame이 아니라 `routed_recv_queue.pending` in-memory queue에 보관하고, `zlink_spot_recv()` 계열은 readiness signal을 소비한 뒤 pending queue에서 payload를 drain한다. 기존 poller 호환을 위해 pair socket은 payload transport가 아니라 readable signal로만 유지한다.
- 이번 단계에서 구현하지 않는 계약: routed ingress backpressure의 transport HWM/drain 제어 검증, channel reply logical queue 이전
- 관련 회귀 테스트 ID: QUEUE-ROUTED-01, QUEUE-ROUTED-03
- 수행한 명령:
  - `cmake --build core/build`
  - `ctest --test-dir core/build -R 'test_spot_dispatch_event|test_spot_poller|test_spot_runtime_activation|test_zmp_request_reply|test_spot_service_introspection' --output-on-failure`
- 수정한 파일:
  - `core/src/api/service_spot_request_reply_queue.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
  - `doc/plan/spot-entry-transport-queues/logs/implementation-review-log.ko.md`
- 검증 결과:
  - core build 통과
  - routed/reqrep/Spot poller focused test 13개 통과
- 남은 위험:
  - pair socket signal 자체는 poller compatibility layer로 남아 있으므로 QUEUE-ROUTED-02는 별도 backpressure 정책 검증 뒤 닫아야 한다
- 다음 확인: channel reply completion queue와 shared dealer completion 분리

## 2026-05-06 전체 core CTest 중간 확인

- 날짜: 2026-05-06
- 단계: 단계 10 core 검증 중간 확인
- 수행한 명령:
  - `ctest --test-dir core/build --output-on-failure`
  - `ctest --test-dir core/build -R '^test_spot_pubsub_scenario$' --output-on-failure`
- 검증 결과:
  - 전체 CTest 102개 중 101개 통과
  - `test_spot_pubsub_scenario` 통합 바이너리가 전체 실행 중 `fast_mutex` invalid argument로 1회 abort
  - 같은 `test_spot_pubsub_scenario` 단독 재실행은 통과
- 판단:
  - 현재 결과만으로 core unit/integration gate를 닫지 않는다
  - 이후 수정 뒤 전체 CTest를 다시 실행해 재현 여부를 확인한다
- 다음 확인: channel reply queue와 Actor remote join 미완료 항목 구현 뒤 전체 CTest 재실행

## 2026-05-06 단계 5/6 remote join pending state와 timeout 보강

- 날짜: 2026-05-06
- 단계: 단계 5. Actor Join, Leave, Remote Handoff / 단계 6. STREAM Session과 Actor 연결
- 확인한 draft spec 절: Remote join process, remote join 원자성, STREAM session Actor binding, remote Actor send fire-and-forget
- 이번 단계에서 구현한 계약: remote join submit 시 target node에 pending Actor state를 만들고, 이 pending Actor는 live lookup, actor snapshot, Spot actor snapshot, active route에 노출하지 않는다. join reject, timeout, STREAM disconnect before visibility, session mapping conflict에서는 pending target을 제거한다. accept 뒤 session Actor list compare-and-swap이 성공하면 target Actor를 활성화하고 source Actor를 retire한다. `timeout_ms_ == 0` request lock은 nonblocking try-lock으로 바꾸었고, stale remote fire-and-forget send는 message를 닫은 뒤 내부 protocol drop counter를 증가시킨다.
- 이번 단계에서 구현하지 않는 계약: visibility point와 visible commit 사이 relay buffering의 실제 비동기 interleave 테스트, Actor table 물리 소유권의 `SpotNode` 내부 state 이전
- 관련 회귀 테스트 ID: ENTRY-ACTOR-23, ENTRY-ACTOR-28, ENTRY-ACTOR-29, ENTRY-ACTOR-36, ENTRY-ACTOR-37, ENTRY-ACTOR-40, ENTRY-ACTOR-41
- 수행한 명령:
  - `cmake --build core/build --target test_spot_actor_dispatch && core/build/bin/test_spot_actor_dispatch`
  - `ctest --test-dir core/build -R 'test_spot_actor_dispatch|test_spot_dispatch_event|test_spot_runtime_activation|test_stream_socket' --output-on-failure`
  - `git diff --check -- core/src/api/service_spot_actor_api.cpp core/tests/integration/test_spot_actor_dispatch.cpp`
- 수정한 파일:
  - `core/src/api/service_spot_actor_api.cpp`
  - `core/tests/integration/test_spot_actor_dispatch.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
  - `doc/plan/spot-entry-transport-queues/logs/implementation-review-log.ko.md`
- 검증 결과:
  - `test_spot_actor_dispatch` 단독 23개 테스트 통과
  - Spot Actor/dispatch/runtime/STREAM focused CTest 10개 통과
  - diff whitespace check 통과
- 남은 위험:
  - remote join의 pending target state는 현재 API translation unit의 actor runtime table 안에 있으므로, Stage 3의 `SpotNode` 직접 소유 구조 항목은 아직 닫지 않는다
  - remote join visibility 뒤 disconnect cleanup은 `test_actor_remote_join_handoff_accept_reject_timeout` 안에서 별도 stream으로 검증했다
- 다음 확인: Stage 5 남은 ENTRY-ACTOR-20/27/41과 Stage 7 channel reply logical queue

## 2026-05-06 단계 7 channel reply queue 검증

- 날짜: 2026-05-06
- 단계: 단계 7. Spot Socket 제거와 Queue/Fanout
- 확인한 draft spec 절: Channel reply queue, Spot socket 제거 모델
- 이번 단계에서 구현한 계약: channel request completion은 요청 Spot의 channel reply completion queue에 enqueue되고, dispatch event subject는 shared dealer socket으로 유지된다. 같은 dealer transport를 여러 Spot이 공유해도 completion은 bridge context가 가진 요청 Spot state로 분리되며, `zlink_spot_channel_reply_progress_from()`이 해당 Spot queue만 drain한다.
- 이번 단계에서 구현하지 않는 계약: channel dealer 자체의 node-owned lifecycle을 더 깊게 정리하는 POSD 리팩토링
- 관련 회귀 테스트 ID: QUEUE-CHAN-01, QUEUE-CHAN-02
- 수행한 명령:
  - `cmake --build core/build --target test_spot_dispatch_event && core/build/bin/test_spot_dispatch_event`
- 수정한 파일:
  - `core/tests/integration/test_spot_dispatch_event.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
  - `doc/plan/spot-entry-transport-queues/logs/implementation-review-log.ko.md`
- 검증 결과:
  - `test_spot_dispatch_event` 단독 13개 테스트 통과
  - 새 shared dealer per-request Spot test가 두 Spot의 completion 분리를 검증함
- 남은 위험:
  - Stage 7에는 publish dead Spot/shutdown failure, peer subscription event queue drain, physical SUB greedy drain, routed backpressure 세부 검증이 남아 있다
- 다음 확인: Stage 7 pub/sub/routed 잔여 항목

## 2026-05-06 단계 7 publish path와 subscription event 정리

- 날짜: 2026-05-06
- 단계: 단계 7. Spot Socket 제거와 Queue/Fanout
- 확인한 draft spec 절: Publish path, Subscription table, 수신 API 연결
- 이번 단계에서 구현한 계약: `zlink_spot_publish_part()`는 staged multipart를 `spot_publish_impl()`로 넘기고, 같은 node service namespace에서는 `spot_subject_publish()`가 node-created publish transport와 local logical fanout을 함께 수행한다. `zlink_spot_subscription_event_recv()`는 node-owned service subscription event queue만 drain하고, per-Spot XPUB recv fallback을 제거했다.
- 이번 단계에서 구현하지 않는 계약: publish dead Spot 또는 SpotNode shutdown 중 세부 failure test, physical SUB greedy drain threshold test
- 관련 회귀 테스트 ID: QUEUE-PUB-01, QUEUE-PUB-02, QUEUE-PUB-03, QUEUE-SUB-01, QUEUE-SUB-02, QUEUE-SUB-03
- 수행한 명령:
  - `cmake --build core/build --target test_spot_service_introspection test_spot_pubsub_scenario test_spot_dispatch_event && ctest --test-dir core/build -R 'test_spot_service_introspection|test_spot_pubsub_scenario|test_spot_dispatch_event' --output-on-failure`
- 수정한 파일:
  - `core/src/api/service_spot_api.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
  - `doc/plan/spot-entry-transport-queues/logs/implementation-review-log.ko.md`
- 검증 결과:
  - Spot service introspection/pubsub/dispatch focused CTest 18개 통과
- 남은 위험:
  - QUEUE-PUB-07, QUEUE-SUB-04, QUEUE-SUB-05는 별도 회귀 테스트 또는 내부 정책 검증이 필요하다
- 다음 확인: Stage 7 routed backpressure와 pub/sub 잔여 회귀 테스트

## 2026-05-06 Actor table ownership와 remote join conflict 보강

- 날짜: 2026-05-06
- 단계: 단계 3. Actor Ref와 Lifecycle / 단계 5. Actor Join, Leave, Remote Handoff
- 확인한 draft spec 절: Actor lifecycle, Remote join process, STREAM session Actor binding
- 이번 단계에서 구현한 계약: Actor handle set, actor id index, node-local generation counter를 `SpotNode` 내부 actor state로 옮겼다. remote join accept 중 session Actor list 갱신이 실패하면 pending target을 제거하고 source Actor를 유지한다. target pending Actor queue는 commit 성공 시 source queue 뒤에 보존한다.
- 이번 단계에서 구현하지 않는 계약: 없음
- 관련 회귀 테스트 ID: ENTRY-ACTOR-20, ENTRY-ACTOR-27
- 수행한 명령:
  - `cmake --build core/build --target test_spot_actor_dispatch && core/build/bin/test_spot_actor_dispatch`
- 수정한 파일:
  - `core/src/services/spot/spot_node_state.hpp`
  - `core/src/services/spot/spot_node.hpp`
  - `core/src/services/spot/spot_node_access.hpp`
  - `core/src/services/spot/spot_node_access.cpp`
  - `core/src/api/service_spot_actor_api.cpp`
  - `core/tests/integration/test_spot_actor_dispatch.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - `test_spot_actor_dispatch` 단독 23개 테스트 통과
- 남은 위험:
  - ENTRY-ACTOR-27은 public header에 노출하지 않는 테스트 전용 hook으로 pending target queue 보존을 검증한다
- 다음 확인: QUEUE-SUB-04/05와 QUEUE-ROUTED-02

## 2026-05-06 QUEUE-PUB-07 lifecycle failure 보강

- 날짜: 2026-05-06
- 단계: 단계 7. Spot Socket 제거와 Queue/Fanout
- 확인한 draft spec 절: Publish path
- 이번 단계에서 구현한 계약: closing Spot publish와 shutdown 중 SpotNode publish가 submit terminated 계열로 실패하도록 publish path와 submit errno 분류를 보강했다.
- 관련 회귀 테스트 ID: QUEUE-PUB-07
- 수행한 명령:
  - `cmake --build core/build --target test_spot_service_introspection && ZLINK_TEST_CASE=test_queue_pub_dead_spot_fails core/build/bin/test_spot_service_introspection`
- 수정한 파일:
  - `core/src/api/service_spot_api.cpp`
  - `core/src/core/internal_errno.hpp`
  - `core/src/services/spot/spot_node_access.hpp`
  - `core/src/services/spot/spot_node_access.cpp`
  - `core/src/services/spot/spot_subject_publish.cpp`
  - `core/tests/e2e/spot/test_spot_service_introspection.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - `test_queue_pub_dead_spot_fails` 단독 통과
- 남은 위험:
  - 전체 submit errno 분류 변경은 전체 CTest로 다시 확인해야 한다
- 다음 확인: Stage 7 routed/subscription 잔여 항목

## 2026-05-06 Stage 7 routed/subscription drain 회귀 확인

- 날짜: 2026-05-06
- 단계: 단계 7. Spot Socket 제거와 Queue/Fanout
- 확인한 draft spec 절: Routed request 처리 backpressure 정책, Pub/sub backpressure 정책, Subscription table 동시성 규칙
- 이번 단계에서 구현한 계약: dispatch callback은 subscribe/routed logical queue를 `EAGAIN`까지 drain하되 callback 실행은 직렬화된다. callback 안 subscription 변경은 허용되며 새 filter는 이후 publish fanout부터 적용된다.
- 관련 회귀 테스트 ID: QUEUE-ROUTED-02, QUEUE-SUB-04, QUEUE-SUB-05
- 수행한 명령:
  - `cmake --build core/build --target test_spot_dispatch_event && core/build/bin/test_spot_dispatch_event`
- 수정한 파일:
  - `core/tests/integration/test_spot_dispatch_event.cpp`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - `test_spot_dispatch_event` 단독 14개 테스트 통과
- 남은 위험:
  - remote physical SUB transport HWM은 focused e2e smoke와 전체 CTest에서 추가 확인한다
- 다음 확인: 제거 API와 core 검증 gate

## 2026-05-06 core 전체 build/CTest 재검증

- 날짜: 2026-05-06
- 단계: 단계 10. Sample/Perf와 전체 검증
- 수행한 명령:
  - `cmake --build core/build && ctest --test-dir core/build --output-on-failure`
  - 첫 실행은 `test_helper_request_sequence_failure`가 94초 동안 진행되어 수동 중단했고, 단독 재실행은 1개 테스트 통과
  - `ctest --test-dir core/build --output-on-failure`
- 검증 결과:
  - 두 번째 전체 CTest 102개 전부 통과
  - 기존 SPOT, STREAM, Discovery, Registry label 포함 테스트가 통과했다
- 다음 확인: 제거 API stale reference 정리와 public header compile check

## 2026-05-06 Stage 10 회귀 테스트 자동화 매핑

- 날짜: 2026-05-06
- 단계: 단계 10. Core 회귀 테스트와 전체 검증
- 확인한 draft spec 절: 회귀 테스트 매트릭스
- 이번 단계에서 구현한 계약: draft의 `ENTRY-*`, `ENTRY-ACTOR-*`, `QUEUE-*` 회귀 테스트가
  자동 테스트로 닫혔는지 matrix와 core test runner를 대조했다.
- 관련 회귀 테스트 ID:
  - `ENTRY-01`..`ENTRY-16`
  - `ENTRY-ACTOR-01`..`ENTRY-ACTOR-50`
  - `QUEUE-ROUTED-01`..`QUEUE-ROUTED-03`
  - `QUEUE-PUB-01`..`QUEUE-PUB-07`
  - `QUEUE-SUB-01`..`QUEUE-SUB-07`
  - `QUEUE-CHAN-01`..`QUEUE-CHAN-02`
  - `QUEUE-SOCKET-01`
- 수행한 명령:
  - `rg -n "(ENTRY-ACTOR|ENTRY|QUEUE-[A-Z]+)-[0-9]+" doc/spec/draft/spot-entry-transport-queues.ko.md`
  - `comm -23 <(rg -o "(ENTRY-ACTOR|ENTRY|QUEUE-[A-Z]+)-[0-9]+" doc/spec/draft/spot-entry-transport-queues.ko.md | sort -u) <(rg -o "(ENTRY-ACTOR|ENTRY|QUEUE-[A-Z]+)-[0-9]+" doc/plan/spot-entry-transport-queues/logs/contract-matrix.ko.md | sort -u)`
  - `ctest --test-dir core/build --output-on-failure`
- 수정한 파일:
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
  - `doc/plan/spot-entry-transport-queues/logs/implementation-review-log.ko.md`
- 검증 결과:
  - draft의 테스트 ID가 contract matrix에 누락 없이 들어 있다.
  - Entry와 Actor 회귀는 `test_spot_actor_dispatch`의 자동 테스트로 실행된다.
  - Queue 회귀는 `test_spot_dispatch_event`, `test_spot_service_introspection`,
    `test_spot_pubsub_scenario`, `unittest_spot_subject_access` 자동 테스트로 실행된다.
  - 전체 CTest 102개가 전부 통과했다.
- 남은 위험: 테스트 이름은 draft ID를 그대로 포함하지 않으므로 matrix와 구현 로그로 추적한다.
- 다음 확인: 구현 후 문서-코드 반복 리뷰

## 2026-05-06 구현 후 문서-코드 반복 리뷰

- 날짜: 2026-05-06
- 단계: 구현 후 문서-코드 반복 리뷰
- 확인한 draft spec 절: Public C API 변경, Entry Spot, Actor ref, Actor join/leave,
  STREAM session과 Actor 연결, Spot socket 제거 모델, Fanout, Channel dealer 처리,
  비목표
- 수행한 명령:
  - `comm -23 <(rg -o "(ENTRY-ACTOR|ENTRY|QUEUE-[A-Z]+)-[0-9]+" doc/spec/draft/spot-entry-transport-queues.ko.md | sort -u) <(rg -o "(ENTRY-ACTOR|ENTRY|QUEUE-[A-Z]+)-[0-9]+" doc/plan/spot-entry-transport-queues/logs/contract-matrix.ko.md | sort -u)`
  - `rg -n "zlink_spot_node_entry_spot|zlink_spot_node_spot_lookup|zlink_actor_ref_t|ZLINK_ACTOR_ID_MAX|zlink_remote_actor_get_ref|zlink_actor_recv_info_t|zlink_actor_join_info_t|ZLINK_ACTOR_JOIN_INFO_REMOTE|zlink_actor_admission_result_t|zlink_actor_create_result_t|ZLINK_CONFIG_INVALID_STATE|ZLINK_CONFIG_NOT_FOUND|ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE|ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE|ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR|zlink_spot_node_actor_new|zlink_spot_node_actor_lookup|zlink_spot_node_actor_recv_part|zlink_spot_node_actor_join_spot|zlink_spot_node_actor_leave_spot|zlink_spot_node_actor_destroy|zlink_spot_node_actor_send_bound_session_msg|zlink_spot_node_actor_close_bound_session|zlink_spot_node_actor_admission_handler|zlink_spot_node_create_remote_actor|zlink_spot_actor_join_recv|zlink_spot_actor_join_reply" core/include/zlink.h core/include/zlink_errno.h core/include/zlink_enum.h`
  - `rg -n "zlink_actor_destroy\\(|zlink_actor_get_ref\\(|zlink_actor_join_spot\\(|zlink_actor_leave_spot\\(|zlink_actor_recv_part\\(|zlink_spot_node_destroy_remote_actor\\(" core/include core/src doc/spec/core doc/spec/bindings doc/spec/sample doc/guide doc/internals bindings -g "!**/native/**" -g "!**/build/**"`
  - `rg -n "generation == 0.*invalid|invalid.*generation == 0|generation 0.*invalid|invalid.*generation 0" core/include core/src doc/spec/core doc/guide doc/internals bindings/c bindings/cpp bindings/dotnet bindings/go -g "!bindings/go/native/**" -g "!bindings/*/native/**" -g "!**/build/**"`
  - `git diff --check`
- 발견한 문제:
  - Python/Rust binding에는 제거 대상 old C API 참조가 남아 있다. 이 항목은 release 뒤
    bindings 순차 적용 단계에서 Python, Rust 순서로 닫는다.
  - core/header/core samples/정식 문서 범위에서는 제거 대상 API 잔존을 찾지 못했다.
- 수정한 파일:
  - `doc/plan/spot-entry-transport-queues/logs/contract-matrix.ko.md`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
  - `doc/plan/spot-entry-transport-queues/logs/implementation-review-log.ko.md`
- 검증 결과:
  - contract matrix의 모든 행 상태를 `reviewed`로 갱신했다.
  - draft 테스트 ID는 matrix에 누락 없이 들어 있다.
  - public C 신규/변경 API와 enum/type은 public header와 정식 spec에 반영되어 있다.
  - `generation == 0`을 invalid로 설명하거나 처리하는 잔존 항목은 찾지 못했다.
  - `git diff --check` 통과.
- 남은 위험: full binding stale API 정리는 bindings 단계에서 닫아야 한다.
- 다음 확인: POSD 기반 전체 리팩토링 루프
