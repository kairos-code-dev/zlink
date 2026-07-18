# S8 NODE bindings 리뷰 iteration-2 — R1(opus) progress

## Scope 확인
- 시작 파일 수: 140 (일치)
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `8a47280f86dff6fcbba089f18c22719fd3ef80c0308f2da0b2993eb98ab1970c` (일치)
- HEAD `3b252dd58`; target `7fead5f17`는 HEAD 조상이며 `git diff 7fead5f17 HEAD -- bindings/node/{src,native/src,samples,binding.gyp,package.json}` 무변경 → scope 내용은 target commit과 byte 동일.
- 파일 미수정(정적 대조). build/실행 안 함.

## iter-1 해소 검증 (commit 내용 대조)
- NF1 wire enum: dispatch.ts `ReceiveKind`(13) = `zlink_mesh_record_kind_t`(1-13) 전값 일치; `OperationKind`(11) = `zlink_mesh_operation_kind_t`(1-11) 전값 일치(ActorJoin=7 정정); mesh_node.ts `MeshNodeState`(7) = `zlink_mesh_node_state_t`(1-7) 전값 일치(Stopped=6/Error=7 추가). ReadyOwnerKind/MeshDestinationKind/ActorLifecycle/ActorJoinResult/TransferRole/TransferPhase 도 actor.h·dispatch.h 대조 일치. sample_support.ts 는 `zlink.ReceiveKind/OperationKind` 공개 enum 사용(raw 상수 제거). → 해소.
- NF2 RouterSocket spot 메서드: router_socket.ts·binding_socket.ts(native)에서 sendToSpot/requestToSpot/replyToSpot·routerSpot* 0 hit. → 해소.
- NF3 kind_data: dispatch.ts `ReceiveKindData` typed union(5 variant) + addon `svc_create_kind_data`(sizeof guard)로 record.kind/operation_kind 분기 emit, runtime conversions.ts `kindDataFromRaw` 매핑. → 해소.
- NF4 ready handler: addon `invoke_ready_handler_js` 가 JS 반환 uint32 mask read-back, JS-thread 재진입 직접호출, 예외 catch→mask 0, `mesh_node_unset_ready_handler` unregister+tsfn release. runtime setReadyHandler 가 state 저장·교체시 clearReadyHandler 선행·close()에서 unset. → 해소.
- NF5 close 결과: mesh_node_destroy 가 `!= ZLINK_CLOSE_OK` 시 throw 하고 handle null화 안 함(busy 시 JS 소유 유지). → 해소.
- NF6 count 타입: addon `svc_set_size`=`napi_create_double`(JS number), TS `messageCount/partCount/byteCount: number` 정합. → 해소.
- NF7 transfer API: addon prepare/commit/activate/abort(actor.h struct 사용) + addon_exports.cc 등록 + transfer.ts contract + mesh_node.ts 4 메서드. → 해소.
- NI2-1: NF2 root 제거로 Router↔Spot 개념누출 해소, enum 단일 소스(contracts), dispatch-turn 캡슐화(ready handler state). → 해소.
- NI3-1: spotNodeActorBindRemoteSession/sync_request_callback/wait_sync_request/zlink_msg_gets 0 hit. create_request_js_state 는 `create_request_js_state_impl` 로 live `create_core_request_js_state` 내부헬퍼화(dead 아님). → 해소.

## 전체 scope 3축 재검토
- I1 mesh/dispatch: enum(record/operation/state/owner/destination/lifecycle/joinResult/transferRole/phase)·transfer·kind_data·ready·close 전량 대조 clean. addon_exports 등록·runtime 배선 end-to-end 일치.
- I1 잔존 finding(독립 검증):
  - NF2-1 result enum 누락: RequestResult 가 Backpressured=113 누락(Core zlink_errno.h:138); RecvResult 207/208, ConnectResult 608, ConfigResult 707/708/709 누락. results.ts 대조.
  - NF2-2 MonitorSourceKind={Socket:1,SpotPub:3,SpotSub:4} vs Core zlink_monitor_source_kind_t=SOCKET:1 만(zlink_enum.h:200-203). addon 이 source_kind raw 통과(addon_monitor_status_values.h:12) → 3/4 도달불가 drift.
- I2/I3 finding(독립 검증):
  - NF2-3/4 binding_socket.ts:128-152 의 streamBindActor/streamBoundActors/streamSendBoundActorPart/streamUnbindActor — addon_exports.cc 미등록(socketStreamAttach·streamSession* 만 등록), scope 전역 호출부 0. StreamSocket contract/runtime 에 actor 메서드 없음 → 미등록 dead 선언(타도메인 누출).
  - NF2-5 MonitorSourceKind.SpotPub/SpotSub dead 멤버.
- no-hit 9종·forbidden 토큰 전량 0(재확인). sendToSpot 유일 hit 은 Spot 타입(정당).

## 검증 방법
- exhaustive sweep(Explore, read-only) 로 후보 도출 → R1 이 각 claim 을 자체 grep/read 로 독립 재검증(addon_exports.cc 등록표, scope 전역 참조, Core enum/errno 헤더 대조). subagent 결과를 그대로 채택하지 않음.

## 판정
- iter-1 finding 전량 해소. 그러나 신규/잔존 finding(NF2-1~NF2-5)로 각 축 finding 0 미충족.
- 최종: BINDINGS REVIEW NOT CLEAN
