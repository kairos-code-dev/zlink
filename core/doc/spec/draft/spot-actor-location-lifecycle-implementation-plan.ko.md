# Actor 위치 생명주기와 Spot join 검토 및 구현 후 리팩토링 계획

이 문서는
[`spot-actor-location-lifecycle.ko.md`](spot-actor-location-lifecycle.ko.md)를
구현 가능한 수준으로 완성하고, 구현 뒤 POSD 기반 리팩토링까지 끝내기 위한 반복
검토 계획이다.

대상 draft 문서는 **구현 전 초안**이며 **현재 공개 계약이 아니다**. 구현 전 단계에서는
정식 spec, guide, internals 문서에 새 계약을 섞어 쓰지 않는다.

## 목적

새 컨텍스트에서 작업하더라도 사용자 개입 없이 아래 목표를 끝까지 수행할 수 있게 한다.

draft 검토 단계의 목표:

1. 대상 draft 문서를 처음부터 끝까지 읽는다.
2. 현재 공개 header와 실제 구현을 확인한다.
3. 누락된 계약, 모순, 오래된 signature, 구현자가 판단해야 하는 빈칸을 찾는다.
4. 대상 draft 문서만 수정한다.
5. 더 이상 누락이 없다고 판단될 때까지 반복해서 리뷰한다.

구현 단계까지 진행한 뒤의 목표:

1. draft 계약에 맞춰 구현된 전체 변경 범위를 리뷰한다.
2. POSD 기준으로 복잡성, 정보 누출, 얕은 모듈, 시간적 분해, pass-through 흐름을 찾는다.
3. 리팩토링 요소가 남지 않을 때까지 코드 수정과 리뷰를 반복한다.
4. 관련 테스트와 검증 명령을 실행하고 결과를 기록한다.

## 대상 문서

- 설계 초안:
  [`doc/spec/draft/spot-actor-location-lifecycle.ko.md`](spot-actor-location-lifecycle.ko.md)
- 이 계획:
  [`doc/spec/draft/spot-actor-location-lifecycle-implementation-plan.ko.md`](spot-actor-location-lifecycle-implementation-plan.ko.md)

## 참조할 파일

아래 파일은 현재 공개 API, 실제 구현, 설계 판단 기준으로 확인한다.

- `core/include/zlink.h`
- `core/include/zlink_errno.h`
- `core/src/api/service_spot_actor_api.cpp`
- `core/src/api/socket_message_send_api.cpp`
- `core/src/api/part_helper_api.cpp`
- `core/src/api/service_spot_request_reply_part_submit.cpp`
- `core/src/api/socket_request_reply_submit_api.cpp`
- `doc/principal/software-design-principles.md`
- 구현 중 새로 변경된 파일 전체

## 반드시 유지할 설계 결정

- Actor 생성, Spot 이동, session attach는 서로 다른 상태 전이다.
- Actor 생성은 local Entry Spot에 배치하며 active route를 공개하지 않는다.
- Actor 생성 성공도 Entry Spot `on_join` lifecycle callback 대상이다.
- Entry Spot은 생성과 leave 뒤 복귀하는 lobby다.
- remote Entry Spot 직접 join API는 제공하지 않는다.
- remote Actor 생성 API는 제거한다.
- user Spot join은 session attach 없이 가능해야 한다.
- user Spot join 성공 시 active route를 target user Spot으로 갱신한다.
- user Spot leave 성공 시 같은 owner node의 Entry Spot으로 돌아가고 active route를
  Entry Spot으로 갱신한다.
- join target으로 Entry Spot을 넘기면 invalid argument 계열 실패다.
- Spot lifecycle callback `on_join`/`on_leave`는 Entry Spot과 일반 Spot 모두 받을 수
  있다.
- lifecycle callback은 등록한 경우에만 호출하며 replay하지 않는다.
- 같은 Spot idempotent join은 admission과 lifecycle callback 없이 success completion이다.
- join completion은 최종 Actor ref를 반환해야 한다.
- remote join 성공 시 source ref가 아니라 target node의 final Actor ref를 반환한다.
- `zlink_actor_join_result_t`와 `zlink_spot_actor_lifecycle_info_t`에는 node rid를 별도
  중복 필드로 넣지 않고 Actor ref 안의 `node_rid`를 사용한다.
- `zlink_remote_actor_get_ref()`는 unchecked ref 생성 helper가 아니라 async checked
  remote lookup API다.
- `zlink_remote_actor_get_ref()`는 Actor를 생성하거나 위치를 바꾸거나 active route를
  갱신하지 않는다.
- `zlink_discovery_resolve_actor()`는 Registry active route 조회이고,
  `zlink_remote_actor_get_ref()`는 target node에 직접 확인하는 checked lookup이다.
- session attach는 Actor 위치와 독립이다.
- 하나의 session은 여러 Actor를 attach할 수 있다.
- 하나의 Actor는 동시에 하나의 session에만 attach된다.
- session Actor mapping 조회 API 이름은 `zlink_stream_bound_actors()`다.
- `zlink_stream_bind_actor()`와 `zlink_stream_unbind_actor()`는 `node` 인자를 제거하고
  `stream` owner SpotNode를 사용한다.
- `zlink_stream_send_bound_actor_part()`도 `node` 인자를 제거하고 stream owner를 통해
  Actor owner로 relay한다.
- `zlink_spot_node_actor_send_bound_session_msg()`는 Actor에서 bound session으로 보내는
  반대 방향 relay이며 ABI는 유지하되 nonblocking submit과 backpressure 계약을
  명확히 한다.
- remote 결과가 필요한 Actor 위치/attach API는 blocking request가 아니라
  `zlink_submit_result_t`와 completion으로 통일한다.
- local 대상도 같은 async completion 경로를 사용한다.
- `timeout_ms == 0`은 Actor 위치/attach API에서 operation timeout 없음이다.
- `timeout_ms`는 mutex lock 대기 시간이 아니라 submit 이후 completion까지의 operation
  timeout이다.
- timeout이 없는 작업도 close, disconnect, remove로 더 이상 완료할 수 없으면 기존
  request result로 completion되어야 한다.
- relay submit에서 내부 queue나 runtime lock을 즉시 확보하지 못하면 blocking하지 않고
  backpressured 계열로 실패한다.
- Actor relay API가 `ZLINK_SUBMIT_OK`를 반환하면 message 소유권은 라이브러리로
  이전된다.
- Actor relay API가 submit 실패를 반환하면 message 소유권은 caller에게 남고
  implementation은 message를 close하거나 reinit하지 않는다.
- `zlink_spot_node_create_remote_actor()`, `zlink_spot_node_actor_admission_handler()`,
  관련 create/admission typedef는 제거 대상이다.
- `zlink_spot_dispatch_event_t`와 기존 result enum에는 새 값을 추가하지 않는다.
- 새 flag bit는 첫 구현에 추가하지 않고 flags는 예약 필드로 0을 채운다.

## draft 반복 리뷰 절차

아래 절차는 draft 문서를 구현 가능한 수준으로 만들기 위한 단계다. 이 단계에서는
구현 전 초안 규칙을 지키기 위해 대상 draft 문서만 수정하고, 정식 spec, guide,
internals 문서는 수정하지 않는다. 아래 절차를 체크리스트가 모두 통과할 때까지
반복한다.

1. [`spot-actor-location-lifecycle.ko.md`](spot-actor-location-lifecycle.ko.md)를
   처음부터 끝까지 읽는다.
2. 현재 `core/include/zlink.h`와 실제 구현 파일을 확인해서 draft 변경안과 현재 구현의
   차이를 다시 검증한다.
3. 누락, 모순, 오래된 signature, 모호한 ownership, 모호한 timeout, 모호한 callback
   lifetime이 있으면 대상 draft 문서만 수정한다.
4. 수정 뒤 다시 대상 draft 문서 전체를 읽는다.
5. 새로 생긴 모순이나 과한 범위 확장이 있으면 다시 수정한다.
6. 검증 명령을 실행한다.
7. 검증 결과와 남은 리스크를 정리한다.

구현 단계까지 진행한 뒤에는 draft 검토 단계와 별개로
[POSD 기반 리팩토링 단계](#posd-기반-리팩토링-단계)를 마지막에 수행한다.

## 체크리스트

- draft 첫머리에 구현 전 초안이며 현재 공개 계약이 아님을 명확히 적었는가?
- 제거 대상 typedef와 function이 빠짐없이 명시되어 있는가?
- 추가 대상 typedef와 function이 모두 선언 형태로 제시되어 있는가?
- 변경 대상 function signature가 모두 한곳에 모여 있는가?
- ABI breaking change 범위와 binding 갱신 범위가 명확한가?
- `zlink_remote_actor_get_ref()`가 async checked lookup으로 명확한가?
- `zlink_spot_node_actor_join_spot()` completion이 final Actor ref를 반환하는가?
- `zlink_spot_node_actor_leave_spot()`와 `zlink_spot_node_actor_destroy()`가 async submit
  및 completion으로 명확한가?
- bind/unbind가 async submit 및 completion이고 `node` 인자를 받지 않는가?
- `zlink_stream_send_bound_actor_part()`가 `node` 인자를 받지 않는가?
- `zlink_stream_bound_actors()`의 2-pass snapshot 규칙, empty mapping, stale 가능성이
  명확한가?
- `zlink_spot_node_actor_send_bound_session_msg()`의 relay 방향과 owner 관계가 명확한가?
- completion payload가 없는 API는 `parts == NULL`, `part_count == 0`이 명시되어 있는가?
- callback result pointer lifetime이 명시되어 있는가?
- lifecycle callback info pointer lifetime이 명시되어 있는가?
- join reply payload ownership과 Actor relay message ownership이 명확한가?
- validation 실패와 submit 실패 시 payload/message ownership이 명확한가?
- timeout 정책이 모든 Actor 위치/attach API에 일관되게 적용되는가?
- `timeout_ms == 0` 의미가 모든 관련 API에서 모순 없이 설명되어 있는가?
- no handler와 timeout 없는 join pending 조건이 명확한가?
- timeout 없는 작업의 close/disconnect/remove completion 조건이 명확한가?
- Entry Spot 생성, join, leave, destroy lifecycle callback 동작이 명확한가?
- Actor 생성 `on_join`이 active route 공개가 아님이 명확한가?
- user Spot join admission과 Entry Spot lobby 개념이 분리되어 있는가?
- Entry Spot을 join target으로 금지하는 규칙이 명확한가?
- explicit leave idempotent success 조건이 `current_spot_rid == Entry Spot rid`일 때로
  명확한가?
- stale leave 조건이 명확한가?
- destroy는 Entry Spot Actor에만 성공하고 user Spot Actor는 실패하는가?
- pending join 중 join/leave/destroy 결과가 명확한가?
- remote join pending target lookup 숨김이 명확한가?
- active route publish, update, remove 시점이 모두 명확한가?
- session attach/detach가 active route를 변경하지 않는가?
- remote join 뒤 session mapping을 자동 compare-and-swap하지 않는다는 점이 명확한가?
- 하나의 session이 여러 Actor를 attach할 수 있고 하나의 Actor는 단일 session만
  허용한다는 점이 명확한가?
- node rid 중복 필드가 join result와 lifecycle info에 없는가?
- enum과 flag 추가 없음이 명확한가?
- 오류 의미 표가 새 API와 변경 API를 모두 포함하는가?
- 회귀 테스트 항목이 API 변경과 state transition을 빠짐없이 덮는가?
- 문서 반영 계획이 spec, guide, internals 역할 구분에 맞는가?
- 금지 표현이 없는가?
- 오래된 signature나 이전 결론이 남아 있지 않은가?

## POSD 기반 리팩토링 단계

구현이 끝난 뒤 마지막 단계로 `doc/principal/software-design-principles.md`를 기준으로
전체 코드를 리뷰하고, POSD 관점의 리팩토링 요소가 남지 않을 때까지 반복한다.

이 단계는 단순한 스타일 정리가 아니다. 목표는 구현된 Actor 위치 생명주기 변경이
복잡성을 줄이는 방향으로 자리 잡았는지 확인하는 것이다. 특히 public API가 caller에게
불필요한 순서, 내부 자료구조, transport 세부 사항, remote/local 분기를 떠넘기지 않는지
검토한다.

### 리팩토링 절차

아래 절차를 POSD 리팩토링 요소가 더 이상 발견되지 않을 때까지 반복한다.

1. `doc/principal/software-design-principles.md`를 읽고 판단 기준을 다시 확인한다.
2. 이번 기능 구현으로 변경된 파일과 그 호출 경로를 모두 찾는다.
3. 변경된 public API, 내부 helper, state machine, callback dispatch, session mapping,
   route update, timeout handling, test code를 코드 리뷰한다.
4. 아래 위험 신호를 파일과 line 기준으로 목록화한다.
5. 각 위험 신호마다 어떤 POSD 원칙에 어긋나는지 적는다.
6. 각 위험 신호마다 두 가지 이상 수정 방향을 검토한다.
7. caller 관점의 인터페이스 복잡성이 가장 낮고 정보 은닉이 가장 잘 되는 방향을 선택한다.
8. 선택한 리팩토링을 구현한다.
9. 관련 테스트를 실행한다.
10. 다시 전체 변경 범위를 리뷰한다.

### POSD 리뷰 체크리스트

- public API가 remote/local 차이를 caller에게 떠넘기지 않는가?
- Actor 위치, session attach, Discovery route, lifecycle callback 책임이 서로 섞이지
  않는가?
- 같은 지식이 header, runtime state, protocol encoding, binding wrapper에 중복되어
  있지 않은가?
- request owner, Actor owner, stream owner 개념이 하나의 내부 모듈에서 일관되게
  관리되는가?
- caller가 내부 control path나 routing helper를 알아야만 올바르게 사용할 수 있는 API가
  없는가?
- `node`, `stream`, `session_rid`, `actor_ref` 같은 인자가 단순 pass-through로 여러
  계층을 떠돌지 않는가?
- special case가 API에 새 인자나 새 enum 값으로 새어 나오지 않았는가?
- timeout, close, disconnect, cancel 처리가 여러 곳에 흩어진 시간적 분해가 아닌가?
- lifecycle callback scheduling과 join completion 순서가 내부에서 흡수되고 caller에게
  불필요한 sequencing 부담을 주지 않는가?
- session Actor mapping 갱신과 Actor ref generation 검증이 하나의 책임 있는 모듈에
  모여 있는가?
- remote handoff rollback, stale ref 처리, pending join cleanup이 중복 구현되지 않았는가?
- helper 함수가 의미 있는 추상화를 제공하지 않고 인자 전달만 하는 얕은 모듈로 남아
  있지 않은가?
- callback 안 재진입 제한 같은 제약이 구현으로 안전하게 강제되고, caller 문서에만
  의존하지 않는가?
- 새 테스트가 구현 세부 순서에 과하게 결합되어 리팩토링을 어렵게 만들지 않는가?

### POSD 리팩토링 완료 기준

- 발견된 모든 위험 신호가 파일과 line 기준으로 정리되었다.
- 각 위험 신호에 대해 두 가지 이상 대안을 검토했다.
- 선택한 대안이 caller 인터페이스를 더 단순하게 만들거나 내부 정보 은닉을 강화한다.
- 리팩토링 뒤 같은 체크리스트를 다시 돌렸을 때 새 위험 신호가 나오지 않는다.
- 관련 테스트가 통과한다.
- 남은 리스크가 있다면 코드 문제가 아니라 명확히 후속 과제로 분리되어 있다.

## 검증 명령

아래 명령은 마지막에 실행한다.

```bash
rg -n "language.exchange|문서.?작성|zlink_request_result_t zlink_remote_actor_get_ref|zlink_request_result_t zlink_spot_node_actor_leave_spot|zlink_request_result_t zlink_spot_node_actor_destroy|zlink_request_result_t zlink_stream_bind_actor|zlink_request_result_t zlink_stream_unbind_actor|out == NULL|nonblocking lookup" doc/spec/draft/spot-actor-location-lifecycle.ko.md
```

의도하지 않은 결과가 없어야 한다.

아래 명령으로 draft와 plan 문서에 금지 표현이 직접 남아 있지 않은지 확인한다.

```bash
rg -n "language""-""exchange|문서""작성" doc/spec/draft/spot-actor-location-lifecycle.ko.md doc/spec/draft/spot-actor-location-lifecycle-implementation-plan.ko.md
```

검색 결과가 없어야 한다.

```bash
git diff --check -- doc/spec/draft/spot-actor-location-lifecycle.ko.md doc/spec/draft/spot-actor-location-lifecycle-implementation-plan.ko.md
```

통과해야 한다.

아래 명령으로 제거 대상이 유지 계약처럼 남아 있지 않은지 확인한다.

```bash
rg -n "zlink_spot_node_create_remote_actor|zlink_spot_node_actor_admission_handler|zlink_actor_create_result_t|zlink_actor_admission" doc/spec/draft/spot-actor-location-lifecycle.ko.md
```

검색 결과는 제거 대상 설명 안에만 있어야 한다.

## 완료 기준

- 체크리스트를 모두 통과한다.
- 검증 명령이 통과한다.
- 대상 draft 문서 안에 구현자가 임의로 판단해야 하는 공개 API 계약 공백이 남아 있지
  않다.
- 새 계약이 정식 spec, guide, internals 문서에 섞여 들어가지 않았다.
- 구현 후 POSD 기반 리팩토링 단계를 반복 수행했고, 더 이상 리팩토링할 위험 신호가
  남아 있지 않다.
