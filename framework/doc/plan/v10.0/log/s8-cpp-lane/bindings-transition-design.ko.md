# S8-CPP bindings 전환 설계 (2026-07-18 조사 확정)

Core 10.0.0에서 SPOT-part push 모델 심볼이 전부 삭제되고 MeshNode+pull
dispatch(ready-index/claim/batch)+spot/actor/stream_session 모델로 재편됨.
cpp bindings Service 레이어(5549줄)를 이에 맞춰 재작성한다. Runtime/raw-socket
레이어는 유지(message property만 제거 완료).

## 파일별 분류

| 파일 | 분류 | 매핑 |
|---|---|---|
| spot_route_bridge.cpp | 삭제 | route_bridge Core API 전면 폐기. publisher는 mesh_node_publisher로 별도 |
| spot_node.cpp | MeshNode 재작성 | zlink_spot_node_* → zlink_mesh_node_*. subjects·internal_sockets·pub bind·pub/sub rid는 대응 없음→공개 API 축소 |
| spot.cpp | 재작성 | zlink_spot_new(mesh_node), recv→pull batch, reply→zlink_mesh_reply, option→publish/subscription 분리 |
| spot_send.cpp | 재작성 | *_part push → zlink_spot_send_to_spot/_channel/_publish 일괄+operation_id. router 직접 폐기 |
| spot_receive.cpp | 재작성 | subscribe_part/recv_event → drain_ready/claim/receive_batch. subscription은 zlink_spot_set/unset_subscription |
| actor.cpp | 재작성 | actor_new(+with_request)→zlink_mesh_node_actor_new(creation_parts), destroy/close_bound_session 매핑 |
| actor_operations.cpp | 재작성 | join_entry_spot/leave/destroy/lookup_remote/join_reply 매핑. bind/unbind→zlink_stream_session_* |
| request_reply.cpp | 재작성+부분 | spot_node_request_to_actor→mesh_node_request_to_actor. raw dealer/router는 생존 |
| reply_operations.cpp | 재작성/부분 | router_reply_spot_part→zlink_mesh_reply. raw router_reply_part 생존 |
| send_operations.cpp | 소폭 | Core 직접 호출 없음, 하위 submit 헬퍼 시그니처 조정 |
| operation_builder_base.hpp | 유지 | 순수 템플릿 |

## 공개 계약(include/Contracts/Service) 재설계
- spot_node.hpp: spot_node_t → mesh_node_t로 개념 전환. spot_route_bridge_t 삭제. spot_node_publisher_t→publisher(mesh_node_publisher 매핑)
- spot.hpp: spot_t(mesh_node&), friend 재구성. recv/dispatch pull batch 노출
- actor.hpp: actor_t가 mesh_node 위임. join/leave/transfer
- actor_models.hpp: actor_ref_t 유지, recv/join_info→receive record/reply_token
- spot_node_models.hpp: mesh_node_status/peer_entry로 축소, subject/socket entry 삭제
- operation_contracts.hpp: fluent builder를 operation_id 반환 모델로

## 아키텍처 전환 5대 리스크
1. 수신 역전: push per-part → pull(set_ready_handler→drain_ready→take_claim→claim_recv_batch→receive_batch_data/parts→mesh_reply)
2. reply 토큰화: reply_*_part(rid,seq) → zlink_mesh_reply_token_t
3. 송신 일괄화: *_part 스트리밍 → (parts,count)+operation_id, 완료는 COMPLETION record
4. 폐기: route_bridge·subjects·internal_sockets·pub bind·pub/sub rid·subscription event·spot actors 열거
5. stream_session 신설: actor bind/unbind가 별도 zlink_stream_session_service_* 핸들

## samples (12 전환, 1 삭제 후보)
삭제 후보: spot_channel_example.cpp(route_bridge). 재작성: spot_pubsub/rpc/timer, actor_* 9개, actor_sample_common.hpp. 유지: raw socket 샘플 7개.

## 진행 순서
1. 삭제(spot_route_bridge.cpp, route_bridge 공개타입, spot_channel_example) + CMake
2. spot_node.cpp → mesh_node 재작성 + 공개계약
3. spot.cpp/spot_send/spot_receive pull batch 재작성
4. actor.cpp/actor_operations + stream_session 신설
5. request_reply/reply_operations
6. samples 전환
7. 컴파일 green → 공통 smoke → bindings 리뷰 campaign

## 진행 방식 변경 로그 (2026-07-18)

**변경**: cpp bindings Service 재작성은 공개 C++ 계약(클래스명 spot_node_t→
mesh_node_t, 수신 push→pull, reply 토큰화)이 파일 간 강결합이라 파일 단위
독립 재작성·검증이 불가능하다. 부분 재작성은 컴파일 불가 중간 상태를
남기므로, 아래 **파일군 단위**로 한 번에 재작성해 각 군 끝에서 컴파일
green을 확인하는 방식으로 진행한다(사용자 승인 "최적 판단·방법 변경").

- 군1(모델·공개계약): spot_node.hpp→mesh_node.hpp(mesh_node_t), spot.hpp,
  actor.hpp, actor_models.hpp, spot_node_models.hpp→mesh_node_models.hpp,
  operation_contracts.hpp. route_bridge·spot_node_publisher·subjects·
  internal_sockets·pub bind·pub/sub rid·dispatch_workers 공개 타입 제거.
- 군2(node 구현): spot_node.cpp→mesh_node.cpp, spot_route_bridge.cpp 삭제.
- 군3(spot 구현): spot.cpp/spot_send.cpp/spot_receive.cpp — pull batch·
  일괄 송신·mesh_reply.
- 군4(actor 구현): actor.cpp/actor_operations.cpp + stream_session 신설 래퍼.
- 군5(request/reply): request_reply.cpp/reply_operations.cpp/send_operations.cpp.
- 군6(samples): 12개 전환, spot_channel_example 삭제.
- 군7: 컴파일 green → 공통 smoke → bindings 리뷰 campaign(Codex·Sonnet).

각 군 완료 시 커밋하고 ledger S8-CPP 증거에 기록한다. bindings 공개 명명은
RouteMesh 개념(MeshNode)에 맞춰 mesh_node_t로 개명하며, 제거 개념명
(SpotNode/bridge)은 no-hit 대상이다. framework(cpp)와 samples는 이 명명을
따른다.

## 진행 방식 2차 변경 로그 (2026-07-18)

**변경**: bindings/framework 재작성 규모(4언어×수천 줄+samples)를 메인 루프
단독 순차 재작성으로 처리하면 컨텍스트 자동 요약이 반복되며 코드 일관성이
저하된다. 사용자 명시 지침("방법 변경 가능·완료 최우선")에 따라, 각 lane의
대규모 구현(파일군 재작성)은 **worktree 격리 구현 에이전트**가 컴파일 green
(`cmake --build`)까지 수행하고, coordinator(메인 루프)가 통합·검증한 뒤
**리뷰 campaign(Codex·Sonnet 3축)**으로 품질을 확보한다. 이는 ledger의 원래
"구현→리뷰 campaign" 설계와 일치하며, 구현 위임 후 독립 리뷰로 검증한다.
(기존 "구현은 메인 루프 직접" 원칙을 이 대규모 릴리스 작업에 한해 조정.)
