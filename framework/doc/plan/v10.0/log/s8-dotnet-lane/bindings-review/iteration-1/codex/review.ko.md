# S8 DOTNET bindings 전환 리뷰 — R1 Codex — iteration 1

> 주: 리뷰 실행 시 Codex workspace가 read-only여서 리뷰어가 직접 파일을 쓰지 못했다.
> 아래 내용은 리뷰어가 반환한 review.ko.md 전문을 coordinator가 그대로 보존한 것이다.

## 1. Scope 확인

- 시작: 206 files
- 시작 aggregate SHA-256: `c9e0aef9e4d386a058282d611f76892530ffe190d1a7f076b4040597f7f9a66b`
- 종료: 206 files
- 종료 aggregate SHA-256: `c9e0aef9e4d386a058282d611f76892530ffe190d1a7f076b4040597f7f9a66b`
- `tests/Zlink.Tests`는 제외했다.
- build, test, sanitizer, package 생성은 수행하지 않았다.
- coordinator의 library 및 18 sample project build 성공 증거만 인정했다.
- 검토 중 scope 파일은 수정하지 않았다.

## 2. I1 계약 구현 일치

### Findings

- `[I1][blocker] bindings/dotnet/src/Zlink/Contracts/Service/IMeshNode.cs:12 — MeshNode의 필수 routing ID를 설정할 공개 표면이 없어 Core 계약에 따라 Start가 성공할 수 없다 — Core는 start 전에 routing ID, bind endpoint, 하나 이상의 ChannelName을 요구한다(core/doc/spec/core/service/01-mesh-node.md:166-172). IMeshNode는 읽기 전용 RoutingId, SetBind, AddChannel만 제공한다(IMeshNode.cs:12-28). 여러 actor sample은 channel도 추가하지 않고 즉시 Start한다(ActorGatewayRelay/Program.cs:12-14, ActorQueueExample/Program.cs:13-15) — IMeshNode에 Core 공통 routing-ID 설정을 연결하고 모든 sample이 고유 routing ID, bind, channel을 start 전에 설정하도록 수정한다.`
- `[I1][blocker] bindings/dotnet/src/Zlink/Runtime/Service/MeshDispatchRuntime.cs:110 — caller-init output 구조체를 size/version 0으로 전달하여 pull receive와 주요 query가 EINVAL로 실패한다 — 공통 계약은 caller-init output마다 struct_size와 version을 미리 설정하고 배열의 모든 element도 초기화하도록 요구한다(core/doc/spec/core/service/README.md:31-44). MeshClaim.Receive는 ZlinkMeshReceiveRequirements를 out으로 전달해 0으로 초기화한다(MeshDispatchRuntime.cs:110-111). MeshNode.Status, Spot.Status, ActorLookup, StreamSessionService.Status도 out 구조체를 그대로 전달한다(MeshNode.cs:173-178, Spot.cs:22-27, MeshNode.Actors.cs:43-48, StreamSessionService.cs:36-41). NativeSnapshotReader는 peer/binding 배열을 할당하지만 element의 size/version을 설정하지 않는다(NativeSnapshotReader.cs:30-37) — 모든 caller-init 구조체를 ref로 전달하고 호출 전에 현재 구조체 크기와 version 1을 설정한다. 배열도 각 element를 초기화한다.`
- `[I1][high] bindings/dotnet/src/Zlink/Contracts/Service/MeshDispatch.cs:230 — actor join admission을 일반 zlink_mesh_reply로 처리하며 join 결과와 typed control data를 노출하지 않는다 — Core는 join 응답에 zlink_actor_join_reply와 Accepted/Rejected 결과를 요구한다(core/include/zlink/service/actor.h:172-177, core/doc/spec/core/service/04-actor.md:221-248). MeshReceiveRecord.Reply는 항상 zlink_mesh_reply를 호출한다(MeshDispatch.cs:230-243). 전용 P/Invoke는 선언만 있고 사용되지 않는다(NativeMethods.Actor.cs:51-53). SampleSupport도 SpotControl을 일반 Reply로 승인하려 한다(SampleSupport.cs:162-170) — actor control record와 join result를 typed public API로 노출하고 전용 join-reply 경로를 사용한다.`
- `[I1][high] core/include/zlink/service/actor.h:214 — Actor transfer 공개 C API 전체가 .NET binding에서 누락됐다 — Core는 prepare, commit, activate, abort 네 함수를 공개한다(actor.h:214-226). scoped symbol 비교에서 bindings/dotnet/src와 samples에는 네 함수 모두 선언·호출이 없다 — transfer value types, P/Invoke, public operation surface와 typed transfer-control records를 추가한다.`
- `[I1][high] bindings/dotnet/src/Zlink/Runtime/Sockets/StreamSocket.cs:20 — IStreamSocket.SetRoutingId가 native routing ID를 설정하지 않고 managed field만 변경한다 — socket API는 zlink_set_routing_id로 handle의 identity를 설정한다(core/include/zlink/socket/api.h:136-139). 다른 routed socket은 Kernel option으로 native 값을 설정하지만(RouterSocket.cs:44-51), StreamSocket은 _routingId = routingId만 수행한다(StreamSocket.cs:20-28) — zlink_set_routing_id/zlink_get_routing_id에 연결하고 local cache를 계약의 기준으로 사용하지 않는다.`
- `[I1][medium] bindings/dotnet/src/Zlink/Runtime/Service/MeshDispatchRuntime.cs:12 — ready batch, receive batch, claim이 unmanaged ownership을 직접 보유하지만 세 타입 모두 finalizer 또는 SafeHandle 보호가 없다 — MeshReadyBatch와 MeshReceiveBatch는 IntPtr을 Dispose에서만 해제하고(MeshDispatchRuntime.cs:12-79, 134-215), MeshClaim 역시 Dispose에서만 claim을 release한다(MeshDispatchRuntime.cs:92-126). 누락된 Dispose는 claim을 계속 점유해 owner application dispatch 진행을 막을 수 있다 — SafeHandle 또는 신뢰할 수 있는 finalizer 경로를 사용하고 release 결과 및 disposed state를 일관되게 관리한다.`
- `[I1][medium] bindings/dotnet/src/Zlink/Runtime/Errors/BoundaryValidation.cs:9 — 유효한 256~511 byte Mesh endpoint를 binding이 거부한다 — Core의 ZLINK_MESH_ENDPOINT_MAX는 511이다(core/include/zlink/service/mesh_node.h:20). SetBind와 ConnectPeer는 공용 255-byte validator를 사용한다(MeshNode.cs:30-35, 71-75; BoundaryValidation.cs:9-22) — endpoint 전용 511-byte validator를 사용한다.`

### Verdict

NOT CLEAN

## 3. I2 POSD·DDD 리팩터링

### Findings

- `[I2][high] bindings/dotnet/src/Zlink/Contracts/Sockets/IStreamSocket.cs:51 — raw STREAM socket이 제거된 직접 actor bind/unbind/send/list 책임을 계속 공개한다 — Core 10.0.0에서는 raw STREAM이 transport session만 소유하고 IStreamSessionService가 MeshNode-Actor binding과 transfer barrier를 소유한다(core/doc/spec/core/service/05-stream-session.md:7-15). IStreamSocket은 BindActor, UnbindActor, SendBoundActor, BoundActors를 공개하며(IStreamSocket.cs:51-76), StreamActorInterop은 제거된 raw-stream P/Invoke를 직접 호출한다(StreamActorInterop.cs:54-85). samples도 이 오래된 표면을 표준 사용법으로 사용한다(ActorGatewayRelay/Program.cs:42-64) — raw STREAM에서 actor 책임을 제거하고 MeshNode.CreateStreamSessionService를 통한 단일 책임 경계로 samples와 구현을 전환한다.`
- `[I2][medium] bindings/dotnet/src/Zlink/Runtime/Service/MeshDispatchRuntime.cs:218 — 모든 dispatch kind를 sentinel field가 많은 하나의 MeshReceiveRecord로 평탄화하면서 Core의 typed kind_data를 폐기한다 — native record에는 KindData와 KindDataSize가 있지만(NativeMeshModels.cs:138-161), Convert는 이를 읽지 않고 일반 필드만 복사한다(MeshDispatchRuntime.cs:218-253) — 공통 envelope 아래 record-kind별 typed payload를 제공하고 Core-owned view의 batch lifetime을 wrapper 내부에서 관리한다.`

### Verdict

NOT CLEAN

## 4. I3 정리 완결성

### Findings

- `[I3][blocker] bindings/dotnet/src/Zlink/Runtime/Native/NativeMethods.Core.cs:8 — Core 10.0.0에서 제거된 symbol이 필수 export 목록, P/Invoke, public API와 native model에 대량 잔존하여 현재 Core library를 로드할 수 없다 — RequiredExportNames에는 zlink_msg_gets, raw STREAM actor API, spot_node, route_bridge 등 제거된 symbol이 포함된다(NativeMethods.Core.cs:37, 55-99). NativeLibraryLoader는 하나라도 없으면 DllNotFoundException을 던진다(NativeLibraryLoader.cs:147-159). zlink_msg_gets는 실제 P/Invoke와 public Message.GetProperty에도 남아 있다(NativeMethods.Core.cs:218-220, Message.Native.cs:141-147, Message.cs:301-303). RouteBridge native 구조체도 남아 있다(NativeTypes.cs:72-86) — RequiredExportNames를 현재 Core 10.0.0 export로 다시 생성하고, 정의 없는 P/Invoke·public forwarder·legacy raw STREAM actor 표면·RouteBridge model을 제거한다.`

### Verdict

NOT CLEAN

## 5. 폐기 개념 no-hit 판정

검사 범위 `bindings/dotnet/src`, `bindings/dotnet/samples` (native/obj/bin 제외).

| 개념 | 결과 | 근거 |
|---|---:|---|
| `SpotNode` | PASS, 0건 | scoped grep no-hit |
| `RouteBridge` | FAIL, 2건 | `NativeTypes.cs:72`, `:81` |
| `spot_node` | FAIL, 25건 | `NativeMethods.Core.cs:59-63`, `76-80`, `83-99`, `111-112` |
| `subjects` | PASS, 0건 | no-hit |
| `internal_sockets` | PASS, 0건 | no-hit |
| pub/sub routing ID | FAIL, 2건 | `NativeMethods.Core.cs:79-80` |
| `dispatch_workers` | PASS, 0건 | no-hit |
| `recv_actor_part` | PASS, 0건 | no-hit |
| `msg_gets` | FAIL, 3건 | `NativeMethods.Core.cs:37`, `:219`, `Message.Native.cs:144` |

BINDINGS REVIEW NOT CLEAN
