# SPOT Actor Dispatch Contract Matrix

이 matrix는 `doc/spec/draft/spot-actor-dispatch.ko.md`의 첫 구현 범위가 구현, 테스트,
문서, binding 반영 단계에서 빠지지 않도록 추적하기 위한 작업 문서다.

상태 값은 `planned`, `implemented`, `tested`, `documented`, `reviewed`만 사용한다.
`existing-reference` 행은 새 구현 대상이 아니며, draft에서 비교나 예시로 언급된 기존
공개 표면을 누락으로 오인하지 않기 위해 둔다.

## Contract Rows

| Contract ID | Contract Kind | Draft Link | Contract Text | Public API / Enum / Struct | Implementation Owner | Test ID | Binding Impact | Doc Impact | Status |
|---|---|---|---|---|---|---|---|---|---|
| ACTOR-OPT-001 | new-option | [상수와 구조체](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#상수와-구조체) | Actor id capacity와 Discovery actor route sync option을 추가한다. Actor HWM option은 만들지 않는다. | `ZLINK_ACTOR_ID_MAX`, `ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC` | `core/include`, `core/src/services/discovery` | ACT-DISC-01 | all | spec, bindings | reviewed |
| ACTOR-TYPE-001 | new-type | [상수와 구조체](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#상수와-구조체) | Actor ref, recv info, join info, create result, actor route 타입을 공개한다. `generation == 0`은 unchecked ref이다. | `zlink_actor_ref_t`, `zlink_actor_recv_info_t`, `zlink_actor_join_info_t`, `zlink_actor_create_result_t`, `zlink_actor_route_t` | `core/include/zlink.h` | ACT-REMOTE-12 | all | spec, bindings | reviewed |
| ACTOR-TYPE-002 | new-type | [모니터링과 snapshot](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#모니터링과-snapshot) | SpotNode Spot/Actor snapshot row 타입을 공개한다. snapshot 값은 진단용이며 flow control 계약이 아니다. | `zlink_spot_node_spot_entry_t`, `zlink_spot_node_actor_entry_t` | `core/include/zlink.h`, `core/src/api/service_spot_node_api.cpp` | ACT-SNAPSHOT-01 | all | spec, internals, bindings | reviewed |
| ACTOR-ENUM-001 | new-enum | [상수와 구조체](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#상수와-구조체) | remote create-or-get 성공 결과와 admission 판단 enum을 추가한다. | `zlink_actor_create_status_t`, `ZLINK_ACTOR_CREATE_CREATED`, `ZLINK_ACTOR_CREATE_EXISTING`, `zlink_actor_admission_result_t`, `ZLINK_ACTOR_ADMISSION_ACCEPT`, `ZLINK_ACTOR_ADMISSION_REJECT` | `core/include/zlink.h` | ACT-REMOTE-01 | all | spec, bindings | reviewed |
| ACTOR-ENUM-002 | new-enum | [Dispatch enum 확장](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#dispatch-enum-확장) | Spot dispatch event와 subject enum에 Actor readable, Actor join readable, Actor subject를 추가한다. | `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE`, `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE`, `ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR` | `core/include/zlink.h`, `core/src/api/service_spot_dispatch_context.cpp` | ACT-JOIN-01, ACT-QUEUE-04 | all | spec, internals, bindings | reviewed |
| ACTOR-ENUM-003 | new-enum | [오류 의미](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#오류-의미) | Actor request result code를 추가하고 기존 timeout/not-found 의미를 유지한다. | `ZLINK_REQUEST_REJECTED`, `ZLINK_REQUEST_CONFLICT`, `ZLINK_REQUEST_BUSY`, `ZLINK_REQUEST_NOT_CONNECTED`, `ZLINK_REQUEST_INVALID_ARGUMENT`, `ZLINK_REQUEST_INVALID_STATE`, `ZLINK_REQUEST_NOT_SUPPORTED`, `ZLINK_REQUEST_TIMED_OUT`, `ZLINK_REQUEST_NOT_FOUND` | `core/include/zlink_errno.h`, `core/src/api/*result*` | ACT-OWN-01 | all | spec, bindings | reviewed |
| ACTOR-API-001 | new-api | [Actor 생성과 종료](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor-생성과-종료) | local Actor lifecycle을 SpotNode가 소유한다. 같은 node 안 중복 id는 거부하고 다른 node 중복 id는 허용한다. joined Actor destroy는 실패한다. | `zlink_spot_node_actor_new()`, `zlink_actor_destroy()` | `core/src/api/service_spot_actor_api.cpp`, `core/src/services/spot` | ACT-LIFE-01, ACT-LIFE-02, ACT-LIFE-04, ACT-LIFE-05 | all | spec, guide, internals, bindings | reviewed |
| ACTOR-API-002 | new-api | [Actor ref 조회](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor-ref-조회), [Remote Actor ref](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#remote-actor-ref) | local lookup은 checked ref를 반환하고 remote ref 생성은 network 확인 없이 unchecked ref를 만든다. | `zlink_actor_get_ref()`, `zlink_spot_node_actor_lookup()`, `zlink_remote_actor_get_ref()` | `core/src/api/service_spot_actor_api.cpp` | ACT-LIFE-03, ACT-REMOTE-12, ACT-REMOTE-13 | all | spec, guide, bindings | reviewed |
| ACTOR-API-003 | new-api | [Remote Actor create-or-get](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#remote-actor-create-or-get), [Admission handler](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#admission-handler) | remote create-or-get은 Actor가 없을 때만 admission handler를 호출한다. 이미 있으면 existing 결과를 반환한다. | `zlink_spot_node_create_remote_actor()`, `zlink_spot_node_actor_admission_handler()`, `zlink_actor_admission_handler_fn` | `core/src/api/service_spot_actor_api.cpp`, `core/src/services/spot` | ACT-REMOTE-01..ACT-REMOTE-08, ACT-REMOTE-16 | all | spec, guide, internals, bindings | reviewed |
| ACTOR-API-004 | new-api | [Remote Actor 종료](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#remote-actor-종료) | remote Actor destroy는 idempotent이고 checked generation mismatch를 conflict로 처리한다. | `zlink_spot_node_destroy_remote_actor()` | `core/src/api/service_spot_actor_api.cpp` | ACT-REMOTE-09..ACT-REMOTE-11, ACT-REMOTE-14, ACT-REMOTE-15, ACT-REMOTE-17, ACT-REMOTE-18 | all | spec, internals, bindings | reviewed |
| ACTOR-API-005 | new-api | [Actor와 Spot join request](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor와-spot-join-request) | Actor join은 dispatch context로 전달되는 request이며 message와 accept/reject reply message를 전달한다. 한 Actor는 하나의 Spot에만 join할 수 있다. | `zlink_actor_join_spot()`, `zlink_spot_node_actor_join_spot()`, `zlink_spot_actor_join_recv()`, `zlink_spot_actor_join_reply()` | `core/src/api/service_spot_actor_api.cpp`, `core/src/api/service_spot_dispatch_context.cpp` | ACT-JOIN-01..ACT-JOIN-27 | all | spec, guide, internals, bindings | reviewed |
| ACTOR-API-006 | new-api | [Actor와 Spot leave](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor와-spot-leave) | leave는 join 관계만 해제한다. queue는 비우지 않고 leave/rejoin 사이 FIFO를 보존한다. | `zlink_actor_leave_spot()`, `zlink_spot_node_actor_leave_spot()` | `core/src/api/service_spot_actor_api.cpp` | ACT-JOIN-11, ACT-JOIN-17, ACT-JOIN-18, ACT-JOIN-22, ACT-JOIN-25, ACT-JOIN-28 | all | spec, guide, internals, bindings | reviewed |
| ACTOR-API-007 | new-api | [STREAM session Actor list bind](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#stream-session-actor-list-bind) | session별 Actor list를 저장한다. 한 session은 여러 Actor를 bind할 수 있고 한 Actor는 하나의 STREAM session에만 bind된다. | `zlink_stream_bind_actor()`, `zlink_stream_unbind_actor()` | `core/src/api/service_spot_actor_api.cpp`, `core/src/services/spot` | ACT-STREAM-01..ACT-STREAM-20 | all | spec, guide, internals, bindings | reviewed |
| ACTOR-API-008 | new-api | [STREAM에서 Actor로 relay](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#stream에서-actor로-relay) | STREAM session Actor list에서 `actor_id`로 대상 Actor를 골라 part를 relay한다. multipart target은 final까지 고정된다. | `zlink_stream_send_bound_actor_part()` | `core/src/api/service_spot_actor_api.cpp`, `core/src/services/spot` | ACT-QUEUE-01..ACT-QUEUE-07, ACT-QUEUE-13..ACT-QUEUE-17 | all | spec, guide, internals, bindings | reviewed |
| ACTOR-API-009 | new-api | [Actor에서 bound session으로 전송](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor에서-bound-session으로-전송) | Actor에서 bound STREAM session으로 raw message 또는 header/body packet을 전송한다. packet send는 부분 성공을 만들지 않는다. | `zlink_actor_send_bound_session_msg()`, `zlink_actor_send_bound_session_packet()` | `core/src/api/service_spot_actor_api.cpp` | ACT-QUEUE-08..ACT-QUEUE-12, ACT-OWN-05 | all | spec, guide, internals, bindings | reviewed |
| ACTOR-API-010 | new-api | [Actor queue 수신](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor-queue-수신) | Actor readable event subject handle에서 nonblocking part recv를 지원한다. callback 밖 또는 blocking recv는 첫 구현에서 지원하지 않는다. | `zlink_actor_recv_part()` | `core/src/api/service_spot_actor_api.cpp` | ACT-QUEUE-04, ACT-QUEUE-05, ACT-QUEUE-16, ACT-OWN-03 | all | spec, guide, bindings | reviewed |
| ACTOR-API-011 | new-api | [Actor active route 조회](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor-active-route-조회) | Discovery는 live Actor 목록이 아니라 bind 성공 시 publish된 active route 하나를 반환한다. | `zlink_discovery_resolve_actor()` | `core/src/api/service_discovery_api.cpp`, `core/src/services/discovery` | ACT-DISC-01..ACT-DISC-15 | all | spec, guide, internals, bindings | reviewed |
| ACTOR-API-012 | new-api | [모니터링과 snapshot](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#모니터링과-snapshot) | local Spot, Spot joined Actor, local Actor snapshot을 in/out count 패턴으로 제공한다. | `zlink_spot_node_spots_snapshot()`, `zlink_spot_actors_snapshot()`, `zlink_spot_node_actors_snapshot()` | `core/src/api/service_spot_node_api.cpp` | ACT-SNAPSHOT-01..ACT-SNAPSHOT-10 | all | spec, guide, internals, bindings | reviewed |
| ACTOR-CHANGE-001 | changed-api | [Dispatch enum 확장](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#dispatch-enum-확장) | `zlink_spot_dispatch_info_t.subject`는 Actor readable event에서 recv 대상 local Actor handle이어야 한다. | `zlink_spot_dispatch_info_t`, `zlink_spot_dispatch_event_t`, `zlink_spot_dispatch_subject_kind_t` | `core/include/zlink.h`, `core/src/api/service_spot_dispatch_context.cpp` | ACT-JOIN-02, ACT-QUEUE-04 | all | spec, internals, bindings | reviewed |
| ACTOR-BEH-001 | behavior | [설계 원칙](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#설계-원칙) | Actor lifecycle, generation, join 상태, session mapping, active route publish 시점은 SpotNode/Actor owner 책임으로 숨긴다. | `N/A` | `core/src/services/spot` | ACT-LIFE-01, ACT-DISC-04 | none | internals | reviewed |
| ACTOR-BEH-002 | behavior | [Actor active route 조회](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor-active-route-조회) | Actor 생성, remote create, join만으로 route를 publish하지 않는다. STREAM bind 성공 시점에 publish하고 destroy/provider cleanup 시 matching route만 제거한다. | `ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC`, `zlink_discovery_resolve_actor()` | `core/src/services/discovery`, `core/src/services/spot` | ACT-DISC-02..ACT-DISC-10, ACT-STREAM-09, ACT-STREAM-10 | all | spec, internals, bindings | reviewed |
| ACTOR-BEH-003 | behavior | [STREAM session Actor list bind](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#stream-session-actor-list-bind) | session owner는 `session -> actor_id -> Actor ref`만 저장하고 joined Spot 정보는 저장하지 않는다. | `zlink_stream_bind_actor()`, `zlink_stream_unbind_actor()` | `core/src/services/spot` | ACT-STREAM-12 | all | internals, bindings | reviewed |
| ACTOR-BEH-004 | behavior | [Backpressure](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#backpressure) | Actor 전용 HWM option 없이 기존 relay/dispatch 자원 상태로 backpressure를 반환한다. | `ZLINK_SUBMIT_BACKPRESSURED` | `core/src/services/spot` | ACT-QUEUE-06 | all | spec, internals, bindings | reviewed |
| ACTOR-BEH-005 | behavior | [동시성과 callback 제한](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#동시성과-callback-제한) | Actor event는 Spot dispatch context에서 직렬화되고 callback 안 같은 Actor destroy는 지원하지 않는다. | `zlink_spot_dispatch_event_handler()`, `zlink_actor_recv_part()` | `core/src/api/service_spot_dispatch_context.cpp` | ACT-OWN-06 | all | spec, guide, bindings | reviewed |
| ACTOR-OWN-001 | ownership | [소유권 규칙](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#소유권-규칙) | send 성공은 message 소유권을 이전하고 실패는 호출자에게 남긴다. recv 성공은 호출자에게 소유권을 이전한다. | `zlink_stream_send_bound_actor_part()`, `zlink_actor_recv_part()`, `zlink_spot_actor_join_recv()`, `zlink_spot_actor_join_reply()` | `core/src/api` | ACT-OWN-01, ACT-OWN-02, ACT-OWN-03 | all | spec, bindings | reviewed |
| ACTOR-OWN-002 | ownership | [Actor와 Spot join request](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor와-spot-join-request) | join request opaque handle은 pending 동안만 유효하고 reply에 정확히 한 번 사용한다. late reply는 invalid-state로 실패한다. | `zlink_actor_join_info_t`, `zlink_spot_actor_join_reply()` | `core/src/api/service_spot_actor_api.cpp` | ACT-JOIN-04, ACT-OWN-04 | all | spec, bindings | reviewed |
| ACTOR-TIMEOUT-001 | timeout | [Actor 생성과 종료](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor-생성과-종료), [Remote Actor 종료](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#remote-actor-종료) | destroy timeout은 Actor slot, joined 상태, bound session 상태를 호출 전 상태로 유지한다. | `zlink_actor_destroy()`, `zlink_spot_node_destroy_remote_actor()` | `core/src/api/service_spot_actor_api.cpp` | ACT-LIFE-11, ACT-REMOTE-15 | all | spec, bindings | reviewed |
| ACTOR-TIMEOUT-002 | timeout | [Actor와 Spot join request](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor와-spot-join-request), [Actor와 Spot leave](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor와-spot-leave) | join/leave timeout은 joined 상태를 호출 전 상태로 유지한다. | `zlink_actor_join_spot()`, `zlink_spot_node_actor_join_spot()`, `zlink_spot_node_actor_leave_spot()` | `core/src/api/service_spot_actor_api.cpp` | ACT-JOIN-12, ACT-JOIN-21, ACT-JOIN-22 | all | spec, bindings | reviewed |
| ACTOR-TIMEOUT-003 | timeout | [STREAM session Actor list bind](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#stream-session-actor-list-bind) | bind/unbind timeout은 session Actor list와 Actor bound session ref를 호출 전 상태로 유지한다. | `zlink_stream_bind_actor()`, `zlink_stream_unbind_actor()` | `core/src/api/service_spot_actor_api.cpp` | ACT-STREAM-17, ACT-STREAM-18 | all | spec, bindings | reviewed |
| ACTOR-REMOVE-001 | removed-api | [Generic discovery route 제거 계획](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#generic-discovery-route-제거-계획) | generic discovery route 공개 API를 호환성 유예 없이 제거하고 sample/binding은 actor/spot 전용 API로 정리한다. | `zlink_discovery_bind_route()`, `zlink_discovery_unbind_route()`, `zlink_discovery_resolve_route()`, `zlink_route_kind_t` | `core/include/zlink.h`, `core/src/api/service_discovery_api.cpp`, `bindings`, `samples` | ACT-ROUTE-01, ACT-ROUTE-02, ACT-ROUTE-03 | all | spec, guide, internals, bindings, sample | reviewed |
| ACTOR-NONGOAL-001 | non-goal | [비목표](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#비목표), [첫 구현 제외 항목](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#첫-구현-제외-항목) | Actor class/factory/DI, typed handler, codec registry, placement, migration, global registry product, client protocol meaning, Actor RPC retry, session auth, metadata store, domain key registry, Actor RPC는 구현하지 않는다. | `N/A` | `none` | N/A | all | guide, bindings | reviewed |
| ACTOR-EXIST-001 | existing-reference | [C API 변경 목록](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#c-api-변경-목록) | draft 예시나 비교 설명에서 언급된 기존 message, Spot, STREAM, recv/send/result 상수는 새 구현 대상이 아니다. | `zlink_msg_init()`, `zlink_msg_close()`, `zlink_spot_destroy()`, `zlink_spot_dispatch_event_handler()`, `zlink_stream_packet_handler()`, `ZLINK_DONTWAIT`, `ZLINK_PART_MORE`, `ZLINK_PART_FINAL`, `ZLINK_RECV_OK`, `ZLINK_RECV_NO_DATA`, `ZLINK_RECV_BUSY`, `ZLINK_RECV_NOT_SUPPORTED`, `ZLINK_SUBMIT_OK`, `ZLINK_SUBMIT_BACKPRESSURED`, `ZLINK_SUBMIT_INVALID_ARGUMENT`, `ZLINK_SUBMIT_INVALID_STATE`, `ZLINK_SUBMIT_NOT_CONNECTED`, `ZLINK_SUBMIT_NOT_FOUND`, `ZLINK_REQUEST_OK`, `ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC`, `ZLINK_EXPORT` | existing | N/A | none | none | reviewed |
| ACTOR-EXIST-002 | existing-reference | [Actor active route 조회](../../../../doc/spec/draft/spot-actor-dispatch.ko.md#actor-active-route-조회) | 기존 Spot owner lookup은 유지한다. | `zlink_discovery_resolve_spot()` | existing | ACT-DISC-11 | all | spec, guide | reviewed |

## Symbol Manifest

이 절은 plan의 `comm` 검증에서 잡히는 draft symbol을 모두 포함한다. 괄호가 붙은
API 이름은 검증 명령의 정규식과 맞추기 위한 것이다.

### zlink API Tokens

```text
zlink_actor_destroy(
zlink_actor_get_ref(
zlink_actor_join_spot(
zlink_actor_leave_spot(
zlink_actor_recv_part(
zlink_actor_send_bound_session_msg(
zlink_actor_send_bound_session_packet(
zlink_discovery_bind_route(
zlink_discovery_resolve_actor(
zlink_discovery_resolve_route(
zlink_discovery_resolve_spot(
zlink_discovery_unbind_route(
zlink_msg_close(
zlink_msg_init(
zlink_remote_actor_get_ref(
zlink_spot_actor_join_recv(
zlink_spot_actor_join_reply(
zlink_spot_actors_snapshot(
zlink_spot_destroy(
zlink_spot_dispatch_event_handler(
zlink_spot_node_actor_admission_handler(
zlink_spot_node_actor_join_spot(
zlink_spot_node_actor_leave_spot(
zlink_spot_node_actor_lookup(
zlink_spot_node_actor_new(
zlink_spot_node_actors_snapshot(
zlink_spot_node_create_remote_actor(
zlink_spot_node_destroy_remote_actor(
zlink_spot_node_spots_snapshot(
zlink_stream_bind_actor(
zlink_stream_packet_handler(
zlink_stream_send_bound_actor_part(
zlink_stream_unbind_actor(
```

### ZLINK Tokens

```text
ZLINK_ACTOR_ADMISSION_ACCEPT
ZLINK_ACTOR_ADMISSION_REJECT
ZLINK_ACTOR_CREATE_CREATED
ZLINK_ACTOR_CREATE_EXISTING
ZLINK_ACTOR_ID_MAX
ZLINK_DONTWAIT
ZLINK_EXPORT
ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC
ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC
ZLINK_PART_FINAL
ZLINK_PART_MORE
ZLINK_RECV_BUSY
ZLINK_RECV_NOT_SUPPORTED
ZLINK_RECV_NO_DATA
ZLINK_RECV_OK
ZLINK_REQUEST_BUSY
ZLINK_REQUEST_CONFLICT
ZLINK_REQUEST_INVALID_ARGUMENT
ZLINK_REQUEST_INVALID_STATE
ZLINK_REQUEST_NOT_CONNECTED
ZLINK_REQUEST_NOT_FOUND
ZLINK_REQUEST_NOT_SUPPORTED
ZLINK_REQUEST_OK
ZLINK_REQUEST_REJECTED
ZLINK_REQUEST_TIMED_OUT
ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE
ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE
ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE
ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE
ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE
ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE
ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR
ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER
ZLINK_SPOT_DISPATCH_SUBJECT_SPOT
ZLINK_SPOT_DISPATCH_SUBJECT_TIMER
ZLINK_SUBMIT_BACKPRESSURED
ZLINK_SUBMIT_INVALID_ARGUMENT
ZLINK_SUBMIT_INVALID_STATE
ZLINK_SUBMIT_NOT_CONNECTED
ZLINK_SUBMIT_NOT_FOUND
ZLINK_SUBMIT_OK
```

## Test Manifest

```text
ACT-DISC-01 ACT-DISC-02 ACT-DISC-03 ACT-DISC-04 ACT-DISC-05
ACT-DISC-06 ACT-DISC-07 ACT-DISC-08 ACT-DISC-09 ACT-DISC-10
ACT-DISC-11 ACT-DISC-12 ACT-DISC-13 ACT-DISC-14 ACT-DISC-15
ACT-JOIN-01 ACT-JOIN-02 ACT-JOIN-03 ACT-JOIN-04 ACT-JOIN-05
ACT-JOIN-06 ACT-JOIN-07 ACT-JOIN-08 ACT-JOIN-09 ACT-JOIN-10
ACT-JOIN-11 ACT-JOIN-12 ACT-JOIN-13 ACT-JOIN-14 ACT-JOIN-15
ACT-JOIN-16 ACT-JOIN-17 ACT-JOIN-18 ACT-JOIN-19 ACT-JOIN-20
ACT-JOIN-21 ACT-JOIN-22 ACT-JOIN-23 ACT-JOIN-24 ACT-JOIN-25
ACT-JOIN-26 ACT-JOIN-27 ACT-JOIN-28
ACT-LIFE-01 ACT-LIFE-02 ACT-LIFE-03 ACT-LIFE-04 ACT-LIFE-05
ACT-LIFE-06 ACT-LIFE-07 ACT-LIFE-08 ACT-LIFE-09 ACT-LIFE-10
ACT-LIFE-11
ACT-OWN-01 ACT-OWN-02 ACT-OWN-03 ACT-OWN-04 ACT-OWN-05 ACT-OWN-06
ACT-QUEUE-01 ACT-QUEUE-02 ACT-QUEUE-03 ACT-QUEUE-04 ACT-QUEUE-05
ACT-QUEUE-06 ACT-QUEUE-07 ACT-QUEUE-08 ACT-QUEUE-09 ACT-QUEUE-10
ACT-QUEUE-11 ACT-QUEUE-12 ACT-QUEUE-13 ACT-QUEUE-14 ACT-QUEUE-15
ACT-QUEUE-16 ACT-QUEUE-17
ACT-REMOTE-01 ACT-REMOTE-02 ACT-REMOTE-03 ACT-REMOTE-04 ACT-REMOTE-05
ACT-REMOTE-06 ACT-REMOTE-07 ACT-REMOTE-08 ACT-REMOTE-09 ACT-REMOTE-10
ACT-REMOTE-11 ACT-REMOTE-12 ACT-REMOTE-13 ACT-REMOTE-14 ACT-REMOTE-15
ACT-REMOTE-16 ACT-REMOTE-17 ACT-REMOTE-18
ACT-ROUTE-01 ACT-ROUTE-02 ACT-ROUTE-03
ACT-SNAPSHOT-01 ACT-SNAPSHOT-02 ACT-SNAPSHOT-03 ACT-SNAPSHOT-04
ACT-SNAPSHOT-05 ACT-SNAPSHOT-06 ACT-SNAPSHOT-07 ACT-SNAPSHOT-08
ACT-SNAPSHOT-09 ACT-SNAPSHOT-10
ACT-STREAM-01 ACT-STREAM-02 ACT-STREAM-03 ACT-STREAM-04 ACT-STREAM-05
ACT-STREAM-06 ACT-STREAM-07 ACT-STREAM-08 ACT-STREAM-09 ACT-STREAM-10
ACT-STREAM-11 ACT-STREAM-12 ACT-STREAM-13 ACT-STREAM-14 ACT-STREAM-15
ACT-STREAM-16 ACT-STREAM-17 ACT-STREAM-18 ACT-STREAM-19 ACT-STREAM-20
```

## Non-Goal Manifest

- Actor class 생성, factory, dependency injection
- typed message handler 등록
- message codec registry
- Actor placement 정책
- core 자동 Actor migration
- 전역 Actor registry 제품화
- client protocol header/body 의미
- request/reply sequence 해석
- Actor RPC 수준 retry policy
- 언어별 framework Actor 객체 자동 생성
- session 인증 정책
- 임의 metadata 저장소
- domain key-value registry
- application key 범용 조회
- Actor RPC 계약

## Matrix Gate Result

- 미분류 항목: 없음
- 빈 matrix 행: 없음
- 구현 후 문서-코드 반복 리뷰 상태: 모든 행은 `reviewed`
