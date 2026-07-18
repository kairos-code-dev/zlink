# S8 CPP bindings 리뷰 iteration-1 — 병합 finding ledger

두 리뷰어(R1 Codex, R2 Sonnet) iteration-1 리뷰(snapshot `2f34aacf2`, 129파일 `e1adbf34`)를
병합. 둘 다 `BINDINGS REVIEW NOT CLEAN`. coordinator가 아래를 수정한 뒤 새 snapshot으로
iteration-2를 돌린다. Core 계약 근거는 각 리뷰의 Evidence 절 참조.

## I1 계약 구현 일치 (root-cause family로 묶음)

### F1. Service parts ownership — borrowed를 move/consume로 오전달 [blocker]
`native_message_parts.hpp:296` `submit_message_array`는 message_t를 zlink_msg_t로 move하고
실패 시에만 복원. Core service send/request/reply/create는 성공·실패 모두 **caller ownership 유지**
(spec 01-mesh-node §361-363, 02-dispatch §318-319, 04-actor §182-183, 05-stream-session §126).
소비처: `mesh_node.cpp:219`, `spot.cpp:111`, `actor.cpp:83`, `stream_session.cpp:191`,
`dispatch.cpp:210`. → service 전용 **borrowed-parts adapter**(const span/vector에서 유효 native
view 또는 명시 copy 구성, 모든 결과에서 caller message 보존) 신설.

### F2. claim 수명·reply token — 성공 recv 후 release 없이 무효화 [blocker]
`dispatch.cpp:70` ZLINK_RECV_OK이면 `_valid=false`로 바꿔 destructor·명시 release를 no-op화.
Core는 claim release가 남은 mailbox work를 rearm하고 reply token은 release 전까지 유효
(spec 02-dispatch §275-281, §323-329). → 성공 recv 후에도 claim valid 유지, 실제
`zlink_mesh_claim_release` 성공 시에만 무효화. record/reply 처리 기간 claim 소유하는 scoped
turn으로 token 수명까지 결속(F I2-1과 동일 family).

### F3. pull helper 다중 record 유실 [high]
`sample_common.hpp:268` receive_batch(32)인데 at(0)/retain_message(0)만 반환하고 batch 폐기 →
index 1..N-1 application record·completion 미관찰. → batch 전체 순회 보존 또는 실제 1-record
batch + residue/rearm 끝까지 처리.

### F4. receive_record_t kind_data 미노출 [high]
`dispatch.hpp:143`/`dispatch_access.hpp:74-98` — Core record의 kind_data(actor lifecycle/join
completion/transfer control/SEND_READY destination)를 복사 안 함. → versioned kind_data를 C++
typed variant/value로 매핑, batch 수명과 독립 값으로 복사.

### F5. outbound metadata·publish detail 미노출 [high]
`mesh_node.hpp:79` 등 — Core send·request·publish는 metadata view 인자를 받고 publish는
admission/drop detail 반환. C++은 항상 nullptr 전달(`mesh_node.cpp:221-223,557-559`,
`spot.cpp:113-115,190-192`, `actor.cpp:137-139`, `stream_session.cpp:193-195`). → 불변 byte
view 인자 추가 + publish detail value object 반환/output.

### F6. actor transfer fence API 전부 누락 [high]
`actor.hpp:31` — Core actor.h:214-226 prepare/commit/activate/abort + transfer token/result
모델 미노출. record_kind의 transfer_control 이름만 존재. → transfer value objects + RAII token
정의, 4상태 API + dispatch control record 노출. (또는 의도 축소면 설계·ledger에 명시 결정 기록.)

### F7. 옵션/쿼리 표면 미바인딩 [medium]
`mesh_node.hpp:106` — publisher set/get option(NODROP), MeshNode set/get option(mailbox
budget/HWM), `peer_channels`, spot publish option getter 미노출. → typed option method + peer
channel snapshot + Spot/publisher NODROP get/set 대칭.

### F8. 샘플 9개 routing_id 미설정 [high]
`sample_common.hpp:319` `mesh_start_single_node`가 set_bind→add_channel_name→start만, 
`set_routing_id` 미호출. Core start는 routing_id·bind·channel 모두 요구(01-mesh-node §164-172).
9개 spot/actor 샘플이 첫 start에서 uncaught `config_error_t`. → sample별 고유 rid 생성해
start 전 set_routing_id 호출, status로 검증.

### F9. set_ready_handler 동시성·예외 경계 [high]
`mesh_node.cpp:485` — 실행 중 callback이 있을 수 있는데 std::function 먼저 대입 후 Core 등록
변경, 실패 시 불일치, trampoline이 사용자 예외를 C 경계 밖으로 전파. → stable shared callback
state + 동기화, Core unregister 후 교체, trampoline 예외 포착→domain/error 정책 변환.

### F10. RAII close 실패 무시 → native 누수 [high]
`mesh_node.cpp:100` — Core는 child 남으면 destroy를 CLOSE_BUSY로 거부·포인터 유지. wrapper는
결과 버리고 impl 파괴/덮어써 누수. publisher/spot/session/batch 동일. → parent/child 수명을
shared control block으로 결속하거나 close 성공 전 ownership 유지, move assignment도 기존 resource
종료 실패 시 미덮어씀(F I2-3과 동일 family).

### F11. remote_actor_ref 255B 초과 무단 절단 [medium]
`mesh_node.cpp:425` — min(size,max) truncate. Core는 1..255 byte, 초과는 다른 값. → 빈 값·255
초과를 invalid_argument로 거부.

## I2 POSD·DDD

### I2-1. dispatch-turn deep abstraction 부재 [high] (F2/F3와 동일 family)
ready drain→claim release까지 소유하는 dispatch-turn 추상화 신설, 모든 record 보존·reply-token
유효성 강제.

### I2-2. spot_operation_state_t가 raw + 제거된 service command 혼합 [medium]
`spot_state.hpp:26` — submit switch는 raw_*·received_*만 쓰는데 enum/state에 publish/
send_channel/send_to_spot/request_to_actor/bound_session_send 등 죽은 variant + spot_node_t*
잔존. → raw socket state와 service state 책임 분리, 미사용 variant/field/friend 제거.

### I2-3. MeshNode aggregate가 child lifetime 미소유 [high] (F10과 동일 family)
공통 control block에서 parent/child ownership·close 상태 관리, 잘못된 파괴 순서를 API로 불가능화.

## I3 정리 완결성

### I3-1. 구 v9.0.4 C 헤더·dead spot_node 식별자 잔재 [high]
- 구 vendor C 헤더: `include/zlink.h`(v9.0.4, stale include), `include/zlink/common.h`(v9),
  `include/zlink/service/actor.h`(109줄 구버전), `service/common.h`(구버전), `service/spot.h`
  (455줄, SpotNode/route_bridge/pub bind·rid/subjects/internal_sockets/spot·actor 열거 전부),
  `include/zlink_enum.h`(spot_node_mode/socket_owner/option[dispatch_workers]/state).
- dead C++ 식별자(정의 없는 spot_node_t): `Contracts/Core/routing_id.hpp:95`,
  `Eventing/poller.hpp:20`, `Messaging/message.hpp:23,113`(friend), `Eventing/events.hpp:56-61`
  (`monitor_target_kind_t::spot_node`), `src/Runtime/Service/spot_state.hpp:22,50,138`
  (fwd-decl + `actor_command_t::node`), `src/Runtime/Options/option_ids.hpp:91-99`
  (`spot_node_option_id`, dispatch_workers).
- no-hit gate 5/7 HIT(SpotNode/spot_node·route_bridge·subjects·internal_sockets·dispatch_workers).
→ 구 vendor C 헤더는 Core 10.0.0과 단일 소스 동기화 또는 삭제(bindings는 core/include 직접 사용),
active C++의 죽은 spot_node forward/friend/state/option/event/variant 제거. no-hit 통과.

## 처리 방침
coordinator가 위를 수정(격리 구현). Core 계약 준수(ownership·claim/token 수명·metadata·transfer),
컴파일+링크 green, 폐기 no-hit 통과. 완료 후 새 snapshot으로 iteration-2 두 리뷰어 재검토.
F5(metadata)·F6(transfer)는 참조 lane(dotnet)과 표면 정합 필요 — dotnet 리뷰 결과와 교차 확인.
