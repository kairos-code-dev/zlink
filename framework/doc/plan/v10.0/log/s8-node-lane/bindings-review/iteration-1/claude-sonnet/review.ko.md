# S8 NODE bindings 전환 리뷰 — R2 (Claude Sonnet) iteration 1

## 1. Scope 확인

- 대상 commit: `db26ce544` (HEAD `d0f8ba563`의 조상, 확인됨)
- Scope: `git ls-files bindings/node/src bindings/node/native/src bindings/node/samples bindings/node/binding.gyp bindings/node/package.json | grep -vE '/build/|node_modules|prebuilds'`
- 파일 수(시작/종료 동일): **139**
- Aggregate SHA-256(시작): `b5e4ffac147a6cc50f11416b456d2ebb093d5b8bcc47f0ea1bf6eb232c1b7af8` — 매니페스트 값과 **일치**
- Aggregate SHA-256(종료): `b5e4ffac147a6cc50f11416b456d2ebb093d5b8bcc47f0ea1bf6eb232c1b7af8` — 동일, 리뷰 중 scope 파일 무수정 확인(`git status --short`로 재확인)
- 정적 read/grep만 수행, build/test/실행 없음. R1(Codex) 결과 미참조.

---

## 2. I1 — 계약 구현 일치

### Finding 1
`[I1][Critical] bindings/node/src/zlink/contracts/service/dispatch.ts:33-45 — ReceiveKind/OperationKind 공개 계약 enum이 addon이 실제로 흘려보내는 raw wire enum 값과 불일치 — public API 오염`

- **문제**: `ReceiveKind = { Node:1, Spot:2, Actor:3 }`, `OperationKind = { Send:1, Request:2, Reply:3, Publish:4, ActorJoin:5, ActorLeave:6 }`가 `contracts/service/index.ts`를 거쳐 `contracts/index.ts` → `src/index.ts`(`export * from './zlink/contracts'`)로 패키지 루트까지 공개 export된다. 그러나 이 값들이 실제로 대응해야 할 `ReceiveRecord.kind`/`.operationKind` 필드는 addon이 core의 raw C enum 값을 **그대로** 흘려보낸다:
  - `native/src/addon_mesh_service.cc:1110` `svc_set_int32(env, record_obj, "kind", static_cast<int32_t>(record.kind))` — `record.kind`는 `zlink_mesh_record_kind_t`(`core/include/zlink/service/dispatch.h:34-48`, 값 1~13: NODE_SEND=1 … TRANSFER_CONTROL=13).
  - `native/src/addon_mesh_service.cc:1121` `operation_kind` → `record.operation_kind`는 `zlink_mesh_operation_kind_t`(`dispatch.h:50-62`, 값 1~11: NODE_REQUEST=1 … STREAM_CLOSE=11).
  - `runtime/service/dispatch.ts:40,46`(`RuntimeReceiveRecord` 생성자)도 `raw.kind`/`raw.operationKind`를 변환 없이 그대로 대입.
  - 즉 공개 `OperationKind.ActorJoin`(5)을 써서 `record.operationKind === OperationKind.ActorJoin`을 비교하면 실제로는 raw `ACTOR_LOOKUP`(5)에 매치되고, 진짜 `ACTOR_JOIN`(raw 7)은 어떤 공개 상수와도 매치되지 않는다. `ReceiveKind.Spot`(2)은 raw `NODE_REQUEST`(2)와 충돌한다. 값 4~13에 해당하는 record kind는 공개 enum에 아예 대응 항목이 없다.
- **근거**: `bindings/node/samples/sample_support.ts:14-27`의 코드·주석이 이를 직접 인정한다 — "A received record's `kind` and `operationKind` are the raw core enum values … The samples spell out the ones they need rather than depend on the higher-level contract enums, which describe a different, coarser taxonomy." 샘플 작성자가 이미 공개 계약 enum이 못 쓸 정도로 어긋난다는 것을 알고 로컬 `MeshRecordKind`/`MeshOperationKind`(1~13/1~11, core 값과 정확히 일치)로 우회했지만, `contracts/service/dispatch.ts`의 공개 enum 자체는 고치지 않았다.
- **수정 제안**: `ReceiveKind`/`OperationKind`를 core의 `zlink_mesh_record_kind_t`/`zlink_mesh_operation_kind_t`와 1:1 값으로 재정의하거나(`sample_support.ts`의 로컬 상수를 정본으로 승격), 혹은 raw kind를 파생 taxonomy로 변환하는 매핑 함수를 두고 필드명을 분리해 혼동을 없앤다.

### Finding 2 (동일 root-cause family)
`[I1][Critical] bindings/node/src/zlink/contracts/service/mesh_node.ts:21-27 — MeshNodeState 공개 계약 enum이 zlink_mesh_node_state_t 값과 불일치`

- **문제**: `MeshNodeState = { Idle:1, Connecting:2, PartialReady:3, Ready:4, Error:5 }`(5개 값). 그러나 `MeshNodeStatus.state`는 `zlink_mesh_node_state_t`(`core/include/zlink/service/mesh_node.h:24-32`: CREATED=1, STARTED=2, PARTIAL_READY=3, READY=4, DRAINING=5, STOPPED=6, ERROR=7 — 7개 값)의 raw passthrough다: `native/src/addon_mesh_service.cc:616`(`svc_set_int32(env, obj, "state", static_cast<int32_t>(status.state))`)·`:1867`(peer status에서도 동일 패턴), `runtime/service/mesh_node.ts:207`(`state: raw.state`, 변환 없음).
  - `MeshNodeState.Error`(5)는 실제로는 raw `DRAINING`(5)과 매치되어, 노드가 단순 draining 중인데 "Error"로 오판될 수 있다. 진짜 `ERROR`(raw 7)·`STOPPED`(raw 6)에 대응하는 공개 상수는 없다.
- **근거**: 위 addon/runtime 코드 인용 + core 헤더 정의 대조.
- **수정 제안**: `MeshNodeState`를 core 7-값 taxonomy(CREATED/STARTED/PARTIAL_READY/READY/DRAINING/STOPPED/ERROR)와 정확히 맞춘다.
- **참고**: 동일 파일의 `SpotKind`(`spot.ts:11`, Invalid=0/Entry=1/User=2)·`SubscriptionKind`(`spot.ts:15`, Exact=1/Prefix=2)·`ReadyOwnerKind`(`dispatch.ts:29`, Node=1/Spot=2/Actor=3)는 각각 `zlink_spot_kind_t`·`zlink_spot_subscription_kind_t`·`zlink_mesh_owner_kind_t`와 정확히 일치함을 확인했다(문제 없음) — 즉 공개 raw-passthrough enum 상수들 중 일부만 드리프트했고 나머지는 정확하다. 같은 파일군에서 반복되는 패턴이므로 하나의 root-cause family로 묶는다: "공개 계약의 raw-passthrough 편의 enum이 실제 core wire 값과 별도로 유지되며 동기화 검증이 없다."

### Finding 3
`[I1][Critical] bindings/node/src/zlink/runtime/sockets/router_socket.ts:84-155, contracts/sockets/router_socket.ts:43-47 — RouterSocket.sendToSpot/requestToSpot/replyToSpot가 addon에 구현되지 않은 native 함수를 호출(런타임 TypeError)`

- **문제**: 공개 `RouterSocket` 인터페이스(`contracts/sockets/router_socket.ts:43-47`)와 그 구현(`runtime/sockets/router_socket.ts:84-155`)이 `native.routerSpotSend`/`native.routerSpotRequest`/`native.routerSpotReply`를 호출한다. 이 세 함수는 `runtime/native/binding_socket.ts:39-61`에 TS **타입**으로만 선언돼 있을 뿐, addon C++ 쪽에는 대응 구현이 전혀 없다:
  - `native/src/*.cc`, `*.h` 전체에서 `routerSpotSend|routerSpotRequest|routerSpotReply|router_spot_send|router_spot_request|router_spot_reply|RouterSpotSend|RouterSpotRequest|RouterSpotReply` grep — 0 hit.
  - `native/src/addon_exports.cc:72-75`의 export 테이블에는 `routerRequest`/`routerReply`/`routerRecvMessage`/`routerRecvMessageNoWait`만 등록돼 있고 spot 변형은 없다.
  - `core/include/zlink/socket/api.h` 및 `core/include/zlink/service/*.h` 전체에서 "router"와 "spot"가 결합된 API grep — 0 hit. Core 10.0.0은 라우터 소켓에서 spot으로 직접 보내는 API를 아예 갖고 있지 않다(spot 주소 지정은 `Spot.sendToSpot`/`requestToSpot` → `zlink_spot_send_to_spot`/`zlink_spot_request_to_spot`을 통해서만 이뤄진다 — 이는 정상적으로 매핑돼 있고 정확히 동작함, `runtime/service/spot.ts:73-108` 참조).
  - 즉 `RouterSocket.sendToSpot(...)`를 호출하면 `native.routerSpotSend`가 `undefined`이므로 즉시 `TypeError: native.routerSpotSend is not a function`. 이는 addon node-gyp 빌드/tsc 타입체크로는 잡히지 않는다(타입 선언은 존재하므로 tsc green, 런타임 호출 전무이므로 addon 링크도 무관).
  - 샘플에서 유일하게 "requestToSpot"을 호출하는 `samples/spot_rpc_example.ts:52`는 `RouterSocket`이 아니라 `Spot`(`clientNode.createSpot()`으로 얻은 객체)의 `requestToSpot`을 호출하므로 이 결함 경로를 우연히도 실행하지 않는다 — coordinator의 "samples tsc green" 증거는 이 결함을 반증하지 않는다.
- **근거**: 위 grep 결과(0 hit) + `router_socket.ts`/`binding_socket.ts`/`addon_exports.cc` 코드 인용 + `spot_rpc_example.ts:52` 대조.
- **수정 제안**: `RouterSocket.sendToSpot`/`requestToSpot`/`replyToSpot`와 관련 native 타입 선언(`binding_socket.ts:39-61`)을 완전히 삭제한다(구 SpotNode/route_bridge 시대 router-to-spot 직결 모델의 잔재이며, 10.0.0에서는 Spot 기반 API로 완전히 대체됨). 필요하면 `contracts/sockets/router_socket.ts` 문서에도 "spot addressing은 Spot API를 쓰라"는 안내를 남긴다.

### Finding 4
`[I1][Medium] Core 10.0.0 actor-transfer API가 Node addon/TS 표면 어디에도 매핑되지 않음`

- **문제**: `core/include/zlink/service/actor.h`가 노출하는 `zlink_mesh_node_actor_transfer_prepare`/`_commit`/`_activate`/`_abort`(spot 간 actor 이관 기능)에 대응하는 항목이 `native/src/addon_exports.cc`의 export 테이블, `contracts/service/mesh_node.ts`의 `MeshNode` 인터페이스, `runtime/service/mesh_node.ts`, 샘플 어디에도 없다(전체 scope grep "transfer" — 주석 1건 무관 매치 외 0 hit).
  - `bindings-transition-design.ko.md`(같은 s8-node-lane 폴더)의 "군(group) 1~6" 실행 계획에도 actor-transfer는 언급이 없다 — tests/perf처럼 "후속(2차)"로 명시 이연된 것도 아니고, 그냥 계획 자체에서 빠져 있다.
- **근거**: `core/include/zlink/service/actor.h`(전송 함수 4개) vs `addon_exports.cc`/`contracts/service/mesh_node.ts` grep 결과 대조.
- **수정 제안**: node 10.0.0 전환 범위에 actor-transfer 포함 여부를 명시적으로 결정하고(포함이면 매핑 추가, 배제면 s8-node-lane 문서에 이연 사유 기록).

**I1 Verdict: NOT CLEAN** (finding 4건, root-cause family 2개: [A] 공개 enum-wire 값 드리프트 Finding 1·2, [B] 구 router-to-spot 모델 잔재로 인한 런타임 파손 Finding 3, [C] actor-transfer 매핑 누락 Finding 4)

---

## 3. I2 — POSD·DDD

샘플로 정독한 `contracts/service/{mesh_node,spot,stream_session,dispatch,publisher}.ts`, `runtime/service/{mesh_node,spot,dispatch}.ts`, `native/src/addon_mesh_service.cc`, `addon_exports.cc`는 contracts(공개 인터페이스) / runtime(얇은 마셜링 어댑터) / native(N-API 경계) 3계층이 명확히 분리돼 있고, 각 `runtime/service/*.ts` 클래스는 자기 native handle 하나만 소유하는 얕고 정직한 어댑터로 정보 은닉이 적절하다. 필드명·메서드명이 addon export 테이블과 1:1로 대응해 추적이 쉽다.

### Finding
`[I2][Low] RouterSocket과 Spot이 동일한 이름(sendToSpot/requestToSpot/replyToSpot)의 메서드를 서로 다른 두 애그리게잇에 노출 — 하나(Spot)만 동작, 다른 하나(RouterSocket, I1 Finding 3)는 죽은 코드`

- **문제**: `contracts/sockets/router_socket.ts:43-47`과 `contracts/service/spot.ts:48-62`가 이름이 동일한 3개 메서드를 서로 무관한 두 타입에 선언한다. 두 타입 모두에서 이 메서드 이름이 통용되는 것처럼 보이지만 실제로 동작하는 것은 `Spot` 쪽뿐이다(I1 Finding 3). 이름 충돌이 API 경계 혼동을 낳고, 죽은 코드가 방치된 근본 원인이기도 하다.
- **근거**: 위 두 계약 파일 인용.
- **수정 제안**: I1 Finding 3 해소(RouterSocket 쪽 spot 메서드 삭제)로 자동 해소됨.

**I2 Verdict: NOT CLEAN** (finding 1건, I1 Finding 3과 동일 root-cause family)

---

## 4. I3 — 정리 완결성

### Finding 1
`[I3][Low] bindings/node/src/zlink/runtime/native/binding_socket.ts:151 — spotNodeActorBindRemoteSession, 삭제된 SpotNode 개념의 죽은 타입 선언 잔존`

- **문제**: `SocketNativeBinding` 인터페이스에 `spotNodeActorBindRemoteSession(node, actor, sourceNodeRid, sourceSessionRid): void` 타입이 여전히 선언돼 있다. 이름 자체가 폐기된 `SpotNode`(→ `MeshNode`) 개념을 담고 있고, 기능적으로도 완전히 대체된 것으로 보인다: 같은 인터페이스에 `streamBindActor`(:157)·`streamSessionBindActor`(addon export `streamSessionBindActor`, `addon_exports.cc:201`)가 stream_session 기반의 신규 대응물로 존재한다.
- **근거**: scope 전체(`bindings/node/`)에서 `spotNodeActorBindRemoteSession` grep — 선언 위치(`binding_socket.ts:151`)와 빌드 산출물(`dist/.../binding_socket.d.ts:55`, scope 밖 build 아티팩트) 외 0 hit. 어떤 `runtime/`·`samples/` 코드도 이를 호출하지 않는다. addon C++ 쪽에도 `spotNodeActorBindRemoteSession`/`spot_node_actor_bind_remote_session` 대응 구현이 없다(호출되지 않으므로 런타임 파손은 아니고 순수 죽은 타입).
- **수정 제안**: 해당 타입 선언 삭제.

### Finding 2
`[I3][Low] bindings/node/native/src/addon_message_values.h:77 — 삭제된 core 심볼 zlink_msg_gets()를 "현재도 존재하는 스텁"으로 서술하는 낡은 주석`

- **문제**: 주석이 "The current core message property API is a stub: zlink_msg_gets() always returns NULL."이라 적고 있으나, `zlink_msg_gets`는 core 10.0.0에 함수로도 선언으로도 전혀 존재하지 않는다(스텁이 아니라 완전 제거됨).
- **근거**: `grep -rn "zlink_msg_gets" core/include/ core/src/ bindings/node/native/src/` — 이 주석 줄 1건만 매치, core 쪽 0 hit. `bindings-transition-design.ko.md`의 "삭제 심볼" 목록에도 `zlink_msg_gets` `addon_message_values.h:77`가 명시적으로 올라 있다(설계자도 인지한 드리프트 지점).
- **수정 제안**: 주석을 "core 10.0.0은 메시지 프로퍼티 API(구 zlink_msg_gets)를 제공하지 않는다"로 갱신.

### 폐기 개념 no-hit 판정 (scoped grep, `bindings/node/src bindings/node/native/src bindings/node/samples` 대상)
| 개념 | 결과 |
|---|---|
| `SpotNode` | 0 hit |
| `spot_node`(snake) | 0 hit |
| `route_bridge` / `RouteBridge` | 0 hit |
| `subjects` | 0 hit |
| `internal_sockets` | 0 hit |
| pub-sub rid(`pub_routing_id`/`sub_routing_id`/`pubRoutingId`/`subRoutingId`) | 0 hit |
| `dispatch_workers` | 0 hit |
| `recv_actor_part` | 0 hit |
| `msg_gets` | **1 hit** — `addon_message_values.h:77`(위 I3 Finding 2, 주석 잔재) |

**I3 Verdict: NOT CLEAN** (finding 2건, 각각 독립 root-cause: 삭제된 SpotNode 바인딩 잔존형 / 삭제된 zlink_msg_gets 서술 주석 잔존)

---

## 5. 총평

Pull-dispatch(claim/batch/ready-index)·actor-join reply route(`replyActorJoin` → `zlink_actor_join_reply`, `record.reply()` → `zlink_mesh_reply` 정확히 분리)·MeshNode/Spot/StreamSessionService의 대다수 메서드 매핑은 core C API export 테이블과 1:1로 정확히 대조되며 계층 구조(contracts/runtime/native)도 견고하다. 그러나:
1. 공개 계약의 raw-passthrough 편의 enum(`ReceiveKind`/`OperationKind`/`MeshNodeState`) 중 일부가 실제 wire 값과 어긋나 있고, 샘플 작성자가 이를 알고 우회했음에도 계약 자체는 고쳐지지 않았다(I1 Critical).
2. `RouterSocket`의 spot 관련 세 메서드가 addon에 구현되지 않은 native 함수를 호출해 호출 즉시 런타임 예외를 던진다(I1 Critical, I2 Low).
3. actor-transfer core API가 이 전환에서 완전히 누락됐다(I1 Medium).
4. 폐기 개념 잔재 2건(스팟노드 바인딩 타입, 낡은 zlink_msg_gets 주석)이 남아 있다(I3 Low).

두 axis 이상에서 실질적 결함이 확인되어 이번 iteration은 CLEAN 판정을 내릴 수 없다.

BINDINGS REVIEW NOT CLEAN
