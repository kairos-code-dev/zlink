# S8 CPP bindings 리뷰 iteration-2 — R1 (Codex) progress

## Scope 확인
- 대상 commit: `de299e184` (working tree == 대상, HEAD `a9e6b521b`가 이를 포함).
- 시작 scope: 123 files, aggregate SHA-256 `c0cfcd3d7c45af4e7b089ef74dc83b5a8d02fcee41bc8242c6de2df785b73a12` (일치).
- 정적 소스 대조만. build/test/sanitizer/package 미실행. coordinator 실행 증거(lib+15 samples green, no-hit ZERO)만 사용.

## iteration-1 finding 재검증 (source 대조 @ de299e184)
- F1 borrowed parts: `with_borrowed_native_parts`/`submit_borrowed_message_array` 신설(native_message_parts.hpp:317-371, NULL free-fn view, caller 보존). 모든 RouteMesh service send/request/reply/create가 이걸 사용(mesh_node/spot/actor/stream_session/dispatch). move/consume는 raw socket 경로에만 잔존(request_reply.cpp raw_*, reply_operations.cpp router_reply, request_submitter). spec 01-mesh-node §361-363 "borrowed, caller retains ownership on every result" 확인. **RESOLVED**
- F2 claim/reply-token 수명: release()가 ZLINK_CLOSE_OK일 때만 _valid=false; recv_batch 성공이 claim 무효화 안 함(dispatch.cpp:39-79). **RESOLVED**
- F3 full-batch drain: sample_common.hpp mesh_drain_claim 전체 batch 순회·retain(276-281), claim 보유 중 캡처 후 release. **RESOLVED**
- F4 kind_data: receive_record_t.kind_data + typed optional(send_ready/join_completion/actor_control/transfer_control). decode 구조체/enum 값 Core ABI와 정확히 일치(dispatch.h/actor.h 대조). **RESOLVED**
- F5 metadata+publish detail: 모든 send/request/publish에 mesh_metadata_t; publish는 publish_detail_t* 반환. view/detail 구조체 Core 일치. **RESOLVED**
- F6 actor transfer fence: prepare 자유함수 + RAII actor_transfer_token_t(commit/activate/abort, 소멸 시 abort). Core prepare/commit/activate/abort와 매핑, token opaque[8] 일치. **RESOLVED**
- F7 options/peer_channels: node set/get(router_hwm_profile/router_hwm/mailbox_message/byte budget), peer_channels(two-call), publisher+spot NODROP get/set. 옵션 id·시그니처 Core 일치. **RESOLVED**
- F8 sample routing_id: mesh_start_single_node가 고유 rid set_routing_id 후 start. **RESOLVED**
- F9 ready-handler: unregister-first→state 교체→re-register, 실패 시 callable 리셋; trampoline catch(...)로 예외 흡수. stable heap 주소 userdata. **RESOLVED**
- F10 close/move: close()→close_result_t, busy 시 handle 보존; move-assign은 close()후 swap(overwrite 안 함). node/publisher/spot/session 전부. **RESOLVED**
- F11 actor-id 검증: empty/`>255` invalid_argument 거부(ZLINK_ACTOR_ID_MAX=255, actor_id[256]). **RESOLVED**
- I2-1 dispatch-turn: 근본원인(조기 무효화·record 유실)이 라이브러리 primitive에서 해소, 샘플 helper가 turn 시연. 별도 라이브러리 turn 타입 부재는 taste. **RESOLVED**
- I2-2 state 분리: enum이 raw_*/received_*로 축소, spot_node_t* 제거. **단 spot_command_t에 dead field 4개(spot/topic/channel_name/target) 잔존** — 아래 finding. **PARTIAL**
- I3-1 구 헤더/dead 식별자: 구 v9 C 헤더 6개 삭제, spot_node_option_id·monitor_target_kind_t::spot_node 제거. no-hit 8/8 통과. **RESOLVED**

## 신규 finding (fresh 3-axis)
- [I2/I3] spot_state.hpp spot_command_t: `spot_t* spot`, `topic`, `channel_name`, `target` 4개 필드가 reset() 외 read/write 0건(grep 확인). request_seq만 live(router.cpp:84 set, reply_operations.cpp:27 read). 제거된 service command의 잔재 dead state.

## no-hit gate (scope minus native)
SpotNode/spot_node/route_bridge/subjects/internal_sockets/dispatch_workers/recv_actor_part/msg_gets: 전량 0 (case-insensitive 포함). set_pub/sub_routing_id: 0.

## 최종 (fresh 3-axis + dead-code sweep 취합, 전량 독립 재검증)
- I1: CLEAN (계약 불일치 0; kind_data ABI·enum·수명·transfer·option 전수 대조).
- I2: NOT CLEAN — I2-2 잔재 dead field 2건: spot_command_t(spot/topic/channel_name/target) + routing_target_t second_rid trio(+미호출 helper 2종).
- I3: NOT CLEAN — 전환 commit 9139ca46e가 남긴 dead 선언/helper/member 3 finding:
  (1) routing_id.hpp:103-112 actor_*_operation_t forward-decl 10개 dangling,
  (2) 죽은 내부 helper 6종(invalidate_claim/make_spot_request_progress/move_assign_pimpl_and_close/native_request_timeout_ms/set_spot_spot_send_context/second-rid 2종),
  (3) spot_impl.hpp:17 dead member last_error.
  deprecated-concept no-hit gate는 8/8 통과.
- 참고(전환 무관 pre-existing dead): poller_item_t::timer, pub_option_id approve/reject_subscribe, Native/native_send.hpp 3종.
- 종료 scope: 123 files, SHA-256 c0cfcd3d…b73a12 (시작과 동일, 수정 0).
- 최종: BINDINGS REVIEW NOT CLEAN.
