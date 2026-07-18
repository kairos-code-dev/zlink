# S8-DN framework 실행 blueprint (Stage-A 조사 산출, atomic assembly)

Stage-A 에이전트가 검증: `Zlink.Framework` assembly는 **atomic**(seam push 메서드 제거 시 모든 consumer가
동시에 깨지고, 새 pump가 착지하기 전엔 어느 것도 green 불가). 부분 재작성=200+ 에러(baseline 18보다 악화).
→ 전체를 한 번에 compile-green까지 실행. baseline: 10.0.0 bindings 대상 18 CS0246(seam 국소).

## 결정적 아키텍처: push→pull을 Backend seam wrapper 안에 격리
framework는 bindings를 `Runtime/Backend/` seam으로만 소비하고, 실제 결합은 framework-owned seam
인터페이스(ActorHandoff·AutoConnectPlanner·AsyncSubmitter·Timer는 seam-agnostic 순수 로직, 무변경 컴파일).
→ seam의 framework-facing shape는 유지하고, wrapper 내부에 **단일 node-level DrainReady pump**를 두어
record를 per-owner 큐로 팬아웃, 기존 seam `Recv*`/`OnDispatchEvent`가 드레인. ~30 Spots/Channels/Host
dispatch 파일 재배선 회피, 관찰 동작 보존.

## Pump/scheduler 설계 (S8-06)
- node wrapper가 `IMeshNode.SetReadyHandler(domains⇒signal)` 등록; 단일 백그라운드 루프가
  `DrainReady(All, readyBatch)`를 residue 소진까지(infra-domain 우선) 반복.
- 각 `MeshReadyRecord`: `TakeClaim(i)`→`claim.Receive(receiveBatch)` 루프→record별 `Kind` 디스패치→
  **finally에서 claim release**(leak-free; IDisposable+finalizer backstop).
- OwnerKind 키드 스케줄링: Node/Spot(SpotRid)/Actor(ActorRef)별 serial executor(기존
  `ZLinkSpotActivationExecution.QueueSerialized` 미러)로 per-owner 순서 보존.
- record→framework-shape 어댑터(bridge):
  - NodeSend/ChannelSend/SpotSend/SpotMulticast→`Received`(RetainMessage)→RecvRoute/구독 드레인
  - *Request→Reply 토큰을 MeshOperationId 키 테이블에 저장→seam Reply/ReplyActorJoin이
    MeshReceiveRecord.Reply/ReplyJoin로 라우팅
  - SpotControl+ActorJoin→`ZLinkBackendActorJoinRequest`(RecvActorJoin, Accept/Reject)
  - SpotControl lifecycle→`ZLinkBackendSpotActorLifecycleEvent`(RecvActorLifecycle)
  - ActorSend/ActorRequest→`ZLinkBackendActorPart` 배치→actor inbound
  - Completion→매칭 MeshOperationId의 request-completion 콜백 해소(구 builder `.Submit(callback)` 대체)
  - SendReady→per-owner OnSendReady 콜백(ZLinkAsyncSubmitter 재시도 모델 유지)
  - TransferControl→transfer 상태기계 구동

## 편집 파일셋 (정밀)
- seam contracts: `Runtime/Backend/Contracts/{IZLinkBackendSpotContracts,IZLinkSpotBackendAdapter,
  IZLinkBackendObjects,IZLinkBackendSocketContracts}.cs`
- wrappers/adapters/mappings: `Runtime/Backend/DotNet/{Wrappers/ZLinkBackendSpotNodeWrapper,
  Wrappers/ZLinkBackendSpotWrapper,Wrappers/ZLinkBackendStreamSocketWrapper,Adapters/
  ZLinkDotNetBackendAdapters,Mappings/ZLinkDotNetBackendMappings}.cs`; **삭제**
  `Wrappers/ZLinkBackendSpotRouteBridgeWrapper.cs`; **신규** pump/scheduler 모듈.
- Spots: ZLinkSpotNodeInitializer, ZLinkSpotNodeRuntime, ZLinkSpotPeerConnector,
  ZLinkSpotNativeDispatchRouter, ZLinkEntrySpotDispatchPump, ZLinkSpotActivationDispatcher/Execution,
  ZLinkSpotActorJoinDispatcher, ZLinkSpotOutboundTransport, ZLinkSpotMonitoringSnapshotProvider,
  ZLinkSpotNodeCatalog/BundleRegistry.
- Channels/Host: ZLinkRouteChannelInitializer/Runtime, ZLinkRouteReceivePump, ZLinkChannelReceiveLoop,
  ZLinkChannelRuntimeManager, ZLinkFrameworkRuntime(.cs/.ChannelOptions/Spots/Actors),
  ZLinkFrameworkRuntimeState, ZLinkActorBoundSessionCoordinator, ZLinkActorRemoteJoiner,
  ZLinkChannelRuntimeOptionsService (+ IZLinkChannelRuntimeOptions→IZLinkRouteMeshRuntimeOptions,
  공개 계약 — parity 정책 준수).
- Streams: ZLinkManagedStream, ZLinkStreamNodeRuntime, ZLinkStreamSessionRuntime/Table,
  ZLinkNativeActorStreamBinding, ZLinkSessionActorCoordinator, ZLinkBoundActorRelaySender.
- Actors: ZLinkActorSessionLocationOwnership, ZLinkActorCreationCoordinator,
  ZLinkActorEntrySpotJoinCoordinator (seam call-site 시그니처만; 상태기계 무변경).

## 실행 순서 (assembly atomic — 마지막에만 0 에러)
pump/scheduler+seam wrappers → Spots dispatch 어댑터 → Channels/Host RouteBridge 제거 → Streams
session-service → actor-transfer stub. 각 서브시스템 후 rebuild, 카운트는 마지막에만 0.

## adapt-to-compile vs feature-upgrade(동작 보존 후 후속)
- 기계적: seam send/request(+metadata,out MeshOperationId), Spots dispatch(pump 공급), Locations
  planner(ZLinkSpotPeerConnector만 ConnectPeer→IMeshNode.ConnectPeer[ulong intent]/DisconnectPeer),
  Messaging(주입 delegate만 SubmitResult), Configuration(SpotNodeMode→AddChannel/SetChannelWeight,
  SetRouterBind→SetBind, pub-bind/pub-sub-rid 제거). **S8-06B timer 이미 Task.Delay — 무변경.**
- feature-upgrade(지금은 컴파일만, 동작 보존): S8-04A Redis transfer authority(handoff 상태기계 유지,
  seam에 PrepareActorTransfer/Commit/Activate/Abort+TransferControl 배선; CAS/lease/crash-recovery는
  후속), S8-06A Streams metadata/session-service(bound-actor 메서드가 IStreamSocket→IStreamSessionService로
  이동됨; ~6 Streams 파일 재배선, ReadOnlyMemory<byte> mutation-snapshot/1024/relay-allowlist).
- 제거(S8-08): RouteBridge 전부(RouteChannelInitializer:95, RouteChannelRuntime attach/TrySend/
  TryRequest/DrainSpotRouteBridges, RouteReceivePump.HandleRouterReceived, FrameworkRuntimeState.
  SpotRouteBridges)→AddChannel+SendToChannel/RequestToChannel+pump channel records. 3 수신 루프→단일 pump.

## 빌드 하네스
bindings를 로컬 nupkg feed로 pack: `/tmp/zlink-localfeed/Systems.Zlink.10.0.0.nupkg`,
`framework/languages/dotnet/nuget.config`가 이를 가리킴. native `libzlink.so→.so.10` 심링크.
빌드: `dotnet build src/Zlink.Framework/Zlink.Framework.csproj -c Release -p:ZLinkBindingsPackageVersion=10.0.0`

## 위험
- IZLinkChannelRuntimeOptions 공개 계약 rename(CLAUDE.md parity). OnSendReady no-hit vs AsyncSubmitter
  재시도 모델. CreateActor IActor→ActorRef(_ownedActors 처분 부기 제거, DestroyActor(ActorRef)).
  Completion↔MeshOperationId request-completion 라우팅이 가장 까다로움(구 builder .Submit(callback) 대체).
