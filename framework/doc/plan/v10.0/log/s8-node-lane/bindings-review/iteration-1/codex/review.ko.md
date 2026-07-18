# S8 NODE bindings 전환 독립 리뷰 — iteration 1 — R1 Codex

## 1. Scope 확인

| 시점 | 파일 수 | aggregate SHA-256 | 판정 |
|---|---:|---|---|
| 시작 | 139 | `b5e4ffac147a6cc50f11416b456d2ebb093d5b8bcc47f0ea1bf6eb232c1b7af8` | manifest와 일치 |
| 종료 | 139 | `b5e4ffac147a6cc50f11416b456d2ebb093d5b8bcc47f0ea1bf6eb232c1b7af8` | 시작값·manifest와 일치 |

검토 범위는 prompt의 `git ls-files` 명령으로 고정했다. build, test, 실행은 하지 않았다. 동적 증거는 manifest에 기록된 addon node-gyp green, source·sample tsc green만 사용했다.

## 2. I1 — 계약 구현 일치

### Findings

- [I1][high] `bindings/node/src/zlink/contracts/service/dispatch.ts:32` — 공개 wire enum이 Core 값과 다른 별도 분류를 노출한다 — Core record kind는 1~13, operation kind는 1~11이다(`core/include/zlink/service/dispatch.h:34`, `core/include/zlink/service/dispatch.h:50`). addon은 이 값을 변환 없이 전달한다(`bindings/node/native/src/addon_mesh_service.cc:1110`, `bindings/node/native/src/addon_mesh_service.cc:1120`). 그러나 `ReceiveKind`는 Node/Spot/Actor=1/2/3이고 `OperationKind`는 Send/Request/Reply/Publish/ActorJoin/ActorLeave=1~6이다. 두 상수는 package public export이기도 하다(`bindings/node/src/zlink/contracts/service/index.ts:8`). 예를 들어 Core의 record kind 3은 `CHANNEL_SEND`인데 Node 상수에서는 `Actor`, operation kind 5는 `ACTOR_LOOKUP`인데 Node 상수에서는 `ActorJoin`으로 해석된다. 같은 family로 `MeshNodeState.Error=5`도 Core의 5=`DRAINING`, 7=`ERROR`와 충돌하고 STOPPED=6을 누락한다(`bindings/node/src/zlink/contracts/service/mesh_node.ts:20`, `core/include/zlink/service/mesh_node.h:24`) — Core와 byte-for-byte 같은 이름·값의 enum을 한 곳에서 정의하고 public record/status 타입도 그 값 타입을 사용한다.

- [I1][high] `bindings/node/native/src/addon_mesh_service.cc:846` — ready handler가 JS handler의 반환 mask를 Core에 전달하지 않고 모든 readable domain을 즉시 승인한다 — Core는 callback 반환값을 소비자가 drain 책임을 수락한 domain으로 정의하고, 거부한 domain만 다시 알린다(`core/doc/spec/core/service/02-dispatch.md:176`). native bridge는 JS 호출을 비동기 queue에 넣은 직후 항상 `ready_domains`를 반환한다(`bindings/node/native/src/addon_mesh_service.cc:852`). 따라서 TypeScript가 약속한 “return the mask to drain” 계약(`bindings/node/src/zlink/contracts/service/mesh_node.ts:201`)은 실행되지 않는다. 또한 Core가 보장하는 `NULL` unregister가 Node 표면에 없고, 등록 때 할당한 state/TSFN(`bindings/node/native/src/addon_mesh_service.cc:869`)을 교체·node destroy 때 해제하는 경로도 없다(`bindings/node/native/src/addon_mesh_service.cc:349`) — 동기 반환을 실제로 Core callback에 전달할 수 있는 bridge를 사용하거나, 비동기 Node 모델에 맞는 승인 계약을 별도 정식 설계한다. handler별 상태를 node에 귀속해 replace/unregister/destroy에서 정확히 한 번 해제한다.

- [I1][high] `bindings/node/native/src/addon_mesh_service.cc:1106` — pull receive가 `zlink_mesh_receive_record_t.kind_data`를 완전히 버린다 — Core record는 `kind_data`/`kind_data_size`를 갖고, actor lookup·actor join completion, send-ready, spot control, transfer control의 필수 구조를 이 필드로 전달한다(`core/include/zlink/service/dispatch.h:119`, `core/doc/spec/core/service/02-dispatch.md:153`). addon이 만드는 JS record와 raw/public TS 타입에는 해당 값이 없다(`bindings/node/src/zlink/runtime/native/binding_service_types.ts:117`, `bindings/node/src/zlink/contracts/service/dispatch.ts:62`). 그 결과 `lookupRemoteActor()`가 반환한 operation completion에서 `zlink_actor_location_t`를 얻을 수 없고 actor lifecycle·transfer control도 해석할 수 없다 — record kind별 versioned `kind_data`를 addon에서 안전한 JS 값으로 materialize하고 discriminated TS union으로 노출한다.

- [I1][high] `bindings/node/src/zlink/runtime/sockets/router_socket.ts:84` — public raw `RouterSocket.sendToSpot/requestToSpot/replyToSpot`가 native에 존재하지 않는 메서드를 호출한다 — TypeScript native interface에는 `routerSpotSend/Request/Reply`가 선언되어 있지만(`bindings/node/src/zlink/runtime/native/binding_socket.ts:39`), addon export table은 `routerRequest`, `routerReply`, recv 뒤 바로 monitor API로 넘어가며 세 export가 없다(`bindings/node/native/src/addon_exports.cc:71`). Core raw header에도 제거된 `zlink_router_*_spot_part` 대체 raw API가 없다. tsc는 `NativeBinding`의 선언만 확인하므로 manifest green이어도 실제 호출은 `undefined is not a function`으로 실패한다 — raw Router의 세 Spot 메서드를 public contract/runtime/native declaration에서 제거하고 Spot service API만 사용한다.

- [I1][high] `bindings/node/native/src/addon_mesh_service.cc:349` — service close/release 결과를 addon이 버려 TypeScript가 실패를 관찰하지 못하고 handle까지 잃는다 — Core의 MeshNode, publisher, ready batch, claim, receive batch, Spot, stream-session destroy/release는 모두 typed close result를 반환한다(예: `core/include/zlink/service/mesh_node.h:142`, `core/include/zlink/service/dispatch.h:166`). addon은 이 반환값을 검사하지 않는다(`bindings/node/native/src/addon_mesh_service.cc:814`, `bindings/node/native/src/addon_mesh_service.cc:1003`, `bindings/node/native/src/addon_mesh_service.cc:1841`). TypeScript는 성공했다고 간주해 `_native=null`로 만든다(예: `bindings/node/src/zlink/runtime/service/mesh_node.ts:91`, `bindings/node/src/zlink/runtime/service/spot.ts:150`) — 각 close result를 검사해 실패 시 errno와 함께 throw하고, 성공했을 때만 JS handle을 무효화한다.

- [I1][medium] `bindings/node/native/src/addon_mesh_service.cc:1086` — buffer-too-small 요구량의 JS runtime 타입이 선언과 다르다 — addon은 세 `size_t`를 BigInt로 생성하지만(`bindings/node/native/src/addon_mesh_service.cc:1090`), raw/public contract는 `number`로 선언한다(`bindings/node/src/zlink/runtime/native/binding_service_types.ts:110`, `bindings/node/src/zlink/contracts/service/dispatch.ts:55`). 호출자가 선언대로 이 값을 새 batch capacity로 넘기는 경로는 `number`를 기대하고 bitwise 변환까지 수행한다(`bindings/node/src/zlink/runtime/service/mesh_node.ts:447`) — 안전 범위를 검증한 Number로 통일하거나 public 타입과 batch 생성 인자를 모두 bigint로 통일한다.

- [I1][medium] `core/include/zlink/service/actor.h:214` — Core actor transfer 공개 계약이 Node addon·TS 표면에서 누락됐다 — Core는 prepare/commit/activate/abort 네 entrypoint와 token/result/control 구조를 공개하지만, 139-file scope에서 `transferPrepare`, `transferCommit`, `transferActivate`, `transferAbort`와 대응 native 호출은 no-hit이다. receive 측 transfer control도 위 `kind_data` 누락으로 해석할 수 없다 — Core actor transfer를 addon과 public TS에 typed API로 투영하고 transfer control record와 함께 contract test 대상에 넣는다.

### Evidence

- actor-join reply route 자체는 올바르다. public `replyActorJoin()`은 native `actorJoinReply`를 호출하고(`bindings/node/src/zlink/runtime/service/dispatch.ts:64`), addon은 generic reply가 아니라 `zlink_actor_join_reply()`를 호출한다(`bindings/node/native/src/addon_mesh_service.cc:1666`).
- ready batch → claim → receive batch의 기본 함수 매핑과 claim release 호출은 Core 시그니처와 일치한다(`bindings/node/native/src/addon_mesh_service.cc:933`, `bindings/node/native/src/addon_mesh_service.cc:981`, `bindings/node/native/src/addon_mesh_service.cc:1059`).
- sample helper가 별도로 정의한 record kind 1~13과 operation kind 1~11은 Core 값과 일치한다(`bindings/node/samples/sample_support.ts:18`). 이는 public enum 불일치를 해소하지 않으며 오히려 public 상수를 사용할 수 없음을 확인한다.
- manifest의 addon·tsc green은 확인했지만 prompt에 따라 재실행하지 않았다.

### Verdict

NOT CLEAN

## 3. I2 — POSD·DDD

### Findings

- [I2][high] `bindings/node/src/zlink/contracts/sockets/router_socket.ts:42` — raw Router 책임에 RouteMesh Spot 주소·generation 개념을 섞어 service 경계가 누출됐다 — Core 10.0.0은 raw socket과 Spot service를 별도 header와 handle로 구분한다. 그런데 Router public contract가 세 Spot 작업을 노출하고 runtime/native declaration까지 통과시킨다. 실제 addon 구현은 이미 없어 이 얕은 우회 표면만 남았다 — raw Router는 routing-id 기반 raw messaging만 소유하고 Spot routing은 `Spot`/`MeshNode` service module에만 둔다.

- [I2][medium] `bindings/node/src/zlink/contracts/service/dispatch.ts:32` — 하나의 wire 분류 지식을 public contract와 sample helper가 서로 다른 정의로 중복 소유한다 — public `ReceiveKind`/`OperationKind`와 sample의 `MeshRecordKind`/`MeshOperationKind`가 같은 raw 필드를 서로 다르게 설명하며, sample 주석은 public enum 대신 로컬 상수를 써야 한다고 명시한다(`bindings/node/samples/sample_support.ts:14`). 이는 정보 은닉 실패와 변경 증폭이다 — Core wire enum 투영을 service contract 한 모듈이 소유하고 samples와 runtime이 그 정의를 재사용한다.

- [I2][high] `bindings/node/native/src/addon_mesh_service.cc:827` — ready notification의 승인 의미와 callback 수명을 모듈이 흡수하지 못하고 동기 Core callback과 비동기 JS callback 사이에서 분리했다 — TS 표면은 단순한 반환-mask callback처럼 보이지만 실제 JS 결과는 폐기되고 native state는 소유자가 없다. 호출자는 이 차이를 알 방법도, unregister로 수명을 닫을 방법도 없다 — Node에 적합한 하나의 깊은 ready-dispatch abstraction이 승인, queueing, replace/unregister, shutdown을 함께 소유하도록 재설계한다.

### Evidence

- 새 service runtime의 일반 send/request/Spot/Actor/stream-session 호출은 대부분 public TS facade가 payload·routing-id 변환을 내부에 숨겨 호출자에게 raw C 구조를 노출하지 않는다.
- actor-join 전용 reply routing도 `ReceiveRecord.replyActorJoin()` 뒤에 숨겨져 있어 호출자가 C token 구조를 알 필요가 없다.

### Verdict

NOT CLEAN

## 4. I3 — 정리 완결성

### Findings

- [I3][high] `bindings/node/src/zlink/runtime/native/binding_socket.ts:39` — 정의 없는 `routerSpotReply/routerSpotRequest/routerSpotSend` native declaration과 이를 호출하는 public runtime이 남았다 — addon export·구현 no-hit이므로 세 메서드는 죽은 export 계약이며 I1 raw Router 파손과 같은 root-cause family다 — contract/runtime/native declaration을 함께 제거한다.

- [I3][medium] `bindings/node/src/zlink/runtime/native/binding_socket.ts:151` — 폐기된 SpotNode 이름의 `spotNodeActorBindRemoteSession` 선언이 유일하게 남았다 — addon export·구현과 scope 내 호출은 no-hit이라 죽은 선언이다 — 선언을 제거한다.

- [I3][low] `bindings/node/native/src/addon_message_values.h:77` — 제거 identifier `zlink_msg_gets`가 현재 코드 주석에 남았다 — 함수 호출은 없지만 prompt의 deprecated no-hit gate를 충족하지 못한다 — 현재 보장만 설명하도록 제거 symbol 이름 없는 주석으로 바꾼다.

- [I3][medium] `bindings/node/native/src/addon_spot_request_callbacks.cc:121` — push-style Spot request callback 잔재와 사용되지 않는 sync helper가 빌드 대상에 남았다 — `create_request_js_state`, `sync_request_callback`, `wait_sync_request`는 선언·정의 외 사용처가 없고 파일명·resource name도 폐기된 Spot request callback 책임을 유지한다. 실제 raw dealer/router request는 `create_core_request_js_state`와 trampoline만 사용한다(`bindings/node/native/src/addon_core.cc:2308`) — raw request callback 지원만 중립적인 raw module로 옮기고 미사용 Spot/sync helper를 삭제한다.

### Evidence

scope 한정 대소문자 무시 grep 결과:

| 폐기 개념·제거 symbol | 판정 |
|---|---|
| `SpotNode` | 1 hit: `binding_socket.ts:151` — 잔재 |
| `spot_node` | no-hit |
| `route_bridge` | no-hit |
| `subjects` | no-hit |
| `internal_sockets` | no-hit |
| pub/sub rid (`pub_rid`, `sub_rid`, `pubRid`, `subRid`) | no-hit |
| `dispatch_workers` | no-hit |
| `recv_actor_part` | no-hit |
| `msg_gets` | 1 hit: `addon_message_values.h:77` — 주석 잔재 |
| `zlink_subscribe_handler` | no-hit |
| `zlink_router_*_spot_part` | no-hit |

addon export table에서 RouteMesh service entry는 정의된 native 함수와 연결되어 있고 actor-join reply도 export되어 있다(`bindings/node/native/src/addon_exports.cc:121`). 위에 열거한 잔재 외 `route_bridge`, subjects/internal_sockets, pub·sub rid, dispatch_workers, recv_actor_part의 scoped hit는 없다.

### Verdict

NOT CLEAN

BINDINGS REVIEW NOT CLEAN
