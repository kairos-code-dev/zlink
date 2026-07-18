# .NET G0 공개 계약 coverage ledger

이 문서는 `.NET` G0 inventory와 조항별 coverage의 영구 증거다. 실행 시각과 명령 결과는
[`dotnet.ko.md`](dotnet.ko.md)에 기록하고, 이 문서는 현재 spec 문서, 공개 서명과 동작 검증의
연결만 유지한다.

## 1. 판정 규칙

- 공개 type과 member의 분모는 `framework/languages/dotnet/contract/api/*.api.txt` 여덟 파일의 `type`, `field`,
  `ctor`, `property`, `event`, `method`, `generic`, `where` 행 전체다.
- package 분모는 `framework/languages/dotnet/contract/packages/*.package.txt` 여덟 파일의 archive, metadata,
  dependency와 assembly 행 전체다.
- 동작 계약은 아래 표의 exact test method 또는 `E2E:<scenario-id>`로 연결한다. test class
  전체나 단순 solution PASS는 개별 동작의 증거로 사용하지 않는다.
- `E2E:<scenario-id>`는 `.NET` `feature-map.ko.md`에 실제로 존재해야 하며, 실행 성공 여부는
  G6 실행 ledger에서 별도로 닫는다.
- 같은 동작을 여러 문서에서 설명하면 같은 proof를 재사용할 수 있지만, 문서 inventory 행과
  규범 조항 행은 생략하지 않는다.
- 아래 §3과 §4의 파일 단위 행은 분모 확인용 inventory다. 한 파일에 proof가 하나 있다고 해서
  그 문서 전체가 covered인 것은 아니다. 규범 조항별 행과 exact proof가 모두 있어야 완료다.
- `GAP`과 `PARTIAL`은 구현 또는 검증 작업이 남았다는 뜻이다. 단순히 관련 test가 있다는 이유로
  `PROVEN`으로 올리지 않는다.

공개 symbol은 아래 표에 파일 하나로 묶어 요약하더라도 파일 단위로 판정하지 않는다. API snapshot의
각 `type`/`field`/`ctor`/`property`/`event`/`method` 행이 symbol 하나의 ledger 행이며, 바로 뒤의
`generic`/`where` 행은 해당 symbol의 제약 ledger다. 이 형식은 overload, nullable, default value와
custom modifier를 서로 다른 텍스트 행으로 보존한다. 같은 내용을 별도 표로 복사하면 snapshot과
서로 다르게 갱신될 수 있으므로 정식 API snapshot 자체를 symbol 단위 ledger로 사용한다.

## 2. 공개 surface와 bindings 입력

| ID | 분모 | 정식 artifact | 검증 |
|----|------|---------------|------|
| DN-SURFACE-001 | `Zlink.Framework` 전체 공개 서명 | `languages/dotnet/framework/languages/dotnet/contract/api/Zlink.Framework.api.txt` | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` |
| DN-SURFACE-002 | `Zlink.Framework.AspNetCore` 전체 공개 서명 | `languages/dotnet/framework/languages/dotnet/contract/api/Zlink.Framework.AspNetCore.api.txt` | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` |
| DN-SURFACE-003 | `Zlink.Framework.Codecs.MessagePack` 전체 공개 서명 | `languages/dotnet/framework/languages/dotnet/contract/api/Zlink.Framework.Codecs.MessagePack.api.txt` | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` |
| DN-SURFACE-004 | `Zlink.Framework.Codecs.Protobuf` 전체 공개 서명 | `languages/dotnet/framework/languages/dotnet/contract/api/Zlink.Framework.Codecs.Protobuf.api.txt` | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` |
| DN-SURFACE-005 | `Zlink.Framework.Locations.Redis` 전체 공개 서명 | `languages/dotnet/framework/languages/dotnet/contract/api/Zlink.Framework.Locations.Redis.api.txt` | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` |
| DN-SURFACE-006 | `Systems.Zlink.Stream.Connector` 전체 공개 서명 | `languages/dotnet/framework/languages/dotnet/contract/api/Systems.Zlink.Stream.Connector.api.txt` | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` |
| DN-SURFACE-007 | `Zlink.HttpClient` 전체 공개 서명 | `languages/dotnet/framework/languages/dotnet/contract/api/Zlink.HttpClient.api.txt` | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` |
| DN-SURFACE-008 | `Zlink.Framework.Contracts` 오류·codec 공개 서명 | `languages/dotnet/framework/languages/dotnet/contract/api/Zlink.Framework.Contracts.api.txt` | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` |
| DN-PACKAGE-001 | 위 여덟 package의 archive와 export | `languages/dotnet/framework/languages/dotnet/contract/packages/*.package.txt` | `scripts/verify_packaged_contract.sh` aggregate consumer, HTTP client 단독 consumer와 package snapshot |
| DN-BINDING-001 | framework가 사용하는 bindings package | `Systems.Zlink` `10.0.0`, local package `Systems.Zlink.10.0.0.nupkg`, SHA-256 `ed841dc7073d7106ce792ff8ef587d4dda9425961a33d1440530d2aaafa4f4a3` | `dotnet restore Zlink.Framework.sln --force-evaluate` 뒤 live project assets 208개가 모두 `Systems.Zlink/10.0.0`으로 해석됨; framework `PackageReference` 사용, source 직접 참조 없음 |
| DN-REUSE-001 | core/bindings와 framework의 동일 기능 재구현 | correlation generator만 중복으로 확정 | `DN-025`; connector의 단일 `ZlinkStreamCorrelation` 사용, framework duplicate symbol/file no-hit |

현재 snapshot의 symbol/constraint 행 수는 다음과 같다. 이 수는 고정 상수가 아니라 checkout에서
위 판정 규칙의 행을 다시 세어 얻은 값이다.

| artifact | symbol/constraint ledger 행 |
|----------|-----------------------------|
| `Systems.Zlink.Stream.Connector.api.txt` | 332 |
| `Zlink.Framework.AspNetCore.api.txt` | 5 |
| `Zlink.Framework.Codecs.MessagePack.api.txt` | 7 |
| `Zlink.Framework.Codecs.Protobuf.api.txt` | 7 |
| `Zlink.Framework.Contracts.api.txt` | 51 |
| `Zlink.Framework.Locations.Redis.api.txt` | 35 |
| `Zlink.Framework.api.txt` | 2,235 |
| `Zlink.HttpClient.api.txt` | 109 |

namespace별 정식 계약 owner는 다음과 같다. 한 symbol의 정확한 서명은 snapshot 행이 소유하고, 동작은
owner 문서의 `DN-COMMON-*`/`DN-DOC-*`와 plan §6의 exact test가 소유한다.

| namespace/type prefix | 정식 계약 owner |
|-----------------------|------------------|
| `Zlink.Framework.Contracts.Actors` | `handler-interfaces.ko.md`, `31-session-actor-dispatch.ko.md` |
| `Zlink.Framework.Contracts.Channels`, `Configuration` | `system-structure.ko.md`, `21-mesh-node.ko.md` |
| `Zlink.Framework.Contracts.Spots`, `Timers`, `Workers` | `system-structure.ko.md`, `handler-interfaces.ko.md` |
| `Zlink.Framework.Contracts.Streams` | `system-structure.ko.md`, `31-session-actor-dispatch.ko.md` |
| `Zlink.Framework.Contracts.Locations`, `Zlink.Framework.Locations.Redis` | `system-structure.ko.md`, `04-routing-id-allocation.ko.md` |
| `Zlink.Framework.Contracts.Dispatch`, `Eventing`, `Monitoring` | `system-structure.ko.md` |
| `Zlink.Framework.AspNetCore` | 언어 `README.ko.md`와 각 capability의 ASP.NET Core 계약 |
| `Zlink.Framework.Codecs.*` | `handler-interfaces.ko.md`의 codec extension 계약 |
| `Zlink.Framework.Contracts.Codecs`, `Errors` | `handler-interfaces.ko.md`의 codec extension 계약과 HTTP client 오류 모델 |
| `Zlink.HttpClient.*` | `http-client/languages/dotnet/dotnet-http-client.ko.md` |
| `Systems.Zlink.Stream.Connector.*` | `system-structure.ko.md`의 connector 절과 `handler-interfaces.ko.md` |

### 2.1 bindings public capability audit

framework 목표 계약에 필요한 low-level 기능은 현재 `Systems.Zlink 10.0.0` package의 public API로
충족한다. framework는 이 표의 기능을 backend wrapper에서 직접 호출하며 reflection이나 friend
assembly로 우회하지 않는다.

| 필요한 기능 | bindings public capability | framework owner와 exact proof | 판정 |
|-------------|----------------------------|--------------------------------|------|
| context와 socket 수명 | `Zlink.CreateContext`, context의 dealer/router/pub/sub 생성, 각 public `DisposeAsync` | `ZLinkDotNetBackendAdapterFactory`; backend factory/forced cleanup tests | 충족 |
| channel send/request/reply | public dealer/router `Send`, `Request`, `Recv`, `Reply`, `SendFlags.DontWait` | channel backend wrappers; Submit acceptance와 route codec tests | 충족 |
| endpoint와 routing option | public `Bind`, `Connect`, `Disconnect`, routing id/probe/mandatory/handover와 HWM option | `ZLinkBackend*Wrapper`; `ChannelRuntimeOptionsTests`, endpoint connection tests | 충족 |
| Spot/actor routing | public MeshNode/Spot 생성, route dispatch, actor create/destroy/send/request/join/transfer callback | `ZLinkBackendMeshNodeWrapper`; actor ownership/transfer/dispatch tests | 충족 |
| STREAM session transport | public stream node/session callback, typed `RoutingId`, send/reply/close | `ZLinkBackendStreamSocketWrapper`; typed ingress와 forced cleanup tests | 충족 |
| monitoring callback | public socket/Spot monitor event와 snapshot query | monitoring backend wrapper; monitoring mapper/source tests | 충족 |

framework production은 application handler와 attribute를 찾을 때 reflection을 사용하지만, bindings
assembly인 `Systems.Zlink`의 non-public member를 `NonPublic`, `MethodInfo.Invoke`, `FieldInfo.GetValue`
등으로 호출하지 않는다. bindings가 framework에 `InternalsVisibleTo`를 제공하는 우회도 없으며,
`Zlink.Framework.csproj`는 bindings source/project가 아니라 중앙에서 고정한 package를 참조한다.

## 3. 공통 spec coverage

| ID | 공통 spec | .NET projection/owner | exact proof |
|----|-----------|-----------------------|-------------|
| DN-COMMON-001 | `README.ko.md` | `.NET/README.ko.md`, 계약 문서 위치와 취소 표현 | `RegressionTests.DotNetContractReadme_Exposes_Resolvable_Regression_Evidence` |
| DN-COMMON-002 | `01-overview.ko.md` | `handler-interfaces.ko.md`, host/DI와 역할별 공개 표면 | `ScaffoldSmokeTests.FrameworkRoot_IsDiscoverable_FromTestRuntime`; `NodesAndServicesTests.AddZLinkFramework_Uses_Standard_DI_For_Application_Dependencies` |
| DN-COMMON-003 | `05-framework-api.ko.md` | `handler-interfaces.ko.md`, 일곱 public API snapshot | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature`; `ScaffoldSmokeTests.FrameworkExportedTypes_UseContractsNamespace` |
| DN-COMMON-004 | `02-interaction-model.ko.md` | channel, Spot, actor와 session interaction owner | `SerialExecutorTests.SerialExecutionQueue_DefaultAwait_Holds_Gate_Until_Work_Completes`; `E2E:SM-D11` |
| DN-COMMON-005 | `03-message-model.ko.md` | typed packet identity와 framework envelope codec | `EnvelopeCodecTests.Envelope_requires_marker_and_roundtrips_flow_fields`; `ContractSurfaceCoverage.Frozen_public_surface_excludes_replaced_contracts` |
| DN-COMMON-006 | `04-async-execution-policy.ko.md` | one-way bounded/nonblocking 수락과 `Async(...)` turn 유지, `Yield(...)` turn 반납 | `ZLinkAsyncSubmitterTests.Async_ThrowsWhenQueueIsFull`; `StreamConnectorTests.OneWaySubmit_Accepts_Into_A_Bounded_Queue_And_Rejects_Full_Synchronously`; `SessionActorCoordinatorTests.Session_Send_Submit_Rejects_Nonblocking_Transport_Failure_On_The_Caller`; `EntrySpotActorDispatchTests.BoundSession_Submit_Rejects_Missing_Binding_On_The_Caller`; `EntrySpotActorDispatchTests.Actor_Send_Submit_Rejects_Nonblocking_Transport_Failure_On_The_Caller`; `WorkerPoolTests.RunCpuWorker_Async_Holds_Serial_Turn_Until_Work_Completes`; `WorkerPoolTests.RunCpuWorker_Yield_Releases_And_Resumes_Through_Serial_Turn`; `E2E:ATD-A1`; `E2E:ATD-B3` |
| DN-COMMON-007 | `10-channel-topology.ko.md` | `system-structure.ko.md`, manual/location 연결 | `ChannelsTests.AddZLinkFramework_Throws_WhenClientHasNoPeerAcquisitionPath`; `E2E:RM-A1`; `E2E:RM-A2`; `E2E:PS-A1` |
| DN-COMMON-008 | `22-actor-model.ko.md` | `handler-interfaces.ko.md`, actor context와 join 결과 | `ActorContracts.Actor_context_creates_actors_and_joins_a_spot_by_routing_id`; `EntrySpotActorDispatchTests.EntrySpotActorDispatch_BoundRequest_UsesBoundSession_AndBindsSession`; `E2E:SM-B7` |
| DN-COMMON-009 | `23-spot-actor.ko.md` | `handler-interfaces.ko.md`, `system-structure.ko.md` | `EntrySpotActorDispatchTests.EntrySpotActorDispatch_ConcurrentActors_StartsOutsideEntrySpotSerialLine_AndKeepsSameActorOrdering`; `E2E:SM-B1`; `E2E:SM-B2` |
| DN-COMMON-010 | `31-session-actor-dispatch.ko.md` | `.NET/session-actor-dispatch.ko.md` | `EntrySpotActorDispatchTests.EntrySpotActorDispatch_NoBindRequest_RepliesViaNoBind_AndDoesNotBindSession`; `E2E:SM-D1`; `E2E:SM-D2`; `E2E:SM-D8` |
| DN-COMMON-011 | `24-spot-address-messaging.ko.md` | opaque `SpotHandle`, refresh와 route egress | `LocationResolverTests.Spot_Handle_Request_Refreshes_Once_After_Target_Not_Found`; `E2E:SM-C3`; `E2E:SM-G2` |
| DN-COMMON-012 | `40-location-runtime.ko.md` | `system-structure.ko.md`, row/watch/reconcile/query와 MeshName·ChannelName 매핑 | `LocationResolverTests.Spot_And_Actor_Handles_Use_Configured_Route_Channel_And_Refresh_Uses_The_Same_Map`; `LocationResolverTests.Spot_Handle_Request_Refreshes_Once_After_Target_Not_Found`; `LocationResolverTests.Watch_Upsert_Applies_The_Current_Row_And_Preserves_The_Route_Channel_Mapping`; `LocationResolverTests.Handle_Polling_Updates_Actor_Snapshot_When_Watch_Is_Unavailable`; `AutoConnectReconcilerTests.Reconcile_Connects_New_Targets_And_Disconnects_Vanished_Ones`; `LocationRuntimeTests.Shutdown_Removes_Owner_Lease_Then_Bulk_Removes_Rows`; `E2E:RM-A1`; `E2E:SF-A1` |
| DN-COMMON-013 | `41-location-store-redis.ko.md` | `Zlink.Framework.Locations.Redis`와 공통 store contract | `InMemoryLocationStoreTests.Mesh_List_Returns_Only_The_Requested_Mesh_Snapshot`; Redis test project 전체 store contract는 G3 실행 ledger에서 별도 증명 |
| DN-COMMON-014 | `52-message-flow-tracing.ko.md` | `system-structure.ko.md`, observer와 error surface | `MessageFlowTracerTests.Off_SuppressesAllTransitions`; `UnhandledDispatchPolicyTests.DispatchErrorReporter_DeliversMessageFlowErrorSnapshot`; `E2E:OBS-A1` |
| DN-COMMON-015 | `53-flow-correlation.ko.md` | channel/stream codec, async flow context와 connector | `FlowCorrelationTests.Connector_and_framework_share_one_monotonic_correlation_sequence`; `FlowCorrelationTests.Awaited_work_keeps_flow_but_detached_work_loses_the_expired_lease`; `E2E:OBS-A2` |
| DN-COMMON-016 | `51-runtime-metrics.ko.md` | framework `Meter`와 connector 계기 | `RuntimeMetricsTests.Meter_Catalog_Uses_Exact_Names_Kinds_Units_And_Scope`; `RuntimeMetricsTests.Inactive_Meter_Does_Not_Allocate_Or_Retain_Per_Event_State`; `StreamConnectorTests.TestMetricReaderRetainsOnlyABoundedSnapshot`; `E2E:OBS-B1` |
| DN-COMMON-017 | `54-graceful-drain-handoff.ko.md` | drain control, typed `Draining`, session closing | `DrainCoordinatorTests.Drain_Is_Idempotent_And_First_Deadline_Is_Fixed`; `DrainCoordinatorTests.Waiter_Cancellation_Does_Not_Cancel_Shared_Drain`; `StreamSessionForcedCleanupTests.Rejected_terminal_disposal_and_force_close_share_one_transport_close`; `E2E:OBS-C1`; `E2E:OBS-C4` |
| DN-COMMON-018 | `00-public-contract-governance.ko.md` | `handler-interfaces.ko.md` §17, fixed source/package snapshots | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature`; `PublicContractSnapshotTests.Renderer_Preserves_CSharp_PublicContract_Distinctions` |
| DN-COMMON-019 | `90-implementation-gap.ko.md` | plan §6 `DN-001`~`DN-025`와 이 ledger | `RegressionTests.DotNetContractRegressionTestReferences_Resolve_ToActiveTestMethods`; 각 DN 행의 exact proof는 plan과 실행 log에서 추적 |
| DN-COMMON-020 | `32-stream-connector.ko.md` | 대상 실행 환경, transport, wire 계약, 연결 생명주기, 배포 산출물 | `StreamConnectorTests.HeaderProtocolRejectsMissingMarkerAndInvalidFlowFields`; `StreamConnectorTests.HeaderProtocolEnforcesControlPacketContract`; `StreamConnectorTests.SessionClosingCodecDecodesTheVersionedClosedReason`; `StreamConnectorTests.TcpSendUsesHeaderPayloadFrame` |
| DN-COMMON-021 | `21-mesh-node.ko.md` | MeshNode 등록, Entry Spot bind 순서, SpotManager 생성·조회·종료 | `E2E:SM-A1`; `ScaffoldSmokeTests.PublicSurface_Removes_DirectRouteContracts_And_Exposes_ActorContracts` |
| DN-COMMON-022 | `25-stage-wrapper-on-spot.ko.md` | spot 실행 문맥 직렬화, timer, wrapper 책임 경계 | `E2E:SM-B7`; `E2E:SM-E3`; `E2E:SM-A5` |
| DN-COMMON-023 | `30-stream-session.ko.md` | 서버 session 표면, dispatch 모델, 등록 검증, 오류 경계 | `NodesAndServicesTests.AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleSessions`; `StreamSessionForcedCleanupTests.Stream_node_preserves_typed_routing_id_from_backend_callback`; `E2E:SM-D7`; `E2E:SM-D8` |
| DN-COMMON-024 | `20-spot-messaging.ko.md` | outbound 세 축, publish·subscribe, dispatch 실패 정책, route ingress, startup validation | `NodesAndServicesTests.AddZLinkFramework_Throws_WhenSpotFactoryTypeIsDuplicatedAcrossNodes`; `NodesAndServicesTests.AddZLinkFramework_AllowsStandaloneLocalSpotNode`; `E2E:SM-C4`; `E2E:SM-B7` |
| DN-COMMON-025 | `11-channel-messaging.ko.md` | channel runtime 수명, dispatch 실패 정책, startup validation, 종료 중 호출 | `NodesAndServicesTests.AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleSessions`; `E2E:OBS-B1` |
| DN-COMMON-026 | `50-runtime-monitoring.ko.md` | source 분리, event 종류, polling 규칙, monitoring startup validation | `CoverageCriticalRuntimeTests.SpotTimerFailureEventFactory_MapsStoppedAndContinuingFailures`; `RuntimeMetricsTests.Meter_Catalog_Uses_Exact_Names_Kinds_Units_And_Scope` |

## 4. .NET 정식 계약 문서 coverage

| ID | 정식 계약 문서 | production owner | exact proof |
|----|----------------|------------------|-------------|
| DN-DOC-001 | `README.ko.md` | 언어별 계약 index와 cancellation projection | `RegressionTests.DotNetContractDocuments_AllExposeRegressionTestSection` |
| DN-DOC-002 | `01-system-structure.ko.md` | ASP.NET Core host 등록·부트스트랩·DI·lifecycle — channel · SPOT · MeshNode/Entry Spot · STREAM · session actor dispatch · monitoring · location 등록 표면과 startup validation | `NodesAndServicesTests.AddZLinkFramework_Throws_WhenSpotFactoryTypeIsDuplicatedAcrossNodes`; `NodesAndServicesTests.AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleSessions`; `NodesAndServicesTests.AddZLinkFramework_AllowsStandaloneLocalSpotNode`; `E2E:SM-A1` |
| DN-DOC-003 | `02-handler-interfaces.ko.md` | 전체 public interface·context·handler·client·등록·timer·filter·attribute·관측 투영 카탈로그와 §17 공개 계약 산출물 검증 절차 | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature`; `PublicContractSnapshotTests.Renderer_Preserves_CSharp_PublicContract_Distinctions`; `CoverageCriticalRuntimeTests.SpotTimerFailureEventFactory_MapsStoppedAndContinuingFailures` |
| DN-DOC-004 | `03-stream-connector.ko.md` | 별도 client connector의 lifecycle, dispatch, codec, transport와 종료 사유 | `StreamConnectorTests.TcpSendUsesHeaderPayloadFrame`; `StreamConnectorTests.HeaderProtocolEnforcesControlPacketContract` |
| DN-DOC-005 | `04-routing-id-allocation.ko.md` | 네 builder의 자동 할당, 같은 store capability, bind 전 lifecycle과 준비된 결과 조회 | `NodesAndServicesTests.AddZLinkFramework_RegistersOneAllocatedRoutingIdCapabilityForAllBuilders`; `NodesAndServicesTests.HostStartup_AllocatesRoutingIdBeforeBindingAndPublishesReadyResult`; `RedisLocationStoreTests.RoutingIdSlotAllocation_IsAtomicIdempotentAndFenced` |
| DN-DOC-006 | `dotnet-http-client.ko.md` | standalone/server client 분리, callback·scheduler와 패키지 의존성 | `HttpClientContractTests.Standalone_and_server_request_surfaces_expose_only_valid_terminators`; `HttpClientContractTests.Callback_completion_enters_the_captured_execution_turn`; `HttpExecutionSchedulerTests.Captured_http_callback_is_posted_as_a_new_serial_turn` |
| DN-DOC-007 | `05-route-mesh.ko.md` | RouteMesh와 MeshNode builder, ChannelName membership, manual peer와 runtime option의 정확한 공개 서명 | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature`; `BuilderContracts.Mesh_and_stream_builders_declare_node_local_roles_and_channel_memberships` |
| DN-DOC-008 | `06-location-store.ko.md` | location capability, descriptor·location record, transfer authority와 Redis extension의 정확한 공개 서명 | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature`; `ContractSurfaceCoverage.Redis_Extension_Remains_A_Separate_Package_Without_A_Backend_Specific_Registration_API` |

`02-framework-interfaces.ko.md`는 상위 사용 모델 guide이므로 정식 interface 분모에서는 제외한다.
문서의 회귀 참조는 active unit test 또는 실제 `.NET` feature-map scenario로만 해석되며, 삭제된
E2E class allowlist는 존재하지 않는다.

## 5. 형식 ID가 있는 규범 조항의 현재 판정

이 표는 독립 read-only 검토에서 각 문장과 exact proof를 다시 대조한 결과다. 한 행에 여러
검증 의미가 있으면 그 의미를 모두 직접 증명해야 `PROVEN`이다.

| 범위 | PROVEN | GAP |
|------|--------|-----|
| `MFLOW-001`~`011` | 001~011 | 없음 |
| `MFLOW-EXT-001`~`015` | 001~015 | 없음 |
| `RMETRIC-001`~`017` | 001~017 | 없음 |
| `DRAIN-001`~`020` | 001~020 | 없음 |

아래 행은 형식 ID 분모를 기계적으로 고정한다. `PROVEN`은 현재 test project에
포함된 exact test method 또는 실제 `.NET` feature map의 scenario ID가 해당 조항의 관찰
가능한 결과를 직접 검증한다는 뜻이다. E2E의 현재 checkout 전체 실행 성공은 G6에서
다시 판정하지만, G0에서는 scenario가 feature map과 runner 구현에 모두 존재하는지를
확인한다.

| ID | 1차 판정 |
|----|----------|
| MFLOW-001 | PROVEN — `MessageFlowTracerTests.Off_SuppressesAllTransitions` |
| MFLOW-002 | PROVEN — `MessageFlowTracerTests.ErrorsOnly_EmitsDroppedButNotHealthyTransitions` |
| MFLOW-003 | PROVEN — `MessageFlowTracerTests.KeyTransitions_EmitsLifecycleKeyedByCorrelation` |
| MFLOW-004 | PROVEN — `MessageFlowTracerTests.Verbose_size_and_structured_hook_fields_follow_the_frozen_gates` |
| MFLOW-005 | PROVEN — `MessageFlowTracerTests.Sampling_Decision_Is_Stable_For_The_Whole_Flow`; `MessageFlowTracerTests.Dropped_And_Error_Bypass_Zero_Sample_Rate` |
| MFLOW-006 | PROVEN — `MessageFlowTracerTests.Observer_is_offloaded_bounded_and_failure_does_not_break_later_delivery` |
| MFLOW-007 | PROVEN — `UnhandledDispatchPolicyTests.Channel_Request_Dispatch_Emits_Received_Then_Replied_With_The_Wire_Identity`; `UnhandledDispatchPolicyTests.Route_Request_Dispatch_Emits_Received_Then_Replied_With_The_Wire_Identity`; `EntrySpotActorDispatchTests.Spot_Actor_Request_Ingress_Emits_Received_Then_Replied_With_The_Wire_Identity`; `StreamSessionForcedCleanupTests.Stream_Request_Emits_Received_Then_Replied_With_Wire_Correlation` |
| MFLOW-008 | PROVEN — `UnhandledDispatchPolicyTests.Channel_Request_Handler_Failure_Logs_The_Inbound_Flow_And_Correlation` |
| MFLOW-009 | PROVEN — `StreamFlowEndToEndTests.Connector_Request_Server_Log_Reply_And_Callback_Share_One_Flow`; `StreamWireInteropTests.Reply_header_echoes_request_correlation_and_root_flow` |
| MFLOW-010 | PROVEN — `MessageFlowTracerTests.Dedicated_file_is_structured_creates_parent_and_takes_precedence_over_app_logger`; `MessageFlowTracerTests.Dispatch_failure_uses_only_the_dedicated_file_and_the_fixed_phase_schema` |
| MFLOW-011 | PROVEN — `MessageFlowTracerTests.LiveMode_OverridesStaticAndTogglesAtRuntime`; `MessageFlowTracerTests.Off_gate_prevents_event_construction` |
| MFLOW-EXT-001 | PROVEN — `EntrySpotActorDispatchTests.Flowless_Actor_Stream_Ingress_Creates_One_Inbound_Flow_For_Join_And_Reply`; `E2E:OBS-A1`; `FlowCorrelationTests.Connector_outbound_flow_is_the_framework_actor_gateway_inbound_event_identity` |
| MFLOW-EXT-002 | PROVEN — `ActorTransferTests.Transfer_admission_commit_and_target_continuation_keep_one_root_flow`; `E2E:OBS-C2` |
| MFLOW-EXT-003 | PROVEN — `EntrySpotActorDispatchTests.Off_Host_Without_An_Ambient_Flow_Does_Not_Create_Actor_Wire_Flow`; `EntrySpotActorDispatchTests.Off_Host_Preserves_An_Inbound_Flow_On_The_Next_Actor_Outbound`; `E2E:OBS-A3` |
| MFLOW-EXT-004 | PROVEN — `StreamFlowEndToEndTests.Connector_Request_Server_Log_Reply_And_Callback_Share_One_Flow`; `E2E:OBS-A1` |
| MFLOW-EXT-005 | PROVEN — `MessageFlowTracerTests.Gateway_logger_prefers_the_explicit_factory_falls_back_and_keeps_Off_silent`; `MessageFlowTracerTests.Off_SuppressesAllTransitions` |
| MFLOW-EXT-006 | PROVEN — `MessageFlowTracerTests.Dedicated_file_is_structured_creates_parent_and_takes_precedence_over_app_logger`; `E2E:OBS-A1` |
| MFLOW-EXT-007 | PROVEN — `EnvelopeCodecTests.Envelope_requires_marker_and_roundtrips_flow_fields`; `StreamConnectorTests.HeaderProtocolRejectsUnknownFlag` |
| MFLOW-EXT-008 | PROVEN — `EntrySpotActorDispatchTests.Flowless_Actor_Stream_Ingress_Creates_One_Inbound_Flow_For_Join_And_Reply`; `StreamConnectorTests.InboundFlowIsReusedAndExpiresAfterCallbackScope`; `E2E:OBS-A3` |
| MFLOW-EXT-009 | PROVEN — `UnhandledDispatchPolicyTests.Channel_Request_Handler_Failure_Logs_The_Inbound_Flow_And_Correlation`; `E2E:OBS-A2` |
| MFLOW-EXT-010 | PROVEN — `UnhandledDispatchPolicyTests.SpotSubscription_Fanout_Instances_Keep_One_Flow_And_Owner_Skip_Uses_The_Same_Identity`; `E2E:OBS-A4` |
| MFLOW-EXT-011 | PROVEN — `FlowCorrelationTests.Lifecycle_entry_and_bound_push_preserve_the_root_flow`; `FlowCorrelationTests.Spot_timer_callback_starts_a_timer_root_and_restores_the_caller_context` |
| MFLOW-EXT-012 | PROVEN — `MessageFlowTracerTests.Sampling_Decision_Is_Stable_For_The_Whole_Flow`; `MessageFlowTracerTests.Dropped_And_Error_Bypass_Zero_Sample_Rate` |
| MFLOW-EXT-013 | PROVEN — `EntrySpotActorDispatchTests.Off_Host_Preserves_An_Inbound_Flow_On_The_Next_Actor_Outbound`; `E2E:OBS-A3` |
| MFLOW-EXT-014 | PROVEN — `FlowCorrelationTests.Awaited_work_keeps_flow_but_detached_work_loses_the_expired_lease`; `StreamConnectorTests.Callback_Outbound_Reuses_Inbound_Flow_And_Does_Not_Leak_To_The_Next_Callback` |
| MFLOW-EXT-015 | PROVEN — `StreamFlowEndToEndTests.Connector_Request_Server_Log_Reply_And_Callback_Share_One_Flow` |
| RMETRIC-001 | PROVEN — `RuntimeMetricsTests.Inactive_Meter_Does_Not_Allocate_Or_Retain_Per_Event_State`; `RuntimeMetricsTests.Listener_Failure_Does_Not_Change_Runtime_Result` |
| RMETRIC-002 | PROVEN — `E2E:OBS-B1` |
| RMETRIC-003 | PROVEN — `RuntimeMetricsTests.Spot_Queue_Wait_Samples_Support_A_Reader_Computed_P99` |
| RMETRIC-004 | PROVEN — `RuntimeMetricsTests.Actor_Transfer_Metric_Uses_The_Moving_Snapshot_Once`; `E2E:OBS-B2` |
| RMETRIC-005 | PROVEN — `RuntimeMetricsTests.Actor_Transfer_Metric_Uses_The_Moving_Snapshot_Once` |
| RMETRIC-006 | PROVEN — `RuntimeMetricsTests.Channel_Request_Metrics_Close_Inflight_And_Count_Only_Timeouts` |
| RMETRIC-007 | PROVEN — `RuntimeMetricsTests.Meter_Catalog_Uses_Exact_Names_Kinds_Units_And_Scope`; `E2E:OBS-B3` |
| RMETRIC-008 | PROVEN — `RuntimeMetricsTests.Observable_Metrics_Pull_Current_Source_State_After_Listener_Attaches`; `RuntimeMetricsTests.Listener_Failure_Does_Not_Change_Runtime_Result` |
| RMETRIC-009 | PROVEN — `RuntimeMetricsTests.Inactive_Histogram_Does_Not_Capture_A_Timestamp` |
| RMETRIC-010 | PROVEN — `RuntimeMetricsTests.Inactive_Meter_Does_Not_Allocate_Or_Retain_Per_Event_State`와 `E2E:OBS-B4`가 framework의 listener 미등록 경로를 검증한다. `StreamConnectorTests.TestMetricReaderRetainsOnlyABoundedSnapshot`는 collector를 계기 생성 전에 활성화하고 8,192개 event 뒤 최근 4,096개만 남는지 직접 검증한다. |
| RMETRIC-011 | PROVEN — `E2E:OBS-B3` |
| RMETRIC-012 | PROVEN — `RuntimeMetricsTests.Spot_Count_And_Lifecycle_Counters_Keep_Entry_And_User_Separate` |
| RMETRIC-013 | PROVEN — `RuntimeMetricsTests.Location_And_Dropped_Metrics_Use_Closed_Labels_And_Ignore_Listener_Failure` |
| RMETRIC-014 | PROVEN — `RuntimeMetricsTests.Actor_Transfer_Metric_Uses_The_Moving_Snapshot_Once`; `ActorHandoffTests.RequestHandoff_PreservesReplyRoutingFields`; `ActorHandoffTests.PendingAdmission_IsCorrelatedByHandoffId_AndEnforcesItsDeadline` |
| RMETRIC-015 | PROVEN — `MessageFlowTracerTests.Observer_is_offloaded_bounded_and_failure_does_not_break_later_delivery` |
| RMETRIC-016 | PROVEN — `StreamConnectorTests.AutomaticReconnectRecordsOneAttemptButInitialConnectRecordsNone` |
| RMETRIC-017 | PROVEN — `RuntimeMetricsTests.Meter_Catalog_Uses_Exact_Names_Kinds_Units_And_Scope` |
| DRAIN-001 | PROVEN — `AutoConnectReconcilerTests.Planner_Keeps_Draining_Peer_Connected`; `E2E:OBS-C1` |
| DRAIN-002 | PROVEN — `DrainCoordinatorTests.Draining_Gate_Rejects_Each_New_Public_Admission_With_The_Frozen_Error`; `DrainCoordinatorTests.New_Stream_Session_After_Drain_Is_Rejected_With_ServerDrain` |
| DRAIN-003 | PROVEN — `ActorTransferTests.Transfer_admission_commit_and_target_continuation_keep_one_root_flow`; `E2E:OBS-C2` |
| DRAIN-004 | PROVEN — `E2E:OBS-C3` |
| DRAIN-005 | PROVEN — `DrainCoordinatorTests.Drain_Does_Not_Complete_Until_The_InFlight_Executor_Completes` |
| DRAIN-006 | PROVEN — `DrainCoordinatorTests.Deadline_Expiry_Force_Stops_With_The_Frozen_Reason`; `DrainCoordinatorTests.Framework_Drain_Sends_ServerDrain_Before_Orderly_Stream_Close`; `E2E:OBS-C4` |
| DRAIN-007 | PROVEN — `DrainCoordinatorTests.Drain_Is_Idempotent_And_First_Deadline_Is_Fixed`; `DrainCoordinatorTests.Waiter_Cancellation_Does_Not_Cancel_Shared_Drain` |
| DRAIN-008 | PROVEN — `RuntimeMetricsTests.Forced_Drain_Records_The_Exact_Count_For_Each_Closed_Kind`; `E2E:OBS-C2`; `E2E:OBS-C3` |
| DRAIN-009 | PROVEN — `LocationRuntimeTests.Drain_Cleanup_Failure_Keeps_Lease_Heartbeat_Until_Retry_Succeeds`; `E2E:OBS-C1` |
| DRAIN-010 | PROVEN — `ActorHandoffTests.Drain_Rejects_New_Admission_But_Allows_An_Already_Accepted_Commit` |
| DRAIN-011 | PROVEN — `ActorTransferTests.Transfer_admission_commit_and_target_continuation_keep_one_root_flow`; `E2E:OBS-C2` |
| DRAIN-012 | PROVEN — `E2E:OBS-C5` |
| DRAIN-013 | PROVEN — `E2E:OBS-C1` |
| DRAIN-014 | PROVEN — `ActorTransferTests.Transfer_admission_commit_and_target_continuation_keep_one_root_flow`; `E2E:OBS-C2` |
| DRAIN-015 | PROVEN — `DrainCoordinatorTests.Drain_Is_Idempotent_And_First_Deadline_Is_Fixed` |
| DRAIN-016 | PROVEN — `LocationRuntimeTests.Drain_Cleanup_Removes_Every_Owner_Row_And_The_Owner_Lease`; `DrainCoordinatorTests.Forced_Drain_Cleans_Owner_Before_Stopping_The_Location_Runtime` |
| DRAIN-017 | PROVEN — `DrainCoordinatorTests.Framework_Drain_Sends_ServerDrain_Before_Orderly_Stream_Close`; `DrainCoordinatorTests.Deadline_Expiry_Force_Stops_With_The_Frozen_Reason`; `E2E:OBS-C4` |
| DRAIN-018 | PROVEN — `StreamWireInteropTests.SessionClosingServerDrainPayload_DecodesInConnector`; `E2E:OBS-C4` |
| DRAIN-019 | PROVEN — `DrainCoordinatorTests.Drain_Executor_Publishes_Marker_And_Serving_Weight_Before_Propagation_And_Sealing`; `DrainCoordinatorTests.Drain_Executor_Waits_For_Weight_Propagation_Before_Sealing_Admission`; `DrainCoordinatorTests.Host_Stop_Uses_The_Same_Thirty_Second_Default_Deadline`; `E2E:RM-B2` |
| DRAIN-020 | PROVEN — `DrainCoordinatorTests.Marker_Store_Failure_Retries_Until_Deadline_And_Skips_Later_Phases`; `DrainCoordinatorTests.Executor_Reported_Marker_Publish_Failure_Is_The_Exact_ForceStopped_Reason` |

### 5.1 형식 ID 밖에서 발견된 미검증 규범

| ID | spec 위치 | 아직 필요한 exact proof | 상태 |
|----|-----------|-------------------------|------|
| DN-G0-MFLOW-001 | `message-flow-tracing` §3~§7 | observer snapshot 수명, non-inline offload, bounded overflow와 overflow 계기 | PROVEN — `MessageFlowTracerTests.Observer_is_offloaded_bounded_and_failure_does_not_break_later_delivery`; `UnhandledDispatchPolicyTests.SpotActorSendMissingHandler_LogsAndReportsMessageFlowDroppedEvent` |
| DN-G0-MFLOW-002 | `message-flow-tracing` §4~§5 | off hot path 이벤트 미생성, logger 구조화 필드, 파일 전용 출력과 부모 디렉터리 생성 | PROVEN — `MessageFlowTracerTests.Off_gate_prevents_event_construction`; `MessageFlowTracerTests.Production_flow_logger_uses_the_fixed_dispatch_category`; `MessageFlowTracerTests.Dedicated_file_is_structured_creates_parent_and_takes_precedence_over_app_logger`; `MessageFlowTracerTests.Dispatch_failure_uses_only_the_dedicated_file_and_the_fixed_phase_schema` |
| DN-G0-MFLOW-003 | `message-flow-tracing` §6~§9 | backend 중립성, 전체 hook/phase/direction/source 의미, correlation 생성 형식 | PROVEN — 여섯 fixed API/package snapshot의 backend-specific export/dependency 부재; `UnhandledDispatchPolicyTests.Channel_Request_Dispatch_Emits_Received_Then_Replied_With_The_Wire_Identity`; `UnhandledDispatchPolicyTests.Route_Request_Dispatch_Emits_Received_Then_Replied_With_The_Wire_Identity`; `EntrySpotActorDispatchTests.Current_Spot_Publish_Emits_Sent_With_Spot_Rid_And_Current_Flow`; `RouteCodecTests.Route_Send_Uses_Monotonic_Correlation_And_Logs_Target_As_SourceRid`; `FlowCorrelationTests.Connector_and_framework_share_one_monotonic_correlation_sequence` |
| DN-G0-FLOW-001 | `flow-correlation` §3~§9 | transfer/bound push, lifecycle origin, owner-skip, gateway 기본 sink 우선순위와 구조화 join | PROVEN — `ActorTransferTests.Transfer_admission_commit_and_target_continuation_keep_one_root_flow`; `FlowCorrelationTests.Lifecycle_entry_and_bound_push_preserve_the_root_flow`; `UnhandledDispatchPolicyTests.SpotSubscription_Fanout_Instances_Keep_One_Flow_And_Owner_Skip_Uses_The_Same_Identity`; `MessageFlowTracerTests.Actor_gateway_runtime_construction_uses_explicit_factory_fallback_and_Off_gate`; `E2E:OBS-A1`; `E2E:OBS-A4` |
| DN-G0-FLOW-002 | `flow-correlation` §10 | legacy decoder·corr-only compatibility·별도 public flow builder/runtime control 부재 | PROVEN — `EnvelopeCodecTests.Envelope_requires_marker_and_roundtrips_flow_fields`; `StreamConnectorTests.HeaderProtocolRejectsMissingMarkerAndInvalidFlowFields`; `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` |
| DN-G0-METRIC-001 | `runtime-metrics` §4~§5 | dropped 닫힌 label, 동적 topic label 생략, session bind 구간 | PROVEN — `RuntimeMetricsTests.Location_And_Dropped_Metrics_Use_Closed_Labels_And_Ignore_Listener_Failure`; `RuntimeMetricsTests.Fanout_Without_A_Declared_Topic_Omits_The_Topic_Label`; `RuntimeMetricsTests.Session_Bind_Duration_Records_One_Completed_Interval` |
| DN-G0-METRIC-002 | `runtime-metrics` §6 | 특정 backend/SDK와 별도 public provider API 비의존 | PROVEN — `ContractSurfaceCoverage.Redis_Extension_Remains_A_Separate_Package_Without_A_Backend_Specific_Registration_API`와 public API snapshot |
| DN-G0-REDIS-001 | `location-store-redis` §1~§3 | 별도 package/전용 등록 부재, 모든 write의 단일 Lua 결정, topology/hash-tag 정책 | PROVEN — `ContractSurfaceCoverage.Redis_Extension_Remains_A_Separate_Package_Without_A_Backend_Specific_Registration_API`; `RedisLocationStoreTests.Every_Write_Operation_Has_One_Atomic_Lua_Script`; `RedisLocationStoreTests.Hash_Tagged_Prefix_Keeps_Every_Derived_Key_In_The_Same_Cluster_Slot` |
| DN-G0-REDIS-002 | `location-store-redis` §5~§7 | 실제 stamp 유실 수렴, host connection dispose, 실행별 prefix 정리 | PROVEN — `RedisLocationStoreTests.Deleted_Change_Stamp_Falls_Back_To_The_Intact_Full_Row_Snapshot`, `NodesAndServicesTests.AddLocationStore_Instance_Is_Disposed_Exactly_Once_By_The_Host_Provider`, `RedisLocationStoreTests.Dedicated_Run_Prefix_Cleanup_Removes_Every_Derived_Key` |
| DN-G0-DRAIN-001 | `graceful-drain-handoff` §3~§5 | 모든 placement 제외, fixed propagation bound, 7단계 순서, surface별 거부와 redirect 부재 | PROVEN — `AutoConnectReconcilerTests.Draining_Marker_Is_Monotonic_Across_Subsequent_Renewal`; `DrainCoordinatorTests.Drain_Executor_Publishes_Marker_And_Serving_Weight_Before_Propagation_And_Sealing`; `DrainCoordinatorTests.Drain_Executor_Waits_For_Weight_Propagation_Before_Sealing_Admission`; `DrainCoordinatorTests.Draining_Gate_Rejects_Each_New_Public_Admission_With_The_Frozen_Error`; `DrainCoordinatorTests.Drain_Is_Idempotent_And_First_Deadline_Is_Fixed`; `ContractSurfaceCoverage.Frozen_public_surface_excludes_replaced_contracts`; `E2E:OBS-C1`; `E2E:RM-B2` |
| DN-G0-DRAIN-002 | `graceful-drain-handoff` §5~§9 | queue drain 전 row release 금지, adapter 미등록 actor, host 30초/종료 순서, drain event 무등록 | PROVEN — `DrainCoordinatorTests.ReleaseAndRecreate_Waits_For_Spot_Queue_Close_Before_Row_Release`; `ActorHandoffTests.Unregistered_Transfer_Adapter_Uses_The_Frozen_Empty_State`; `DrainCoordinatorTests.Host_Stop_Uses_The_Same_Thirty_Second_Default_Deadline`; `DrainCoordinatorTests.Default_Drain_Uses_Thirty_Seconds_Without_Event_Registration`; `DrainCoordinatorTests.Framework_Drain_Sends_ServerDrain_Before_Orderly_Stream_Close` |
| DN-G0-SPOTNODE-001 | `dotnet/route-mesh` §1~§4 | Entry Spot 설정 독립성, Entry RID-before-bind와 전체 적용 순서 | PROVEN — `EntrySpotActorDispatchTests.EntrySpot_Configuration_Is_Independent_And_RoutingId_Is_Applied_Before_Bind` |
| DN-G0-SPOTNODE-002 | `dotnet/route-mesh` §4, `dotnet/location-store` | actor의 Entry RID, handle 내부 owner/kind 보존과 유효한 location row 기준 | PROVEN — `EntrySpotActorDispatchTests.Actor_Creation_Observes_The_Configured_EntrySpot_RoutingId`; `LocationResolverTests.Actor_Handle_Internal_Snapshot_Preserves_Entry_Owner_And_Kind`; plan `DN-057` |
| DN-G0-SPOTPUBLISH-001 | `dotnet/handler-interfaces` §5.3, `dotnet/aspnet-core-spot` 외부 publisher 계약 | `IZLinkSpotPublisherClient.PublishSpot(channelName, topic, message)` exact member와 외부 Spot channel 발행 | RESOLVED — 구현과 fixed API snapshot을 `PublishSpot`으로 교체; `SpotContracts.Spot_clients_separate_local_spot_api_routed_egress_and_publisher_channels`; `EntrySpotActorDispatchTests.External_Spot_Publish_Emits_Internal_Publisher_Rid_Without_Correlation`; `E2E:SM-C4` |
| DN-G0-BLOCKER-001 | `dotnet/location-store` | 언어별 문서의 존재하지 않는 core `ResolveSpot` 요구를 제거하고 공통 location runtime의 유효한 store row 기준으로 통일 | RESOLVED — plan `DN-057` |
| DN-G0-BLOCKER-002 | `flow-correlation` §8 | `FlowOrigin`을 nullable로 교체해 flow/origin optional pair를 정확히 표현 | RESOLVED — plan `DN-058` |

새 GAP이 발견되면 §6 구현 ledger에 개별 작업과 exact test로 연결한다. 여러 의미를 한 test
이름만으로 덮지 않으며, frozen public interface 변경이 필요한 경우 구현하지 않고 계약 blocker로
분리한다.

## 6. 아직 남은 G0 판정

- public API와 package export 분모: 최종 checkout package verifier와 clean consumer 재검증 완료
- bindings package/version/hash: `Systems.Zlink 10.0.0`과 package SHA-256 `ed841dc7073d7106ce792ff8ef587d4dda9425961a33d1440530d2aaafa4f4a3` 재검증 완료
- core/bindings 기능 재사용 감사: 1차 완료, G4/G7 재감사 필요
- 공통 spec 및 언어별 문서 파일 inventory: 완료
- 모든 규범 문장의 조항별 symbol/behavior/test 연결: 완료
- 남은 GAP의 §6 작업 행과 실패 test 등록: 현재 남은 GAP 없음
- 문서 회귀 proof의 active test/scenario 해석: validator 전체 15 PASS
- E2E proof의 실제 실행 성공: G6 scenario 181개와 aggregate runner PASS

## 7. 검토 대상 문서 snapshot

아래 SHA-256은 조항 검토가 어느 문서 내용에 대해 수행됐는지 고정한다. hash 일치는 coverage
증거가 아니며, 문서가 바뀌면 기존 조항 판정을 재사용하지 못하게 하는 무효화 장치다.

```text
3538efc2f99956660089ef35470501418c811bf79dbd12537f9d6053a6d6f194 00-public-contract-governance.ko.md
3f22511ba4d4fdf2dabc4225265be1d4b71a6b15071b130af7b5c589c9d21dbb 01-overview.ko.md
b3f25e1761b392cee06784e2a37ebf612545ebd51c6e66ef044b17060e797083 02-interaction-model.ko.md
df4c982868b0d367465dd060a8bd83e811696a1d0bb8da3d9614de486566359a 03-message-model.ko.md
39acd0f880523fff718c5bde27266aef6dbf2b338e37fa0d6274fee09785b88d 04-async-execution-policy.ko.md
6258b0d81689a313af2e034ca182f4c697592663e6dc8d09576f046a20219f2a 05-framework-api.ko.md
7db19ead495e6484672060d6593dc01495e16358eb56595e5ed9598979e44ca3 90-implementation-gap.ko.md
40bbce9a6a54e06bc8592e97177923909c27187e0cf52bc1bc4cf120e2d9a9a3 README.ko.md
2e8d5b168a7a1dce897c0372c16ff2a4f594d2972a24648abeffd211c3cef738 http-client/languages/dotnet/dotnet-http-client.ko.md
fb9042b2da236685a8d333a5f6b5cc86555a4f1068a6b134fa3c32dadc288fdd server/10-channel-topology.ko.md
39a003be40b2c513985cfbe357079dc935f44040ff9bb5b0d5e964703e22143c server/11-channel-messaging.ko.md
b9ce9a018495bfeda445b3c8f8f2f15798a83b337955baa61875086e9bda59b4 server/20-spot-messaging.ko.md
2f48cb256c74e8c1de47d899237c472db615ddd177473f91c27aa4c28d2a7bc2 server/21-mesh-node.ko.md
e85257bf576d4e59efa75807a2b8b4a730a9a119efa1216e38e1c08d9553c798 server/22-actor-model.ko.md
9783fa31ddce92cc82c36cda6947a5e3702812a6b0bfe10f0c3e7b43b29c7c86 server/23-spot-actor.ko.md
bc9314ecbb5a02bbbfea3355195aec878143989e2d0c204e1b0a73a755f74b8e server/24-spot-address-messaging.ko.md
c227e228b91f5887d94539be2c9954b33d1ee411d8d7e6f2bd5be9427f02dd53 server/25-stage-wrapper-on-spot.ko.md
5f516c76cf255f20693c04ec76199261623daba73aa8e0c010f42ae9fbec24f5 server/30-stream-session.ko.md
90b10fbe258905a1e8483689aca6499e8eb2141ba434f02547df6d134a24321e server/31-session-actor-dispatch.ko.md
e62cd34ad7fc3ba3ed48c604b086a9bda42433d61d2fcf8742ce49b584032c92 server/40-location-runtime.ko.md
100f2ae2bb9cac87a0be80b9551a4c3934a42bec4ebdda260c345a57fe8b30a7 server/41-location-store-redis.ko.md
d174572a644354576565ac1fbb4dca320afa4eb5e073a052c7013630b76e332d server/50-runtime-monitoring.ko.md
3e12588cdf9aab655a39823e90ddcbd22b5867bcaead9fa093e554dc484656c0 server/51-runtime-metrics.ko.md
2dab5abcdb783e2b16aace2020850958ac4bc4b64b919a494d39267306906ad2 server/52-message-flow-tracing.ko.md
89b668fcd8ccc710323cd3a3548d8f19626f7b288b790fc4dc01987f66e4c0c4 server/53-flow-correlation.ko.md
a34114446de62d9a0680d60d04e41f0c18e1e53b93a95e782bd399288e8516ff server/54-graceful-drain-handoff.ko.md
4516ef5c5956431330c1dbfe0fdae69d7840395c7803c1b8cf3b14f9b262de22 server/languages/dotnet/01-system-structure.ko.md
9679a2c6966a4b8aea19c497d6127ece4468d6cc255944f6d009f087e60f6ba9 server/languages/dotnet/02-handler-interfaces.ko.md
a32d6fe58906d3f2eab0db51be80e039554f39a9daaf0fd234f2d0087948fd5c server/languages/dotnet/04-routing-id-allocation.ko.md
2b45cd8f7aa8e5bc6a0051fb4e3e609b86eaf6f299ce7be6eca186025956ceee server/languages/dotnet/05-route-mesh.ko.md
9359fe0c29b6c8323e634b9923e44496f46b0b93e539b8f3d1e590cf49d893b9 server/languages/dotnet/06-location-store.ko.md
9012c074d6b625ea7c83b07500cd6a4cba7e91f01b1569b1addd7f968a25d845 server/languages/dotnet/README.ko.md
a33b6c5683b6af15025c9e74ee73719d8ddea68fbefd2c6455471482a7c523e9 stream-connector/32-stream-connector.ko.md
c7aa4466622ff8b992a2e58315ea963c9071ea6a8db272d96057414e8bc6b5cf stream-connector/languages/dotnet/03-stream-connector.ko.md
```
