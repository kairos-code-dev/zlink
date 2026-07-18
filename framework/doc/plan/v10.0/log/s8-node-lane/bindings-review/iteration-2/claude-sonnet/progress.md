# S8 NODE bindings 리뷰 iteration-2 — R2 (Claude Sonnet) progress

## 1. Scope 확인
```
git ls-files bindings/node/src bindings/node/native/src bindings/node/samples \
  bindings/node/binding.gyp bindings/node/package.json \
  | grep -vE '/build/|node_modules|prebuilds' | LC_ALL=C sort | xargs sha256sum | sha256sum
```
- 파일 수: 140 (기대치와 일치)
- hash: `8a47280f86dff6fcbba089f18c22719fd3ef80c0308f2da0b2993eb98ab1970c` (기대치와 일치)
- 대상 commit `7fead5f17` 존재 확인. 현재 HEAD(`3b252dd58`)는 그 위에 freeze 커밋 1개만 얹은 상태이며
  scope 내 파일은 `7fead5f17` 이후 미변경(작업트리 diff 없음, hash 그대로 일치) — 정적 대조 유효.

## 2. iter-1 finding 해소 검증 (소스 대조, `7fead5f17`)

| Finding | 판정 | 근거 |
|---|---|---|
| NF1 wire enum 값 | 해소 | `dispatch.ts` ReceiveKind(1-13)/OperationKind(1-11), `mesh_node.ts` MeshNodeState(1-7)를 `core/include/zlink/service/dispatch.h`·`mesh_node.h`의 `zlink_mesh_record_kind_t`/`zlink_mesh_operation_kind_t`/`zlink_mesh_node_state_t`와 값 단위 전수 대조 — 완전 일치. `MeshDestinationKind`(1-5)도 `zlink_mesh_destination_kind_t`와 일치 |
| NF2 RouterSocket spot 제거 | 해소 | `contracts/sockets/router_socket.ts`에 spot 메서드 없음. `routerSpot`/`sendToSpot`/`requestToSpot`/`replyToSpot` grep — RouterSocket 관련 hit 0 (남은 hit은 전부 `Spot` 서비스 자신의 정상 메서드). `addon_exports.cc`도 `routerRequest`/`routerReply`만 export |
| NF3 kind_data 타입화 | 해소 | `addon_mesh_service.cc:297-406` `svc_create_kind_data`가 kind(+ActorJoin/ActorLookup은 operation_kind)로 분기해 `zlink_actor_control_record_t`/`zlink_actor_join_completion_t`/`zlink_actor_location_t`/`zlink_mesh_send_ready_data_t`/`zlink_actor_transfer_control_t`를 필드 단위로 `core/include/zlink/service/actor.h`·`dispatch.h`와 대조 — 필드 순서·타입 일치. 스칼라 copy-out(포인터 미보존)이라 batch 수명과 분리됨 |
| NF4 ready handler mask+unregister | 해소 | `ready_handler_bridge`(addon_mesh_service.cc:1041)가 JS 반환값(`invoke_ready_handler_js`)을 Core에 그대로 반환. `mesh_node_unset_ready_handler` 신설+export. TS `MeshNode.setReadyHandler`가 재등록 전 `clearReadyHandler()` 선행 호출로 이전 등록 누수 방지, `close()`도 동일 |
| NF5 close-busy 존중 | 해소 | `mesh_node_destroy`가 `ZLINK_CLOSE_OK` 아니면 throw(핸들 유지). TS `closeCall`이 실패 시 throw하며 `_native=null`은 그 다음 문장이라 실패 시 미실행. 동일 패턴이 `socket_base.ts`/`spot.ts`/`stream_session.ts`/`publisher.ts`/`monitor_socket.ts`/`poller.ts`/`timer.ts`/`context.ts` 등 scope 내 전체 close() 경로에 일관 적용됨을 확인 |
| NF6 count 타입 정합 | 해소 | `svc_set_size`(addon_mesh_service.cc:84)가 `napi_create_double`로 JS `number`를 emit — TS `ReceiveRequirements.messageCount/partCount/byteCount: number`와 일치 |
| NF7 transfer API | 해소 | addon에 `mesh_node_actor_transfer_{prepare,commit,activate,abort}` 신설, `addon_exports.cc`/`addon_spot_api.h`에 export+선언 추가. `contracts/service/transfer.ts`(ActorTransferRole/Phase가 `zlink_actor_transfer_role_t`/`phase_t`와 값 일치)+`MeshNode.prepareActorTransfer/commitActorTransfer/activateActorTransfer/abortActorTransfer` 구현이 Core `zlink_actor_transfer_prepare_t`/`prepare_result_t`/`token_t` 필드와 1:1 대응 |
| NI2-1 raw Router spot 누출 | 해소 | NF2와 동일 root, 해소 확인. `sample_support.ts`도 `zlink.ReceiveKind`/`zlink.OperationKind`를 단일 소스로 import(자체 raw 상수 재정의 제거) |
| NI3-1 폐기·dead 잔재 | 해소 | `create_request_js_state`/`sync_request_callback`/`wait_sync_request`/`sync_request_state_t` 전량 삭제(diff 확인, `create_core_request_js_state`는 별개의 살아있는 dealer/router raw-socket 헬퍼로 잔존은 정상). `addon_message_values.h:77` 주석이 제거된 `zlink_msg_gets` 언급 없이 재작성됨. `spotNodeActorBindRemoteSession` 0 hit |

새 반례 없음. 전량 재개하지 않음.

## 3. 신규 3축 리뷰

### I1 계약 일치
- wire enum 값 재확인(위 표) — `ReceiveKind`/`OperationKind`/`MeshNodeState`/`MeshDestinationKind`/`ActorLifecycleKind`/`ActorJoinResult`/`ActorTransferRole`/`ActorTransferPhase`/`ReadyOwnerKind` 전부 Core enum과 값 단위 일치.
- pull dispatch 수명: reply token은 `napi_create_buffer_copy`로 32바이트 복사(포인터 아님, opaque uint64x4) → 배치 파괴 후 reply 호출해도 안전(네이티브가 Buffer 바이트를 재파싱). 수신 parts는 `zlink_mesh_receive_batch_retain_message`로 batch storage와 분리된 message로 retain 후 버퍼화 — batch/claim close 이후에도 parts 참조 안전.
- transfer API: 위 표. prepare/commit/activate/abort 4종 모두 Core 함수·구조체와 필드 단위 일치.
- actor-join reply routing: `ReceiveRecord.replyActorJoin`이 `actorJoinReply` netive(=`zlink_actor_join_reply`)를 호출, reply token 경유. `reply()`(`zlink_mesh_reply`)와 별개 경로로 존재 — Core API 형태와 일치.
- raw-layer drift: addon native/src 전체에서 실제 호출되는 `zlink_*` 함수 심볼(181개, 함수-호출 형태로 추출)을 `core/include/`와 전수 대조 — `zlink_stream_detach` 1건만 공개 헤더에 없으나, 이는 `core/src/api/socket/socket_api.cpp:90`의 `ZLINK_INTERNAL_EXPORT` 내부 심볼을 addon이 `extern "C"` 로컬 선언으로 직접 링크하는 기존 패턴(2026-02/03월 커밋, scope diff 밖, socket 레이어 — 이번 회차 변경분 아님). 신규 drift 아님.
- MeshNode 계약 인터페이스 45개 메서드 전량이 런타임 구현에 존재(gap 0).

Finding 0. **CLEAN**.

### I2 POSD·DDD
- RouterSocket이 더 이상 Spot 개념을 노출하지 않음(NF2/NI2-1 해소) — 소켓 계층과 서비스 계층 경계가 명확해짐.
- enum 단일 소스화: 샘플이 패키지가 export하는 named enum을 import해서 사용, 자체 raw 상수 재정의 제거.
- ready handler 수명이 캡슐화됨: 등록 상태(`ready_handler_state_t`/`_readyHandlerState`)가 클래스 내부에 은닉되고 교체·close 시 정리 로직이 일관되게 호출됨.
- kind_data가 discriminated union(`kind: 'actorControl' | 'actorJoinCompletion' | ...`)으로 타입화되어 소비자가 records kind로 안전하게 narrowing 가능 — 이전의 unstructured opaque 상태보다 캡슐화 개선.

Finding 0. **CLEAN**.

### I3 정리 완결성
- no-hit 9종(SpotNode/spot_node/route_bridge/subjects/internal_sockets/dispatch_workers/recv_actor_part/zlink_msg_gets/routerSpot) 전량 grep 재실행 — 0 hit 확인(coordinator manifest와 일치, 직접 재확인 완료).
- NI3-1 대상 dead 헬퍼(`create_request_js_state`/`sync_request_callback`/`wait_sync_request`/`sync_request_state_t`) 삭제 diff 확인. 잔존하는 `create_core_request_js_state`/`request_reply_callback_trampoline`은 `addon_core.cc`의 `dealer_request`/`router_request`(raw 소켓 계층, mesh 서비스와 무관)에서 실사용 중 — dead 아님.

Finding 0. **CLEAN**.

## 4. 폐기 no-hit 판정
manifest에 기록된 9종 패턴 전부 scope 내 0 hit 재확인 완료 (coordinator 증거 재검증, 재실행 아님).

## 5. 결론
전 축 CLEAN, finding 0.

BINDINGS REVIEW CLEAN
