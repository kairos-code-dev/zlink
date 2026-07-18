# S8 CPP bindings 전환 리뷰 — iteration 2 — R1 (Codex)

독립 리뷰어 R1. 정적 소스 대조만 수행했다. build·테스트·sanitizer·package는 실행하지 않았고,
실행 증거는 manifest §2의 coordinator 결과(라이브러리 + 15 samples compile+link green, no-hit ZERO)만
사용했다. R2(Claude Sonnet)의 iteration-2 산출물은 보지 않았다.

## 1. Scope 확인

- 대상 commit: `de299e184` (working tree == 대상; HEAD `a9e6b521b`가 이를 포함, scope 경로 수정 0건).
- 시작: 123 files, aggregate SHA-256 `c0cfcd3d7c45af4e7b089ef74dc83b5a8d02fcee41bc8242c6de2df785b73a12` (일치).
- 종료: 123 files, aggregate SHA-256 `c0cfcd3d7c45af4e7b089ef74dc83b5a8d02fcee41bc8242c6de2df785b73a12` (일치, 변경 없음).
- scope 필터는 `grep -v native/`(소문자)다. `bindings/cpp/src/Runtime/Native/`(대문자 N)는 이 필터에
  걸리지 않으므로 **scope에 포함**된다(10 files). 아래 검토는 이를 포함한다.

## 2. iteration-1 finding 해소 판정 (source 대조 @ de299e184)

Core 계약 근거: `core/include/zlink/service/{dispatch,actor,mesh_node}.h`,
`core/doc/spec/core/service/{01-mesh-node,02-dispatch,04-actor,05-stream-session}.md`.

| ID | 판정 | 근거 (핵심) |
|---|---|---|
| F1 borrowed parts | **RESOLVED** | `native_message_parts.hpp:317-371` `with_borrowed_native_parts`/`submit_borrowed_message_array` 신설 — NULL free-fn 비소유 view, caller 메시지 전 결과 보존. RouteMesh service send/request/reply/create(`mesh_node.cpp` 12곳, `spot.cpp` 5곳, `actor.cpp` 6곳, `stream_session.cpp` 2곳, `dispatch.cpp:216` reply) 전량이 이걸 사용. move/consume는 raw-socket 경로(`request_reply.cpp` raw_*, `reply_operations.cpp` router reply, `request_submitter.hpp`)에만 잔존 — raw 소켓은 consume가 정본. spec 01-mesh-node §361-363 "Input is borrowed and read-only, and the caller retains ownership on every result. On success, Core acquires required references before returning" 확인. |
| F2 claim/reply-token 수명 | **RESOLVED** | `dispatch.cpp:39-51` release()가 `ZLINK_CLOSE_OK`일 때만 `_valid=false`; 실패 시 재release 가능. `:53-79` 성공 recv_batch가 claim 무효화 안 함(reply token은 release 전까지 유효). token opaque[4]·claim opaque[4] ABI 일치. |
| F3 full-batch drain | **RESOLVED** | `sample_common.hpp:276-281` `mesh_drain_claim`가 batch 전체(`0..count`) 순회·retain, claim 보유 중 캡처 후 `:326` release. |
| F4 typed kind_data | **RESOLVED** | `dispatch.hpp:306-316` `receive_record_t.kind_data`(독립 복사) + typed optional. `dispatch_access.hpp:104-161` decode가 `zlink_mesh_send_ready_data_t`/`zlink_actor_transfer_control_t`/`zlink_actor_control_record_t`/`zlink_actor_join_completion_t` 및 record/operation/destination/lifecycle/join/role/phase enum 값을 Core ABI와 정확히 매핑(전 필드·전 상수 대조 완료). |
| F5 metadata+publish detail | **RESOLVED** | 모든 send/request/publish에 `mesh_metadata_t`(span) 인자, `make_metadata_view`가 빈 span→nullptr. publish는 `publish_detail_t*` 반환(`store_publish_detail`). `zlink_mesh_metadata_view_t`/`zlink_mesh_publish_detail_t` 필드 일치. |
| F6 actor transfer fence | **RESOLVED** | `actor.hpp:62-126` prepare 값객체 + RAII `actor_transfer_token_t`(commit/activate/abort, 소멸 시 best-effort abort). `actor.cpp:283-359`가 `zlink_mesh_node_actor_transfer_prepare/commit/activate/abort`와 매핑, token opaque[8] 일치. |
| F7 옵션/peer_channels | **RESOLVED** | node set/get(`router_hwm_profile/router_hwm/mailbox_message_budget/mailbox_byte_budget`, opt id 0x3620-0x3623), `peer_channels`(two-call probe, `char(*)[ZLINK_CHANNEL_NAME_MAX+1]` 시그니처 일치), publisher+spot `set_nodrop/nodrop`(`ZLINK_MESH_PUBLISH_OPT_NODROP` 0x3630). |
| F8 sample routing_id | **RESOLVED** | `sample_common.hpp:348-359` `mesh_start_single_node`가 고유 rid로 `set_routing_id` 후 start, status로 rid 반환. |
| F9 ready-handler 동시성·예외 | **RESOLVED** | `mesh_node.cpp:622-642` unregister-first → state 교체 → re-register, 실패 시 callable 리셋. userdata=heap 상주 `_impl->ready_handler` 안정 주소. trampoline `:72-81` `catch(...)`로 예외 흡수. |
| F10 close/move | **RESOLVED** | `close()`가 `close_result_t` 반환, busy 시 handle 보존(`mesh_node.cpp:173-180`). move-assign은 `close()` 후 `swap`(overwrite 안 함) — node/publisher/spot/session 4종 동일. |
| F11 actor-id 검증 | **RESOLVED** | `mesh_node.cpp:567-568` empty·`>255` → `std::invalid_argument`. `ZLINK_ACTOR_ID_MAX=255`, `actor_id[256]` → memcpy 안전. |
| I2-1 dispatch-turn | **RESOLVED** | 근본원인(조기 무효화·record 유실)이 라이브러리 primitive에서 해소(F2/F3), `mesh_drain_claim`/`mesh_pull_one`가 turn 소유·전 record/token 보존을 시연. 별도 라이브러리 turn 타입 부재는 taste로 판단 — finding 아님. |
| I2-2 state 분리 | **PARTIAL** | 죽은 enum variant·`spot_node_t*` 제거는 완료. **단 `spot_operation_state_t`가 여전히 dead field를 다수 보유**(아래 I2/I3 finding). |
| I3-1 구 헤더/dead 식별자 | **RESOLVED** | 구 v9 C 헤더 6개(zlink.h/common.h/zlink_enum.h/service/{actor,spot,common}.h) 삭제 확인. `spot_node_option_id` enum·`monitor_target_kind_t::spot_node` 제거. no-hit 8/8 통과(§5). `#include <zlink.h>`는 삭제된 binding 헤더가 아니라 Core 10.0.0 umbrella로 해소. |

## 3. Fresh 3-axis 재검토

### I1 — 계약 구현 일치

**Findings: 없음.**

Evidence: Core 10.0.0 service C API 표면 대비 C++ 매핑을 전수 대조했다. kind_data typed decode의
구조체 필드·enum 상수(record_kind 1-13, operation_kind 1-11, destination 1-5, owner 1-3,
lifecycle 1-5, join 0/1, transfer role 1/2·phase 1-5)가 Core header와 완전히 일치한다. borrowed vs
move/consume 이원화가 service(borrowed)와 raw-socket(consume) 경계와 정확히 대응한다. claim/reply
token 수명(release까지 유효), metadata view, publish detail, transfer fence(opaque[8]),
peer_channels(char[256] stride), option id, remote_actor_ref 검증 모두 정합. actor_transfer_token
소멸 시 abort, ready-handler 예외 경계도 계약 준수.

**Verdict: CLEAN**

### I2 — POSD·DDD

iteration-1 I2-2(“raw socket state와 service state 책임 분리, 미사용 variant/field/friend 제거”)가
**부분 해소**에 그쳤다. `spot_operation_state_t`는 여전히 raw-socket 경로 전용인데, 제거된 service
command에서 넘어온 미사용 field를 그대로 안고 있어 special-general 혼합과 죽은 상태가 남아 있다.

**Finding 1** `[I2][low] bindings/cpp/src/Runtime/Service/spot_state.hpp:99-102 — `spot_command_t`의
`spot_t *spot`, `topic`, `channel_name`, `target` 4개 field가 dead다 — 전 scope grep 결과 `.spot.` 접근은
`request_seq`(set `Runtime/Sockets/router.cpp:84`, read `reply_operations.cpp:27`)와 `reset()`
(`spot_state.hpp:267`)뿐이며, 위 4개 field는 어디서도 read/write되지 않는다(alias 바인딩도 없음). 제거된
publish/send_channel/send_to_spot/request_to_actor/bound_session_send command의 잔재로, 실제로는
raw ROUTER reply의 `request_seq` 하나만 운반한다 — `request_seq`만 raw reply 상태로 남기고 dead field 4개와
`spot_command_t`의 잉여 구조를 제거하라(구조체명 `spot_command_t`도 raw-router 의미와 어긋난다).`

**Finding 2** `[I2][low] bindings/cpp/src/Runtime/Service/spot_state.hpp:50,52,54 —
`routing_target_t`의 `second_rid`, `second_rid_native_cache`, `has_second_rid_native_cache`가 dead
island이다 — 이 세 field를 만지는 코드는 `reset()`과 두 helper `cache_second_rid_native`(:130-136),
`target_second_rid_native`(:156-164)뿐인데, 그 두 helper 자체가 전 scope에서 호출 0건(grep -w 각각 1건=정의뿐)이다.
즉 second-rid 경로 전체가 self-contained dead code다(이전 두-rid service routing의 잔재). → second_rid trio와
두 helper를 함께 제거하라.`

**Verdict: NOT CLEAN** (2 findings; 근본은 I2-2 잔재)

### I3 — 정리 완결성

폐기 개념 no-hit gate는 통과(§5)했다. 그러나 이번 전환 commit(`9139ca46e`, "resolve iteration-1 review
findings F1-F11/I2/I3"; 대상 `de299e184`)이 새로 쓴 service 코드 안에 **정의 없는 선언·호출 없는 helper·죽은
member**가 다수 남아 있어 정리가 완결되지 않았다. 아래 항목은 전 scope grep으로 확인했다(각 식별자 whole-scope
occurrence = 1 → 정의/선언만 존재, 사용 0).

전환 코드(commit 9139ca46e) 잔재 — 이번 리뷰의 finding:

**Finding 1** `[I3][low] bindings/cpp/include/zlink/Contracts/Core/routing_id.hpp:103-112 — service
operation-builder 타입 10개 forward-decl이 정의·사용 없이 dangling이다 — `actor_join_operation_t`,
`actor_join_submit_operation_t`, `actor_join_callback_submit_operation_t`,
`actor_join_entry_spot_operation_t`, `actor_join_reply_operation_t`, `actor_leave_operation_t`,
`actor_destroy_operation_t`, `actor_lookup_operation_t`, `actor_bind_operation_t`,
`actor_unbind_operation_t` — 각각 전 scope 등장 1회(이 forward-decl 자체)뿐. 같은 블록의
`send_operation_t`/`request_*`/`reply_*`는 15-35회 사용되는 live 타입이라 대비된다. actor 연산은
`actor_t` 메서드가 직접 `submit_result_t`를 반환하도록 구현되어 이 builder 타입들은 끝내 정의되지 않았다 →
10개 forward-decl 제거.`

**Finding 2** `[I3][low] 죽은 내부 helper 6종(정의만, 호출 0) —
`bindings/cpp/src/Runtime/Service/dispatch_access.hpp:54 invalidate_claim`,
`bindings/cpp/src/Runtime/Service/detail.hpp:89 make_spot_request_progress`,
`bindings/cpp/src/Runtime/Service/pimpl_move.hpp:25 move_assign_pimpl_and_close`,
`bindings/cpp/src/Runtime/Core/operation_detail.hpp:35 native_request_timeout_ms`,
`bindings/cpp/src/Runtime/Messaging/received_access.hpp:46 set_spot_spot_send_context`,
그리고 I2 Finding 2의 `cache_second_rid_native`/`target_second_rid_native` — 각 whole-scope grep -w 1건.
특히 `move_assign_pimpl_and_close`는 F10 move-assign이 수동 swap으로 구현되며 미사용, `invalidate_claim`은
F2 claim 수명 수정 후 미사용, `set_spot_spot_send_context`는 제거된 spot send 경로 잔재다 → 전부 제거.`

**Finding 3** `[I3][low] bindings/cpp/src/Runtime/Service/spot_impl.hpp:17 — `spot_t::impl`의
`int last_error`가 dead member다 — impl 접근은 전부 `_impl->handle`이며 이 `last_error`는 read/write 0건.
공개 오류 상태는 별개 member `spot_t::last_error_`(spot.hpp:84, spot.cpp:63에서 native로부터 설정)로
운반된다 → impl의 잉여 `last_error` 제거.`

전환과 무관하게 예전부터 있던 dead code(참고 — 이번 전환 잔재는 아니나 scope 내 죽은 code):

- `bindings/cpp/src/Runtime/Eventing/poller_socket_cache.hpp:23` `poller_item_t::timer`(write-only:
  `poller.cpp:205`에서 대입만, read 0; lookup은 별도 `timer_handle` 사용). 마지막 touch는 2026-07-12 perf
  commit로 S8 이전.
- `bindings/cpp/src/Runtime/Options/option_ids.hpp:77-78` `pub_option_id::approve_subscribe`/
  `reject_subscribe` enum 값(공개 메서드는 native 매크로를 직접 사용, enum 값 참조 0). S8 이전 socket-options.
- `bindings/cpp/src/Runtime/Native/native_send.hpp:19,41,63` 죽은 helper 3종(`send_single_no_wait_result`,
  `submit_single_message_part_restore`, `submit_single_message_part_no_wait_result`; 각 grep 1건).

**Verdict: NOT CLEAN** (전환 코드 잔재 3 finding; deprecated-concept no-hit는 통과)

## 4. 폐기 개념 no-hit 판정 (scope minus `native/`, case-insensitive 포함)

| 개념 | 판정 | 근거 |
|---|---|---|
| SpotNode / `spot_node` | **NO-HIT** | 0 files (대/소문자 모두) |
| `route_bridge` | **NO-HIT** | 0 files |
| `subjects` | **NO-HIT** | 0 files |
| `internal_sockets` | **NO-HIT** | 0 files |
| pub/sub 별도 rid (`set_pub_routing_id`/`set_sub_routing_id`) | **NO-HIT** | 0 files |
| `dispatch_workers` | **NO-HIT** | 0 files |
| `recv_actor_part` | **NO-HIT** | 0 files |
| `msg_gets` / `zlink_msg_gets` | **NO-HIT** | 0 files |

폐기 개념 no-hit gate 8/8 통과. 구 v9 vendor C 헤더 6개 삭제 확인.

## 5. 종합

- I1 계약: **CLEAN** (finding 0).
- I2 POSD·DDD: **NOT CLEAN** (I2-2 잔재 dead field 2건).
- I3 정리 완결성: **NOT CLEAN** (전환 코드 dead 선언/helper/member 3건; no-hit gate는 통과).

모든 계약·수명·동시성·자원 결함(F1-F11 계열)은 실제로 해소됐고 신규 계약 불일치는 없다. 남은 것은 전환 commit이
남긴 **죽은 code·정의 없는 선언**(저심각도이나 iteration-2 기준 "각 축 finding 0"에서 CLEAN을 막는 항목)이다.

BINDINGS REVIEW NOT CLEAN
