# .NET G0 공개 계약 coverage ledger

이 문서는 `.NET` G0 inventory와 조항별 coverage의 영구 증거다. 실행 시각과 명령 결과는
[`dotnet.ko.md`](dotnet.ko.md)에 기록하고, 이 문서는 현재 spec 문서, 공개 서명과 동작 검증의
연결만 유지한다.

## 1. 판정 규칙

- 공개 type과 member의 분모는 `framework/languages/dotnet/contract/api/*.api.txt` 여섯 파일의 `type`, `field`,
  `ctor`, `property`, `event`, `method`, `generic`, `where` 행 전체다.
- package 분모는 `framework/languages/dotnet/contract/packages/*.package.txt` 여섯 파일의 archive, metadata,
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
| DN-PACKAGE-001 | 위 여섯 package의 archive와 export | `languages/dotnet/framework/languages/dotnet/contract/packages/*.package.txt` | `scripts/verify_packaged_contract.sh` clean consumer와 package snapshot |
| DN-BINDING-001 | framework가 사용하는 bindings package | `Systems.Zlink` `8.6.6`, local package `Systems.Zlink.8.6.6.nupkg`, SHA-256 `dae37c26458d965cc63114d1fe846b129208feff815dc0a22ddd5f904854f84e` | `dotnet list src/Zlink.Framework/Zlink.Framework.csproj package --include-transitive` resolved `8.6.6`; framework `PackageReference` 사용, source 직접 참조 없음 |
| DN-REUSE-001 | core/bindings와 framework의 동일 기능 재구현 | correlation generator만 중복으로 확정 | `DN-025`; connector의 단일 `ZlinkStreamCorrelation` 사용, framework duplicate symbol/file no-hit |

현재 snapshot의 symbol/constraint 행 수는 다음과 같다. 이 수는 고정 상수가 아니라 checkout에서
위 판정 규칙의 행을 다시 세어 얻은 값이다.

| artifact | symbol/constraint ledger 행 |
|----------|-----------------------------|
| `Systems.Zlink.Stream.Connector.api.txt` | 301 |
| `Zlink.Framework.AspNetCore.api.txt` | 4 |
| `Zlink.Framework.Codecs.MessagePack.api.txt` | 7 |
| `Zlink.Framework.Codecs.Protobuf.api.txt` | 7 |
| `Zlink.Framework.Locations.Redis.api.txt` | 32 |
| `Zlink.Framework.api.txt` | 2,126 |

namespace별 정식 계약 owner는 다음과 같다. 한 symbol의 정확한 서명은 snapshot 행이 소유하고, 동작은
owner 문서의 `DN-COMMON-*`/`DN-DOC-*`와 plan §6의 exact test가 소유한다.

| namespace/type prefix | 정식 계약 owner |
|-----------------------|------------------|
| `Zlink.Framework.Contracts.Actors` | `handler-interfaces.ko.md`, `31-session-actor-dispatch.ko.md` |
| `Zlink.Framework.Contracts.Channels`, `Configuration` | `system-structure.ko.md`, `21-spot-node.ko.md` |
| `Zlink.Framework.Contracts.Spots`, `Timers`, `Workers` | `system-structure.ko.md`, `handler-interfaces.ko.md` |
| `Zlink.Framework.Contracts.Streams` | `system-structure.ko.md`, `31-session-actor-dispatch.ko.md` |
| `Zlink.Framework.Contracts.Locations`, `Zlink.Framework.Locations.Redis` | `system-structure.ko.md` |
| `Zlink.Framework.Contracts.Dispatch`, `Eventing`, `Monitoring` | `system-structure.ko.md` |
| `Zlink.Framework.AspNetCore` | 언어 `README.ko.md`와 각 capability의 ASP.NET Core 계약 |
| `Zlink.Framework.Codecs.*` | `handler-interfaces.ko.md`의 codec extension 계약 |
| `Systems.Zlink.Stream.Connector.*` | `system-structure.ko.md`의 connector 절과 `handler-interfaces.ko.md` |

### 2.1 bindings public capability audit

framework 목표 계약에 필요한 low-level 기능은 현재 `Systems.Zlink 8.6.6` package의 public API로
충족한다. framework는 이 표의 기능을 backend wrapper에서 직접 호출하며 reflection이나 friend
assembly로 우회하지 않는다.

| 필요한 기능 | bindings public capability | framework owner와 exact proof | 판정 |
|-------------|----------------------------|--------------------------------|------|
| context와 socket 수명 | `Zlink.CreateContext`, context의 dealer/router/pub/sub 생성, 각 public `DisposeAsync` | `ZLinkDotNetBackendAdapterFactory`; backend factory/forced cleanup tests | 충족 |
| channel send/request/reply | public dealer/router `Send`, `Request`, `Recv`, `Reply`, `SendFlags.DontWait` | channel backend wrappers; Submit acceptance와 route codec tests | 충족 |
| endpoint와 routing option | public `Bind`, `Connect`, `Disconnect`, routing id/probe/mandatory/handover와 HWM option | `ZLinkBackend*Wrapper`; `ChannelRuntimeOptionsTests`, endpoint connection tests | 충족 |
| Spot/actor routing | public SpotNode/Spot 생성, route bridge, actor create/destroy/send/request/join/transfer callback | `ZLinkBackendSpotNodeWrapper`; actor ownership/transfer/dispatch tests | 충족 |
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
| DN-COMMON-003 | `05-framework-api.ko.md` | `handler-interfaces.ko.md`, 여섯 public API snapshot | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature`; `ScaffoldSmokeTests.FrameworkExportedTypes_UseContractsNamespace` |
| DN-COMMON-004 | `02-interaction-model.ko.md` | channel, Spot, actor와 session interaction owner | `SerialExecutorTests.SerialExecutionQueue_DefaultAwait_Holds_Gate_Until_Work_Completes`; `E2E:SM-D11` |
| DN-COMMON-005 | `03-message-model.ko.md` | typed packet identity와 framework envelope codec | `EnvelopeCodecTests.Envelope_requires_marker_and_roundtrips_flow_fields`; `ContractSurfaceCoverage.Frozen_public_surface_excludes_replaced_contracts` |
| DN-COMMON-006 | `04-async-execution-policy.ko.md` | one-way bounded/nonblocking 수락, 단일 `Async(...)` terminator와 automatic turn | `ZLinkAsyncSubmitterTests.Async_ThrowsWhenQueueIsFull`; `StreamConnectorTests.OneWaySubmit_Accepts_Into_A_Bounded_Queue_And_Rejects_Full_Synchronously`; `SessionActorCoordinatorTests.Session_Send_Submit_Rejects_Nonblocking_Transport_Failure_On_The_Caller`; `EntrySpotActorDispatchTests.BoundSession_Submit_Rejects_Missing_Binding_On_The_Caller`; `EntrySpotActorDispatchTests.Actor_Send_Submit_Rejects_Nonblocking_Transport_Failure_On_The_Caller`; `SerialExecutorTests.SerialExecutionQueue_AutomaticTurn_Allows_Later_Work_Then_Resumes_On_Line`; `E2E:ATD-A1`; `E2E:ATD-B3` |
| DN-COMMON-007 | `10-channel-topology.ko.md` | `system-structure.ko.md`, manual/location 연결 | `ChannelsTests.AddZLinkFramework_Throws_WhenClientHasNoPeerAcquisitionPath`; `E2E:RM-A1`; `E2E:RM-A2`; `E2E:PS-A1` |
| DN-COMMON-008 | `22-actor-model.ko.md` | `handler-interfaces.ko.md`, actor context와 join 결과 | `ActorContracts.Actor_context_creates_actors_and_joins_a_spot_by_routing_id`; `EntrySpotActorDispatchTests.EntrySpotActorDispatch_BoundRequest_UsesBoundSession_AndBindsSession`; `E2E:SM-B7` |
| DN-COMMON-009 | `23-spot-actor.ko.md` | `handler-interfaces.ko.md`, `system-structure.ko.md` | `EntrySpotActorDispatchTests.EntrySpotActorDispatch_ConcurrentActors_StartsOutsideEntrySpotSerialLine_AndKeepsSameActorOrdering`; `E2E:SM-B1`; `E2E:SM-B2` |
| DN-COMMON-010 | `31-session-actor-dispatch.ko.md` | `.NET/session-actor-dispatch.ko.md` | `EntrySpotActorDispatchTests.EntrySpotActorDispatch_NoBindRequest_RepliesViaNoBind_AndDoesNotBindSession`; `E2E:SM-D1`; `E2E:SM-D2`; `E2E:SM-D8` |
| DN-COMMON-011 | `24-spot-address-messaging.ko.md` | opaque `SpotHandle`, refresh와 route egress | `LocationResolverTests.Spot_Handle_Request_Refreshes_Once_After_Target_Not_Found`; `E2E:SM-C3`; `E2E:SM-G2` |
| DN-COMMON-012 | `40-location-runtime.ko.md` | `system-structure.ko.md`, row/watch/reconcile/query와 Spot mesh→route channel 매핑 | `LocationResolverTests.Spot_And_Actor_Handles_Use_Configured_Route_Channel_And_Refresh_Uses_The_Same_Map`; `LocationResolverTests.Spot_Handle_Request_Refreshes_Once_After_Target_Not_Found`; `LocationResolverTests.Watch_Upsert_Preserves_Configured_Route_Channel_Mapping`; `LocationResolverTests.Handle_Polling_Updates_Actor_Snapshot_When_Watch_Is_Unavailable`; `AutoConnectReconcilerTests.Reconcile_Connects_New_Targets_And_Disconnects_Vanished_Ones`; `LocationRuntimeTests.Shutdown_Removes_Owner_Lease_Then_Bulk_Removes_Rows`; `E2E:RM-A1`; `E2E:SF-C1` |
| DN-COMMON-013 | `41-location-store-redis.ko.md` | `Zlink.Framework.Locations.Redis`와 공통 store contract | `InMemoryLocationStoreTests.Paged_List_Traverses_All_Rows_With_Continuation_Tokens`; Redis test project 전체 store contract는 G3 실행 ledger에서 별도 증명 |
| DN-COMMON-014 | `52-message-flow-tracing.ko.md` | `system-structure.ko.md`, observer와 error surface | `MessageFlowTracerTests.Off_SuppressesAllTransitions`; `UnhandledDispatchPolicyTests.DispatchErrorReporter_DeliversMessageFlowErrorSnapshot`; `E2E:OBS-A1` |
| DN-COMMON-015 | `53-flow-correlation.ko.md` | channel/stream codec, async flow context와 connector | `FlowCorrelationTests.Connector_and_framework_share_one_monotonic_correlation_sequence`; `FlowCorrelationTests.Awaited_work_keeps_flow_but_detached_work_loses_the_expired_lease`; `E2E:OBS-A2` |
| DN-COMMON-016 | `51-runtime-metrics.ko.md` | framework `Meter`와 connector 계기 | `RuntimeMetricsTests.Meter_Catalog_Uses_Exact_Names_Kinds_Units_And_Scope`; `RuntimeMetricsTests.Inactive_Meter_Does_Not_Allocate_Or_Retain_Per_Event_State`; `StreamConnectorTests.TestMetricReaderRetainsOnlyABoundedSnapshot`; `E2E:OBS-B1` |
| DN-COMMON-017 | `54-graceful-drain-handoff.ko.md` | drain control, typed `Draining`, session closing | `DrainCoordinatorTests.Drain_Is_Idempotent_And_First_Deadline_Is_Fixed`; `DrainCoordinatorTests.Waiter_Cancellation_Does_Not_Cancel_Shared_Drain`; `StreamSessionForcedCleanupTests.Rejected_terminal_disposal_and_force_close_share_one_transport_close`; `E2E:OBS-C1`; `E2E:OBS-C4` |
| DN-COMMON-018 | `00-public-contract-governance.ko.md` | `handler-interfaces.ko.md` §17, fixed source/package snapshots | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature`; `PublicContractSnapshotTests.Renderer_Preserves_CSharp_PublicContract_Distinctions` |
| DN-COMMON-019 | `90-implementation-gap.ko.md` | plan §6 `DN-001`~`DN-025`와 이 ledger | `RegressionTests.DotNetContractRegressionTestReferences_Resolve_ToActiveTestMethods`; 각 DN 행의 exact proof는 plan과 실행 log에서 추적 |
| DN-COMMON-020 | `32-stream-connector.ko.md` | 대상 실행 환경, transport, wire 계약, 연결 생명주기, 배포 산출물 | `StreamConnectorTests.HeaderProtocolRejectsMissingMarkerAndInvalidFlowFields`; `StreamConnectorTests.HeaderProtocolEnforcesControlPacketContract`; `StreamConnectorTests.SessionClosingCodecDecodesTheVersionedClosedReason`; `StreamConnectorTests.TcpSendUsesHeaderPayloadFrame` |
| DN-COMMON-021 | `21-spot-node.ko.md` | SpotNode 등록, Entry Spot bind 순서, SpotManager 생성·조회·종료 | `E2E:SM-A1`; `ScaffoldSmokeTests.PublicSurface_Removes_DirectRouteContracts_And_Exposes_ActorContracts` |
| DN-COMMON-022 | `25-stage-wrapper-on-spot.ko.md` | spot 실행 문맥 직렬화, timer, wrapper 책임 경계 | `E2E:SM-B7`; `E2E:SM-E3`; `E2E:SM-A5` |
| DN-COMMON-023 | `30-stream-session.ko.md` | 서버 session 표면, dispatch 모델, 등록 검증, 오류 경계 | `NodesAndServicesTests.AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleSessions`; `StreamSessionForcedCleanupTests.Stream_node_preserves_typed_routing_id_from_backend_callback`; `E2E:SM-D7`; `E2E:SM-D8` |
| DN-COMMON-024 | `20-spot-messaging.ko.md` | outbound 세 축, publish·subscribe, dispatch 실패 정책, route ingress, startup validation | `NodesAndServicesTests.AddZLinkFramework_Throws_WhenSpotFactoryTypeIsDuplicatedAcrossNodes`; `NodesAndServicesTests.AddZLinkFramework_AllowsStandaloneLocalSpotNode`; `E2E:SM-C4`; `E2E:SM-B7` |
| DN-COMMON-025 | `11-channel-messaging.ko.md` | channel runtime 수명, dispatch 실패 정책, startup validation, 종료 중 호출 | `NodesAndServicesTests.AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleSessions`; `E2E:OBS-B1` |
| DN-COMMON-026 | `50-runtime-monitoring.ko.md` | source 분리, event 종류, polling 규칙, monitoring startup validation | `CoverageCriticalRuntimeTests.SpotTimerFailureEventFactory_MapsStoppedAndContinuingFailures`; `RuntimeMetricsTests.Meter_Catalog_Uses_Exact_Names_Kinds_Units_And_Scope` |

## 4. .NET 정식 계약 문서 coverage

| ID | 정식 계약 문서 | production owner | exact proof |
|----|----------------|------------------|-------------|
| DN-DOC-001 | `README.ko.md` | 언어별 계약 index와 cancellation projection | `RegressionTests.DotNetContractDocuments_AllExposeRegressionTestSection` |
| DN-DOC-002 | `01-system-structure.ko.md` | ASP.NET Core host 등록·부트스트랩·DI·lifecycle — channel · SPOT · SpotNode/Entry Spot · STREAM · session actor dispatch · monitoring · location 등록 표면과 startup validation | `NodesAndServicesTests.AddZLinkFramework_Throws_WhenSpotFactoryTypeIsDuplicatedAcrossNodes`; `NodesAndServicesTests.AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleSessions`; `NodesAndServicesTests.AddZLinkFramework_AllowsStandaloneLocalSpotNode`; `E2E:SM-A1` |
| DN-DOC-003 | `02-handler-interfaces.ko.md` | 전체 public interface·context·handler·client·등록·timer·filter·attribute·관측 투영 카탈로그와 §17 공개 계약 산출물 검증 절차 | `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature`; `PublicContractSnapshotTests.Renderer_Preserves_CSharp_PublicContract_Distinctions`; `CoverageCriticalRuntimeTests.SpotTimerFailureEventFactory_MapsStoppedAndContinuingFailures` |
| DN-DOC-004 | `03-stream-connector.ko.md` | 별도 client connector의 lifecycle, dispatch, codec, transport와 종료 사유 | `StreamConnectorTests.TcpSendUsesHeaderPayloadFrame`; `StreamConnectorTests.HeaderProtocolEnforcesControlPacketContract` |

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
| DRAIN-019 | PROVEN — `DrainCoordinatorTests.Drain_Executor_Preserves_The_Frozen_Seven_Phase_Order`; `DrainCoordinatorTests.Host_Stop_Uses_The_Same_Thirty_Second_Default_Deadline` |
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
| DN-G0-DRAIN-001 | `graceful-drain-handoff` §3~§5 | 모든 placement 제외, fixed propagation bound, 7단계 순서, surface별 거부와 redirect 부재 | PROVEN — `AutoConnectReconcilerTests.Draining_Marker_Is_Monotonic_Across_Subsequent_Renewal`; `DrainCoordinatorTests.Drain_Executor_Preserves_The_Frozen_Seven_Phase_Order`; `DrainCoordinatorTests.Draining_Gate_Rejects_Each_New_Public_Admission_With_The_Frozen_Error`; `DrainCoordinatorTests.Drain_Is_Idempotent_And_First_Deadline_Is_Fixed`; `ContractSurfaceCoverage.Frozen_public_surface_excludes_replaced_contracts`; `E2E:OBS-C1` |
| DN-G0-DRAIN-002 | `graceful-drain-handoff` §5~§9 | queue drain 전 row release 금지, adapter 미등록 actor, host 30초/종료 순서, drain event 무등록 | PROVEN — `DrainCoordinatorTests.ReleaseAndRecreate_Waits_For_Spot_Queue_Close_Before_Row_Release`; `ActorHandoffTests.Unregistered_Transfer_Adapter_Uses_The_Frozen_Empty_State`; `DrainCoordinatorTests.Host_Stop_Uses_The_Same_Thirty_Second_Default_Deadline`; `DrainCoordinatorTests.Default_Drain_Uses_Thirty_Seconds_Without_Event_Registration`; `DrainCoordinatorTests.Framework_Drain_Sends_ServerDrain_Before_Orderly_Stream_Close` |
| DN-G0-SPOTNODE-001 | `dotnet/spot-node` §9~§31 | Entry Spot 설정 독립성, Entry RID-before-bind와 전체 적용 순서 | PROVEN — `EntrySpotActorDispatchTests.EntrySpot_Configuration_Is_Independent_And_RoutingId_Is_Applied_Before_Bind` |
| DN-G0-SPOTNODE-002 | `dotnet/spot-node` §41~§55 | actor의 Entry RID, handle 내부 owner/kind 보존과 유효한 location row 기준 | PROVEN — `EntrySpotActorDispatchTests.Actor_Creation_Observes_The_Configured_EntrySpot_RoutingId`; `LocationResolverTests.Actor_Handle_Internal_Snapshot_Preserves_Entry_Owner_And_Kind`; plan `DN-057` |
| DN-G0-SPOTPUBLISH-001 | `dotnet/handler-interfaces` §5.3, `dotnet/aspnet-core-spot` 외부 publisher 계약 | `IZLinkSpotPublisherClient.PublishSpot(channelName, topic, message)` exact member와 외부 Spot channel 발행 | RESOLVED — 구현과 fixed API snapshot을 `PublishSpot`으로 교체; `SpotContracts.Spot_clients_separate_local_spot_api_routed_egress_and_publisher_channels`; `EntrySpotActorDispatchTests.External_Spot_Publish_Emits_Internal_Publisher_Rid_Without_Correlation`; `E2E:SM-C4` |
| DN-G0-BLOCKER-001 | `dotnet/spot-node` §55~§57 | 언어별 문서의 존재하지 않는 core `ResolveSpot` 요구를 제거하고 공통 location runtime의 유효한 store row 기준으로 통일 | RESOLVED — plan `DN-057` |
| DN-G0-BLOCKER-002 | `flow-correlation` §8 | `FlowOrigin`을 nullable로 교체해 flow/origin optional pair를 정확히 표현 | RESOLVED — plan `DN-058` |

새 GAP이 발견되면 §6 구현 ledger에 개별 작업과 exact test로 연결한다. 여러 의미를 한 test
이름만으로 덮지 않으며, frozen public interface 변경이 필요한 경우 구현하지 않고 계약 blocker로
분리한다.

## 6. 아직 남은 G0 판정

- public API와 package export 분모: 최종 checkout package verifier와 clean consumer 재검증 완료
- bindings package/version/hash: `Systems.Zlink 8.6.6`과 package SHA-256 `dae37c26458d965cc63114d1fe846b129208feff815dc0a22ddd5f904854f84e` 재검증 완료
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
8ffae3ae36f3305e1dfa35d1874a1c2c9c57342f5f2116abbe7f5e432f79f595 README.ko.md
8cf0cac1e46c6086de082d8ad4aeae51f339245d05da1b7bc6175f9b622ec79e server/22-actor-model.ko.md
6614f5efd549442f95ac4f67f8ff1e10bba9c7061ee63a7608ffd91f43fea4bd 04-async-execution-policy.ko.md
5f190e3b4f1b93d4a0e03c9ba23b625a1b8a56c5d79dc361917215be73fa0839 server/10-channel-topology.ko.md
077319afac1aec1aba884853cd172443f5e2563d664b00b0a9e2468a252a196c server/53-flow-correlation.ko.md
06f3d56438301a80afd983475e58c65d3b0e678a32b832c5f13813bf937ffcb6 05-framework-api.ko.md
822ada32199d71d2c4505c561fc4f2f4db6f9c50d49eb2469b202d87dd2bc97f server/54-graceful-drain-handoff.ko.md
b40d643a75203a8c632e24304e0a0d06603556843d0958fc6a253a771310b1ed 90-implementation-gap.ko.md
df441c4de567865658b0b79ded6c840d020ccf60865f58e7990a248e9fa361a0 02-interaction-model.ko.md
dfa08a0db46f59bcd107347c9f02256ff023d7c64c3f8caac42772c37d7b058b server/40-location-runtime.ko.md
f84d4a035cd773d6fe8aa0096151909e92be0743b57445dd51e5b38eeab9376c server/41-location-store-redis.ko.md
0635851f5d9b3cf0fa6f481fb886200e1802f3bda6fe80db3648b35b53e22108 server/52-message-flow-tracing.ko.md
a165665cbb47ef2b69744cfa7614d40c35274154af47439693f811080934f914 03-message-model.ko.md
136b4b2378c404b4728a4e526f985da6303456c294c06e9e425a39abb99d816b 01-overview.ko.md
883e767f3c2b673c9dabc4083fa42a7fc29799d25ef9ad04761d9cbdbc5cb245 00-public-contract-governance.ko.md
d34e9b26860a2ee285b340c5234bb27fc4c82438bbdf375e697f1350a0c1ef1f server/51-runtime-metrics.ko.md
d30ea2acfbee45009ee2e0d000f2b37009ccf9f5f134c8ad29dd2035e3b8ab99 server/50-runtime-monitoring.ko.md
49f5154412ed827496ba50f2e49a0f6bc84f3e1bcfdb4022b561dbded9b64147 server/31-session-actor-dispatch.ko.md
ae0c25c9f67cb397da861e82d8aaf1311472dfe5e28212a88f1e0aa32ec20998 server/23-spot-actor.ko.md
45576c26b8061e0a1965d219d539080a4917c6c06572b5d63bccddfb2f1bbe4d server/24-spot-address-messaging.ko.md
d9546cf37a3f9f34e863ac4a63eda2e2af6f1985269279579fa5b53632978108 server/20-spot-messaging.ko.md
d9a36ee80739f7035a0371c703871cea26f952e353d0f123e45b69e44c540088 server/21-spot-node.ko.md
fe9072b34809ccc20b489f6a3ebdd093fdd35470d3f1ee291e065f5644cc5f99 stream-connector/32-stream-connector.ko.md
623bca5e070513cc314c2d7f93d00dcdeab8b5f473bdeb883bfb5711eaa028e0 server/30-stream-session.ko.md
25ed1dd2bb81af78a61695bfaadbdba35edb15be565ca2c53e8f0e2cabaf65ec server/languages/dotnet/README.ko.md
9ce0f5144fc1a828de539d0c49a900d43b2d2a81ef47e5d8411e4600a72283ce stream-connector/languages/dotnet/03-stream-connector.ko.md
09451c4d9c3bc707e5ce7108dfe4ca636e975ca9e8e4f9b1110f5e9065a505ea server/languages/dotnet/02-handler-interfaces.ko.md
8c1dc04eb9193658092a96cb0bcd44e1dde64789a40835ff74680726ffd162aa server/languages/dotnet/01-system-structure.ko.md
54f7a53bc1ff7cc97ada0a41d28f50678e43d68c3ad46e0beef43466dd8ccf5c server/25-stage-wrapper-on-spot.ko.md
7a1a32c29bc2cfc642ce465f71e5f405741a2a8c23b95d0426b132947fdd0202 server/11-channel-messaging.ko.md
```
