# S8-DOTNET lane — .NET(C#) bindings 10.0.0 전환 설계

RouteMesh 10.0.0: SpotNode/route-bridge/PUB-SUB-plane/push-dispatch → MeshNode + pull dispatch
(ready-index/claim/receive-batch/reply-token) + spot/actor/stream_session. **Runtime raw-socket
레이어는 생존**(raw PUB/SUB `zlink_publish_part`·`zlink_subscribe_part`·`zlink_set_subscription`·
`zlink_xpub_recv_part` 등은 Core 10 `socket/api.h`에 존속) — 전환 대상은 **Service 레이어 + 폐기 P/Invoke**.

## 규모
- `Contracts/Service/` ≈ 1,819줄 15파일, `Runtime/Service/` ≈ 6,443줄 37파일.
- P/Invoke: `Runtime/Native/NativeMethods.{SpotNode,Actor,SpotRouteBridge,PubSub,Core,Spot,Socket}.cs`.
- 최대 파일: `ActorOperationsImpl.cs`(606), `Spot.cs`(463), `SpotNode.ActorRuntime.cs`(384),
  `SpotRouteBridge.cs`(355, **삭제**).

## 개명 (SpotNode→MeshNode)
- `ISpotNode`(+`ISpotNodeConfiguration/Peers/Spots/Topology`) → `IMeshNode`.
- `SpotNodeStatus`·`SpotNodePeerEntry`·`SpotNodePeerFilter`·`SpotNodeState`·`SpotPeerSource/Kind/State`·
  `SpotKind`·`SpotRole` → `MeshNode*`. `ISpotNodePublisher` → `IMeshNodePublisher`
  (`zlink_mesh_node_publisher_new/publish/destroy`).
- Runtime `SpotNode.*.cs`(`zlink_spot_node_*`) → mesh_node, `createSpotNode`→`createMeshNode`.

## 삭제 (10.0.0 등가물 없음)
- Route bridge: `ISpotRouteBridge`·`SpotRouteBridgeOptions`·`EndpointOptions`·`EndpointCapabilities`,
  `SpotRouteBridge.cs`(contracts+runtime), `CreateRouteBridge()` factory.
- pub/sub routing-id·pub bind: `SetPublisherRoutingId`/`SetSubscriberRoutingId`, pub-bind config.
- subjects: `SubjectKind`·`SpotNodeSubjectEntry`·`SubjectFilter`·`SpotDispatchSubjectKind`,
  status의 `SubjectCount`/`ReadySubjectCount`, `NativeMethods.SpotNode.cs` `_subjects`.
- internal sockets: `SpotNodeSocketEntry`·`SocketFilter`·`SocketOwner`·`ISpotNodeTopology.InternalSockets`,
  `_internal_sockets`.
- dispatch workers: `DispatchWorkersMin/Max`(`SpotNode.OptionsRuntime.cs`).
- spot-level actor 열거: `ISpotNodeActors`·`SpotNodeActorEntry`·`_actors`.
- message property: `zlink_msg_gets`(`Message.Native.cs:144`, `NativeMethods.Core.cs`), `zlink_subscribe_handler`.

## 재작성 (모델 역전)
- 수신 push→pull: `ActorRecvInfo`·`ActorReceived`·`SpotDispatchInfo`·`SpotDispatchEvent` →
  pull batch receive record + `mesh_reply` reply token. `ActorInterop.cs`의 `_actor_recv_part` → receive batch.
- 송신: `(parts,count)` + `zlink_mesh_operation_id_t`. `Spot.MultipartSubmit.cs`·`SpotOperationsImpl.cs`.
- **신규 `stream_session` C# 타입 필요**(현재 없음): `ActorBindOperation`/`ActorUnbindOperation` +
  `bind_remote_session`/`close_bound_session`/`forward_bound_session_part` → `zlink_stream_session_service_*`.
- spot publish/subscribe: `zlink_spot_publish_part_utf`·`subscribe_part_buffer`·`recv_subscription_event`
  → `zlink_spot_publish`·`spot_set/unset_subscription`.

## dotnet 고유 배선 (cpp 문서에 없음)
- `Zlink.csproj <Version>` 9.0.8 → 10.0.0.
- `native/linux-x64/libzlink.so` 심링크가 `.so.9`를 가리킴 → `.so.10`으로 repoint(빌드 편의, 커밋 대상 아님).
- `NativeMethods.cs:7` `LibraryName="zlink"` (base name resolve, 버전 무관).

## samples (`samples/`, ~1 삭제 + ~12 재작성)
- 삭제: `SpotChannelExample`(route bridge).
- 재작성: `SpotRequestAsync`·`SpotPubSubExample`·`SpotRecv`·`SpotRpcExample`·`SpotTimerExample` +
  actor 6개(`ActorGatewayRelay`·`ActorQueueExample`·`ActorRoomExample`·`ActorRoomServer`·
  `ActorSequentialExample`·`ActorSinglePlayerQueue`) + `SampleCommon/SampleSupport.cs`.
- 생존(raw socket): `PubSubRecv`·`DealerRouterRecv`·`MonitorRecv`·`PairRecv`·`RequestReplyAsync`·
  `StreamRecv`·`StreamPacketCallback`.

## 순서
cpp lane이 mesh_node_t/pull/stream_session C++ 패턴을 확정한 뒤 그 계약 형태를 미러. 군: 1)P/Invoke
retarget(`NativeMethods.*`) 2)Contracts 개명·삭제 3)Runtime mesh_node/spot/actor 재작성 + stream_session
신설 4)samples 5)csproj 버전·심링크 6)`dotnet build` green → smoke → bindings 리뷰 campaign.
