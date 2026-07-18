# S8 NODE bindings 전환 리뷰 — iteration 2 — R2 (Claude Sonnet)

독립 리뷰(다른 리뷰어·coordinator 해석 미참조). 대상 commit `7fead5f17`.

## 1. Scope 확인

```
git ls-files bindings/node/src bindings/node/native/src bindings/node/samples \
  bindings/node/binding.gyp bindings/node/package.json \
  | grep -vE '/build/|node_modules|prebuilds' | LC_ALL=C sort | xargs sha256sum | sha256sum
```

- 파일 수: **140** (기대치 일치)
- aggregate hash: **`8a47280f86dff6fcbba089f18c22719fd3ef80c0308f2da0b2993eb98ab1970c`** (기대치 일치)
- 대상 commit `7fead5f17` 확인, scope 파일은 HEAD까지 미변경(hash 그대로 재현) — 정적 소스 대조 유효.

## 2. iter-1 finding 해소 판정

`../iteration-1/finding-ledger.ko.md`의 NF1-NF7·NI2-1·NI3-1을 commit `7fead5f17`에서 소스 대조로 재검증했다.

| Finding | 판정 | 근거 요지 |
|---|---|---|
| NF1 wire enum 값 불일치 | **해소** | `dispatch.ts` `ReceiveKind`(1-13)·`OperationKind`(1-11), `mesh_node.ts` `MeshNodeState`(1-7), `MeshDestinationKind`(1-5)를 Core `zlink_mesh_record_kind_t`/`zlink_mesh_operation_kind_t`/`zlink_mesh_node_state_t`/`zlink_mesh_destination_kind_t`(core/include/zlink/service/{dispatch,mesh_node}.h)와 값 단위 전수 대조 — 완전 일치. `ActorLifecycleKind`(1-5)·`ActorJoinResult`(0/1)·`ActorTransferRole`(1/2)·`ActorTransferPhase`(1-5)도 `core/include/zlink/service/actor.h`와 일치 |
| NF2 RouterSocket spot 메서드 dead | **해소** | `contracts/sockets/router_socket.ts`에 spot 메서드 없음. `routerSpot`/`sendToSpot`/`requestToSpot`/`replyToSpot` 전체 scope grep — RouterSocket 관련 hit 0(남은 hit은 `Spot` 서비스 자신의 정상 메서드/샘플뿐). `addon_exports.cc`도 `routerRequest`/`routerReply`만 export |
| NF3 kind_data 폐기 | **해소** | `addon_mesh_service.cc:297-406` `svc_create_kind_data`가 `kind`(+ActorJoin/ActorLookup 분기는 `operation_kind`)로 5종 페이로드를 판독해 typed JS 객체로 materialize. `zlink_actor_control_record_t`/`zlink_actor_join_completion_t`/`zlink_actor_location_t`/`zlink_mesh_send_ready_data_t`/`zlink_actor_transfer_control_t`(actor.h/dispatch.h) 필드 순서·타입과 1:1 대조 완료. 전부 스칼라 copy-out이라 batch storage 포인터 미보존 |
| NF4 ready handler mask 무시+unregister 부재 | **해소** | `ready_handler_bridge`(addon_mesh_service.cc:1041)가 `invoke_ready_handler_js`의 JS 반환값을 Core에 그대로 반환(재진입 시 직접 호출, 아닐 시 threadsafe blocking call). `mesh_node_unset_ready_handler` 신설+`addon_exports.cc`/`addon_spot_api.h` export. TS `MeshNode.setReadyHandler`가 재등록 전 `clearReadyHandler()`를 선행 호출(이전 등록 누수 방지), `close()`도 동일하게 정리 후 destroy |
| NF5 close/release 결과 폐기 | **해소** | `mesh_node_destroy`가 `ZLINK_CLOSE_OK` 아니면 throw(핸들을 JS에 유지). TS `closeCall`이 실패 시 throw하고 `_native=null` 대입은 그 다음 문장이라 실패 시 미실행 — 이 패턴이 `socket_base.ts`/`spot.ts`/`stream_session.ts`/`publisher.ts`/`monitor_socket.ts`/`poller.ts`/`timer.ts`/`context.ts` 등 scope 내 모든 `close()` 경로에 일관 적용됨을 확인 |
| NF6 count 타입 불일치 | **해소** | `svc_set_size`(addon_mesh_service.cc:84)가 `napi_create_double`로 JS `number`를 emit — TS `ReceiveRequirements.{messageCount,partCount,byteCount}: number`와 일치(BigInt 아님) |
| NF7 actor-transfer API 부재 | **해소** | addon에 `mesh_node_actor_transfer_{prepare,commit,activate,abort}` 신설(`addon_exports.cc`/`addon_spot_api.h` export+선언), Core `zlink_actor_transfer_prepare_t`/`prepare_result_t`/`token_t`/`zlink_mesh_node_actor_transfer_*` 함수와 필드·시그니처 1:1 대조 완료. `contracts/service/transfer.ts`(Role/Phase 값 일치)+`MeshNode.{prepare,commit,activate,abort}ActorTransfer` 런타임 구현 완비 |
| NI2-1 raw Router의 Spot 개념 누출 | **해소** | NF2와 동일 root, 해소 확인. `sample_support.ts`가 `zlink.ReceiveKind`/`zlink.OperationKind`를 패키지에서 import(자체 raw 상수 재정의 제거) — enum 단일 소스화 |
| NI3-1 폐기·dead 잔재 | **해소** | `create_request_js_state`/`sync_request_callback`/`wait_sync_request`/`sync_request_state_t` diff로 전량 삭제 확인. `create_core_request_js_state`/`request_reply_callback_trampoline`은 이름이 유사하나 별개 헬퍼로, `addon_core.cc`의 `dealer_request`/`router_request`(raw 소켓 계층)에서 실사용 중이라 dead 아님. `addon_message_values.h:77` 주석이 제거된 `zlink_msg_gets` 언급 없이 재작성됨. `spotNodeActorBindRemoteSession` 0 hit |

새 반례 없음 — 전량 재개하지 않는다.

## 3. 신규 전체 scope 3축 리뷰

### I1 계약 구현 일치

- **wire enum**: 위 표에서 `ReceiveKind`/`OperationKind`/`MeshNodeState`/`MeshDestinationKind`/`ActorLifecycleKind`/`ActorJoinResult`/`ActorTransferRole`/`ActorTransferPhase`/`ReadyOwnerKind` 전부 Core enum과 값 단위 재확인 완료.
- **pull dispatch 수명**: reply token은 `napi_create_buffer_copy`로 32바이트(`zlink_mesh_reply_token_t` opaque uint64x4)를 JS Buffer로 복사 — 배치/클레임을 close한 뒤에도 reply 호출 시 네이티브가 Buffer 바이트를 재파싱하므로 안전. 수신 parts는 `zlink_mesh_receive_batch_retain_message`로 batch storage와 독립된 message로 retain된 뒤 버퍼화되어 batch/claim close 이후에도 참조 안전.
- **transfer API**: prepare/commit/activate/abort 4종 모두 Core 함수 시그니처·구조체 필드와 1:1 대응(`addon_mesh_service.cc:2099-2212`).
- **actor-join reply routing**: `ReceiveRecord.replyActorJoin`이 `actorJoinReply`(=`zlink_actor_join_reply`)를 reply token 경유로 호출하며, 범용 `reply()`(`zlink_mesh_reply`)와 별개 경로로 존재 — Core API 형태와 일치.
- **raw-layer drift**: addon native/src에서 실제 호출되는 `zlink_*` 함수 심볼(함수-호출 형태 181개)을 `core/include/`와 전수 대조. `zlink_stream_detach` 1건만 공개 헤더 밖이나, 이는 `core/src/api/socket/socket_api.cpp:90`의 `ZLINK_INTERNAL_EXPORT` 내부 심볼을 `extern "C"` 로컬 선언으로 직접 링크하는 기존 패턴(2026-02~03월 커밋, 이번 iter-1→iter-2 diff 밖, socket 레이어). 신규 drift 아님.
- `MeshNode` 계약 인터페이스 45개 메서드 전량이 런타임 구현에 존재(gap 0) — 표면 정합.

**Finding 0 — CLEAN.**

### I2 POSD·DDD

- RouterSocket이 더 이상 Spot 개념을 노출하지 않아(NF2/NI2-1 해소) 소켓 계층과 서비스 계층 경계가 명확해짐.
- enum 단일 소스화: 샘플이 패키지 export named enum을 import — 자체 raw 상수 재정의 제거로 중복 지식 소거.
- ready handler 수명 캡슐화: 등록 상태(`ready_handler_state_t` / TS `_readyHandlerState`)가 내부에 은닉되고, 교체·close 시 정리 로직(`clearReadyHandler`)이 일관되게 호출됨.
- kind_data가 discriminated union(`kind: 'actorControl' | 'actorJoinCompletion' | 'actorLookupCompletion' | 'sendReady' | 'transferControl'`)으로 타입화되어 소비자가 안전하게 narrowing 가능 — 이전 opaque 상태 대비 캡슐화 개선.

**Finding 0 — CLEAN.**

### I3 정리 완결성

- no-hit 9종(SpotNode/spot_node/route_bridge/subjects/internal_sockets/dispatch_workers/recv_actor_part/zlink_msg_gets/routerSpot) 전량 재확인 — **0 hit**.
- NI3-1 대상 dead 헬퍼 삭제를 diff로 직접 확인. 이름이 유사한 잔존 헬퍼(`create_core_request_js_state` 등)는 raw 소켓 계층에서 실사용 중이라 dead 아님을 사용처 대조로 검증.

**Finding 0 — CLEAN.**

## 4. 폐기 no-hit 판정

manifest에 기록된 9종 패턴을 scope 전체에서 직접 재실행하여 전부 0 hit을 재확인했다(coordinator 실행 증거를 신뢰하되 직접 재검증).

## 5. 결론

iter-1 finding 9건 전량 해소 확인, 새 반례 없음. 전체 scope 3축(I1/I2/I3) 재검토 결과 finding 0.

BINDINGS REVIEW CLEAN
