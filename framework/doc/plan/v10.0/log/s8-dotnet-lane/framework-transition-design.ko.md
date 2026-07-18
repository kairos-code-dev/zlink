# S8-DN framework 단계 전환 설계 — .NET RouteMesh framework (참조 lane)

dotnet bindings CLEAN(게이트 통과) 후 framework 단계(§12 S8-01~S8-18). framework src ~48.5k줄/354파일.
읽기 전용 조사(`log/adf941dd` 산출) 기반. 정본 spec: `framework/doc/framework/spec/server/{21-mesh-node,
40-location-runtime,41-location-store-redis,05-route-mesh}` + `.../languages/dotnet/` exact interface.
target 표면: `bindings/dotnet/src/Zlink/Contracts/Service/{IMeshNode,MeshDispatch,ActorTransfer,
IStreamSessionService,MeshRecordPayloads,MeshNodeModels}.cs`.

## 0. 핵심 통찰
1. **단일 seam**: framework는 bindings를 `Runtime/Backend/`(22파일 2,322줄)으로만 소비. 실제 구 API 결합은
   국소적(ISpotNode=1·CreateSpotNode=4·CreateRouteBridge=4). "SpotNode 382+143"는 대부분 framework
   내부 타입명(cosmetic rename, 기능 blocker 아님).
2. **신규 API 채택 0**: IMeshNode/CreateMeshNode/DrainReady/SetReadyHandler/MeshReadyBatch/MeshClaim/
   IZLinkRouteMeshRuntimeOptions/IZLinkMeshPeerConnections/IStreamSessionService = framework src에 0 hit.
   seam 뒤 greenfield 채택.
3. **dispatch 모델 역전이 최대 작업**: 구=push(OnSendReady + framework 수신 루프 ZLinkRouteReceivePump·
   ZLinkEntrySpotDispatchPump·ZLinkChannelReceiveLoop). 신=pull(SetReadyHandler+DrainReady+MeshClaim/
   ReceiveBatch/Reply). 재아키텍처(S8-06).

## 1. 서브시스템 분류 (transition site)
| 서브시스템 | 경로 | 파일/줄 | 구 API | S8 task |
|---|---|---|---|---|
| **Backend seam** | `Runtime/Backend/` | 22/2,322 | ISpotNode·ISpotRouteBridge·CreateSpotNode·CreateRouteBridge·OnSendReady·SetRouterBind/PubBind | **S8-05/06/03** (IMeshNode 재정의) |
| Spots | `Runtime/Spots/` | 61/9,720 | SpotNode init/dispatch·EntrySpotDispatchPump·actor router | S8-03/06/06B |
| Actors | `Runtime/Actors/` | 27/5,663 | framework 자체 handoff(ZLinkActorHandoff·TransferRegistry·HandoffAdmissions, native transfer 없이) | **S8-04A** (native ActorTransfer*+Redis authority로 교체) |
| Locations | `Runtime/Locations/` | 31/5,611 | auto-connect planner/reconciler/loop·in-memory store·lease | **S8-04** (descriptor+connection planner)/04B |
| Channels | `Runtime/Channels/` | 23/3,566 | RouteBridge·RouteReceivePump·ChannelReceiveLoop·ChannelRuntimeOptionsService | S8-02/02A/05/08 |
| Host | `Runtime/Host/` | 16/4,162 | runtime 배선·RouteBridge·drain/session coordinator | S8-05/06/08 |
| Streams(S/S) | `Runtime/Streams/` | 33/3,894 | STREAM node/session·MessageMetadataPolicy | **S8-06A**→IStreamSessionService |
| Configuration/DI | `Runtime/Configuration/` | 19/2,582 | AddSpotMesh/AddRouterAuto/Manual·UseInMemoryLocationStores·SpotNodeBuilders | S8-02/02A/08 |
| Handlers | `Runtime/Handlers/` | 13/1,257 | handler scan/dispatch/registry | S8-03 |
| Messaging | `Runtime/Messaging/` | 20/2,118 | envelope/codec/submit queue | S8-05/06A |
| Timers | `Runtime/Timers/` | 1/~365 | ZLinkTimer(Core timer FFI) | S8-06B (Task.Delay) |
| Contracts(public) | `Contracts/` | 53/3,453 | IZLinkChannelRuntimeOptions·IZLinkEndpointConnections·UseInMemoryLocationStores·location store | S8-02A/04/08 |

## 2. 기계적 remap vs 신규 build
### (a) 기계적 remap (rename+시그니처 스왑)
- S8-02/02A AddRouteMesh+ChannelName+IZLinkRouteMeshRuntimeOptions: 빌더 plumbing 존재, AddChannel/
  SetChannelWeight로 re-point. live-socket weight 기계 이미 존재.
- S8-05 send: 구 wrapper Send/Request → IMeshNode.SendToNode/RequestToNode/SendToChannel/SendToActor/
  RequestToActor(+`ReadOnlyMemory<byte> metadata`, `out MeshOperationId`).
- S8-03 handler/Spot/Actor 등록: CreateSpot/EntrySpot/GetOrCreateSpot/CreateActor 직접 대응.
- S8-06B timer: ZLinkTimer(Core FFI)→Task.Delay+keyed scheduler(~365줄, 자기완결).
- S8-07 Logical Multicast+NoDrop: config flag remap(NoDrop 이미 ApplyRoleConfig에).

### (b) 신규 feature build (대형)
- **S8-06 ready/claim pump (최대)**: push→pull 역전. 3개 수신 루프 폐기→단일 DrainReady pump(infra-first
  drain + Node/Spot/Actor keyed scheduler + MeshClaim/ReceiveBatch/Reply/ReplyJoin claim 수명, leak 0).
- **S8-04 location descriptor+connection planner (대형)**: 통합 descriptor(MeshName scope·expected-RID
  pin·lifecycle generation·descriptor revision·source merge·ready index) → Redis 자동 discovery와 manual
  IZLinkMeshPeerConnections(계약 미존재, 신설)가 같은 admission 공유.
- **S8-04A Redis Actor transfer authority (대형)**: 구 framework 자체 handoff 삭제→native ActorTransfer*
  (prepare/commit/activate/abort) + Redis authority(participant-set CAS·token·lease·prepared/commit/abort
  crash recovery). Redis lease/CAS/commit 기계는 상당 부분 존재(재매핑).
- **S8-06A S/S metadata**: Streams 33파일+MetadataPolicy를 신 `ReadOnlyMemory<byte>` 규칙(mutation
  snapshot·immutable handler view·1024 경계·relay allowlist·reply 비자동복사)+IStreamSessionService로 재배선.

## 3. Redis/location (S8-04/04A/04B)
- location store 계약 견고(`Contracts/Locations/Stores.cs`: IZLinkLocationStore 5역할 조합, write-status
  race, RemoveAllByOwnerAsync) — S8-04B "backend 무명명·all-or-nothing"에 근접.
- Redis extension(`src/Zlink.Framework.Locations.Redis/`, 11파일 1,607줄): lease/token/CAS/commit Lua
  기계 다수 존재(1,064 lease·131 commit). storage primitive는 대부분 있음 — native transfer authority
  semantics로 재매핑이 과제(from scratch 아님).
- in-memory store가 현재 production 경로(UseInMemoryLocationStores) → test-only화(S8-04B: 미등록 시
  분산 조회 startup failure).

## 4. 제거 대상 (S8-08)
- RouteBridge 전부(BackendSpotRouteBridgeWrapper·IZLinkBackendSpotRouteBridge·CreateRouteBridge·10 호출처)
  → IMeshNode.AddChannel으로 흡수.
- UseInMemoryLocationStores production 경로(Builders.cs:250·ZLinkFrameworkOptionsBuilder.cs:105·validator)
  → test-only helper(테스트 ~26곳 이관).
- IZLinkChannelRuntimeOptions → IZLinkRouteMeshRuntimeOptions.
- framework 자체 handoff(ZLinkActorHandoff*·TransferRegistry) → native ActorTransfer*.
- push-ready(OnSendReady + 3 수신 루프) → DrainReady pump.

## 5. samples/E2E/tests (S8-09/10)
- samples 8앱 구 API(Bingo·DeliveryDispatch·ZoneWorld·TicTacToe·GameQuest·SupportChat·ShoppingMall).
- e2e 15파일 구 API(SpotService 14 등). `e2e/run_e2e_all.sh`.
- tests 21파일 구 API(UseInMemoryLocationStores ~20×). Redis.Tests·SampleRegressionTests.
- 그 외 AspNetCore·Stream.Connector 2파일.

## 6. 구현 순서 (제안)
1. **Backend seam 재정의**(`Runtime/Backend`): IZLinkBackendSpotNode→IZLinkBackendMeshNode(IMeshNode 기반:
   add-channel·connect-peer·pull-dispatch). 이후 대부분 downstream rename+call-site 시그니처(metadata·
   MeshOperationId).
2. 대형 build 순서: **S8-06 pump/scheduler → S8-04 planner/descriptor → S8-04A native transfer+Redis
   authority → S8-06A S/S metadata**(각자 test matrix). S8-02/02A/03/05/06B/07은 그 위에 remap.
3. S8-08 제거 → S8-09 samples → S8-10 E2E → S8-13/14/15 리뷰 campaign(DOTNET REVIEW CLEAN) → S8-11
   package 종료검증 → S8-17/18 internals 확정.

## 7. 처리 방침
framework는 bindings급 대형 단계다. seam 집중 + 대형 build 4개(pump·planner·transfer·metadata)를 단계적
구현(각 격리 에이전트, 컴파일 green), coordinator 통합, DOTNET REVIEW CLEAN campaign(§13/14/15)로 검증.
dotnet framework 확정 표면이 cpp/jvm/node framework의 참조가 된다.
