# S8 NODE bindings 리뷰 iteration-1 — 병합 finding ledger

두 리뷰어(R1 opus, R2 Sonnet) iteration-1(snapshot `db26ce544`, 139파일 `b5e4ffac`). 둘 다
`BINDINGS REVIEW NOT CLEAN`. coordinator가 수정 후 새 snapshot으로 iteration-2.

## I1 계약 구현 일치

### NF1. wire enum 값 불일치 [blocker]
addon이 raw `zlink_mesh_record_kind_t`(1-13)/`zlink_mesh_operation_kind_t`(1-11)를 그대로 전달
(`addon_mesh_service.cc:1110,1120`)하는데, 공개 TS enum이 다른 값:
- `dispatch.ts:32-45` `ReceiveKind={Node:1,Spot:2,Actor:3}`, `OperationKind={…ActorJoin:5,ActorLeave:6}`
  → `OperationKind.ActorJoin`=5는 실제 `ACTOR_LOOKUP`, 실제 `ACTOR_JOIN`=7. 소비자가 잘못 비교.
- `mesh_node.ts:21-27` `MeshNodeState`(5값), `Error`=5가 Core `DRAINING`=5와 충돌(실제 `ERROR`=7,
  `STOPPED`=6 누락). Core `zlink_mesh_node_state_t`는 7값.
- 샘플이 자체 raw 상수로 우회(`sample_support.ts:14-27`)한 것이 방증.
→ 공개 enum 값을 Core C enum과 정확히 일치시킴.

### NF2. RouterSocket.sendToSpot/requestToSpot/replyToSpot dead [blocker]
`binding_socket.ts:39-61`에 `native.routerSpot*` TS 타입만 있고 addon export 없음
(`addon_exports.cc`는 `routerRequest`/`routerReply`만) → 호출 시 `undefined is not a function`.
Core 10.0.0은 router-to-spot API 자체 없음(`zlink_router_*_spot_part` 제거). 정상 경로는
`Spot.sendToSpot/requestToSpot`. → RouterSocket에서 spot 메서드 제거(+I2 naming collision 해소).

### NF3. receive record kind_data 폐기 [high]
`zlink_mesh_receive_record_t.kind_data`가 바인딩에 0 hit → actor-lookup/join-completion/
transfer-control 구조화 데이터 판독 불가. → typed kind_data record 노출(cpp/dotnet 참조 표면과 정합).

### NF4. ready handler가 JS 반환 mask 무시 + unregister/cleanup 부재 [high]
`addon_mesh_service.cc:857` ready handler가 항상 `ready_domains`를 반환하고 JS가 반환한 drain mask를
무시. handler 교체·해제 경로 없음. → JS 반환 mask 전달, unregister/cleanup·예외 경계.

### NF5. close/release 결과 폐기 [medium]
`mesh_node_destroy` 등 반환값 무시하면서 TS가 핸들을 null화 → close-busy 시 누수 무신호.
→ close 결과 존중(busy 시 핸들 유지).

### NF6. buffer-too-small 카운트 타입 불일치 [medium]
`messageCount/partCount/byteCount`를 BigInt로 emit하나 TS 타입은 `number`. → 타입 정합(bigint 또는 변환).

### NF7. actor-transfer API 부재 [high]
Core `zlink_mesh_node_actor_transfer_*`(prepare/commit/activate/abort)가 addon/TS/samples에 0 매핑.
→ transfer API 노출(참조 표면 정합).

## I2 POSD·DDD

### NI2-1. raw Router가 Spot 서비스 개념 누출 [high] (NF2와 동일 root)
RouterSocket과 Spot이 동명 spot 메서드 노출(Router 쪽만 dead). wire-enum 지식이 contract와 샘플에
중복. ready-dispatch 승인/수명 미캡슐화. → NF2 제거 + enum 단일 소스 + dispatch-turn 캡슐화.

## I3 정리 완결성

### NI3-1. 폐기·dead 잔재 [medium]
- dead `native.routerSpot*` 선언(NF2), `spotNodeActorBindRemoteSession`(`binding_socket.ts:151`,
  SpotNode 잔재, `streamBindActor`로 대체됨).
- 미사용 push-style 요청 헬퍼 `create_request_js_state`/`sync_request_callback`/`wait_sync_request`.
- `addon_message_values.h:77` 제거된 `zlink_msg_gets` 설명 주석 잔재.
→ 전부 제거. no-hit 통과(route_bridge/subjects/internal_sockets/pub-sub rid/dispatch_workers/
recv_actor_part/subscribe_handler는 이미 clean; msg_gets 주석만 잔존).

## 처리 방침
coordinator 격리 수정. Core enum 값 정합·RouterSocket spot 제거·kind_data/transfer 노출·ready
handler 정합·close 결과 존중. addon node-gyp green + `tsc` src+samples green, no-hit(주석 포함) 통과.
참조 표면(kind_data record·transfer)은 dotnet/cpp lane과 정합. 완료 후 iteration-2.
