<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [Scope](./implementation-scope-and-nongoals.ko.md)
<!-- framework-adapter-nav:end -->

# Draft -- Java Regression Test Matrix

> 이 문서는 **구현 전 초안**이다.
> Java/Kotlin 구현이 `.NET` framework와 동등함을 증명하기 위한 테스트 묶음이다.

## 1. 테스트 계층

| 계층 | 목적 |
|------|------|
| unit | option validation, scanner, packet name resolver, DI 노출 정책 |
| contract | public interface, annotation, exception, codec 계약 |
| fake backend | submit queue, reply correlation, session serial dispatch |
| integration-single-process | 한 JVM 안의 channel, registry, spot, stream smoke |
| integration-multi-process | registry discovery, reconnect, remote actor/session relay |
| sample regression | 실제 sample 실행과 self-check |

JUnit 테스트 이름은 `.NET` 테스트 메서드를 camelCase로 옮긴 대응이다(`.NET`의
`AddZLinkFramework_*` → `addZLinkFramework_*`). 확인 기준(의미)은 그대로 유지하며
`.NET` 코드가 최종 기준이다.

## 2. Channel regression

`ChannelMessagingTest.discoveryClientServer_requestReplySucceeds`는 embedded registry,
server runtime, client runtime을 나누어 시작한 뒤 `useDiscovery(...)`로 registry에
붙은 DEALER client가 ROUTER server를 찾아 request/reply 하는 경로를 검증한다.

| 항목 | 계층 | JUnit 테스트 | 통과 기준 |
|------|------|--------------|-----------|
| duplicate channel name | unit | `ChannelsTest.addZLinkFramework_throws_whenChannelNameIsDuplicated` | startup validation 오류 |
| handler duplicate mapping | unit | `HandlerExposureTest.addZLinkFramework_throws_whenMappedGroupAndTypedHandlerExposeSameChannelPacket` | 같은 channel의 `kind + packetName` 중복 차단 |
| client without peer acquisition | unit | `ChannelsTest.addZLinkFramework_throws_whenClientHasNoPeerAcquisitionPath` | startup validation 오류 |
| discovery/manual mixed capability | unit | `ChannelsTest.addZLinkFramework_throws_whenRouteChannelMixesDiscoveryAndManualConnections` | startup validation 오류 |
| manual client/server request | integration-single-process | `ChannelMessagingTest.manualClientServer_requestReplySucceeds` | request/reply 성공 |
| discovery client/server request | integration-single-process | `ChannelMessagingTest.discoveryClientServer_requestReplySucceeds` | registry discovery 기반 request/reply 성공 |
| fanout publish/subscribe | integration-single-process | `FanoutTest.publisherAndSubscriber_workAcrossHosts` | publish 수신 |
| route mesh request | integration-single-process | `RouteMeshTest.routeMesh_requestByRoutingIdSucceeds` | target `RoutingId` request 성공 |
| send/publish async submit | fake backend | `ZLinkAsyncSubmitterTest.submitAsync_drainsPendingItemFromReadyCallback` | ready 전 caller thread를 막지 않음 |
| pending request cleanup | unit | `ZLinkAsyncSubmitterTest.submitAsync_failsPendingItemWhenSendTimeoutExpires` / `disposeAsync_failsPendingItems` | timeout, cancellation, stop에서 pending 제거 |

## 3. Spot/Actor regression

`RemoteActorGatewayTest`, `ActorSessionStateTest`, `BoundSessionTest`는 testkit
fake backend에서 ActorGateway attach, bind/unbind, bound push backend operation을
관찰해 session relay의 내부 계약을 먼저 고정한다. 실제 OS process를 나누는
multi-process smoke는 release gate에서 같은 테스트 이름의 의미를 유지해 추가한다.

| 항목 | 계층 | JUnit 테스트 | 통과 기준 |
|------|------|--------------|-----------|
| duplicate Spot factory | unit | `NodesAndServicesTest.addZLinkFramework_throws_whenSpotFactoryTypeIsDuplicatedAcrossNodes` | startup validation 오류 |
| duplicate Entry Spot | unit | `NodesAndServicesTest.addZLinkFramework_throws_whenSpotNodeRegistersMultipleEntrySpots` | startup validation 오류 |
| actor factory without SpotNode | unit | `NodesAndServicesTest.addZLinkFramework_throws_whenActorFactoryWithoutSpotNode` | startup validation 오류 |
| actor manager DI 노출 | unit | `NodesAndServicesTest.addZLinkFramework_registersActorManager_whenSpotNodeAndActorFactoryExist` | SpotNode + actor factory 시 등록 |
| local-only SpotNode | integration-single-process | `NodesAndServicesTest.addZLinkFramework_allowsStandaloneLocalSpotNode` | discovery 없이 local Spot 생성 |
| Spot create/get/list/remove | integration-single-process | `SpotManagerTest.spotManager_createListRemoveAndPublish_workThroughFrameworkRuntime` | lifecycle callback과 조회 일관 |
| Spot getOrCreate 1회 초기화 | integration-single-process | `SpotManagerTest.spotManager_getOrCreate_initializesOnceWithFirstCreatePayload` | 첫 create payload로 한 번만 초기화 |
| Spot timer/publish/remove | integration-single-process | `SpotManagerTest.spot_publishTimerAndRemove_stopCallbacksWork` | timer/publish/remove 의미 유지 |
| actor manager factory | integration-single-process | `ActorManagerTest.actorManager_createGetOrCreateFind_work` | create/getOrCreate/find 동작 |
| session actor relay | integration-single-process | `SessionActorRelayTest.bindAndRelay_work` | `bindAsync`와 `relayAsync` 동작 |
| remote ActorGateway relay | integration-multi-process | `RemoteActorGatewayTest.sessionAndPlayServers_relaySucceeds` | Session 서버와 Play 서버 사이 relay 성공 |
| stale binding token guard | integration-multi-process | `ActorSessionStateTest.actorSessionState_filtersStaleDisconnect_andOnlyDisconnectsCurrentStream` | 이전 binding이 새 binding을 지우지 않음 |
| bound session push | integration-multi-process | `BoundSessionTest.playActorPush_arrivesAtClientStream` | Play actor push가 client stream에 도착 |

## 4. STREAM/Connector regression

connector 테스트 이름은 `Systems.Zlink.Stream.Connector.Tests`의 메서드를 camelCase로
옮긴 대응이다.

| 항목 | 계층 | JUnit 테스트 | 통과 기준 |
|------|------|--------------|-----------|
| stream node duplicate session | unit | `NodesAndServicesTest.addZLinkFramework_throws_whenStreamNodeRegistersMultipleSessions` | startup validation 오류 |
| session connected/dispatch/reply | integration-single-process | `StreamSessionTest.headerSession_connectedDispatchReply_succeeds` | header session callback 성공 |
| session serial dispatch | fake backend | `StreamSessionTest.sameSessionCallbacks_runSerially` | 같은 session callback 순서 보장 |
| payload borrowed lifetime | contract | `StreamPayloadTest.borrowedPayload_requiresCopyOutsideCallback` | callback 밖 보관 시 copy 필요 정책 일치 |
| connector transport inference | unit | `ZLinkStreamConnectorTest.uriSchemeAndTransportMismatchIsRejected` | URI scheme과 transport mismatch 차단 |
| connector manual dispatch | integration-single-process | `ConnectorDispatchTest.dispatch_invokesCallback` | `dispatchAsync()` 호출 시 callback 실행 |
| connector request timeout | unit | `LifecycleTest.heartbeatTimeoutFailsPendingRequestsWithTimeoutCause` | pending request 정리 |
| connector reconnect | integration-single-process | `LifecycleTest.reconnectRestoresConnectionAfterTransportClose` | backoff와 max attempts 의미 유지 |
| reserved packet name 거부 | contract | `ZLinkStreamConnectorTest.reservedPacketNamesAreRejectedForUserHandlers` | 예약 packet 이름 거부 |
| connector codec helper | contract | `ConnectorCodecContractTest.jsonMsgpackProtobufTypedHelperRoundtrip` | JSON/MessagePack/Protobuf typed helper roundtrip |
| Kotlin connector wrapper | contract | `KotlinConnectorWrapperTest.suspendWrapperPreservesConnectorSemantics` | suspend wrapper가 Java connector 의미를 바꾸지 않음 |

## 5. Registry/Monitoring regression

monitoring은 discovery source를 노출하지 않는다(discovery 상태는 registry
snapshot/query로만 관찰). 따라서 "socket/registry/spot" event만 둔다.
`EmbeddedRegistryTest.remoteRegistryQueryClient_canReadTopologySnapshot`은
embedded registry의 router endpoint에 remote query client가 연결해 topology
snapshot API를 호출하는 native integration gate다. 현재 channel discovery 등록은
별도 행에서 닫기 전이므로 snapshot 내용은 비어 있을 수 있지만, query client 연결과
filter 전달은 실제 binding public API 경로로 검증한다.

| 항목 | 계층 | JUnit 테스트 | 통과 기준 |
|------|------|--------------|-----------|
| registry pub endpoint missing | unit | `RegistryAndMonitoringTest.addZLinkRegistry_throws_whenPubEndpointIsMissing` | startup validation 오류 |
| registry router endpoint missing | unit | `RegistryAndMonitoringTest.addZLinkRegistry_throws_whenRouterEndpointIsMissing` | startup validation 오류 |
| embedded registry query | integration-single-process | `EmbeddedRegistryTest.embeddedRegistry_queryService_resolvesAndReadsStatus` | `ZLinkRegistryQuery` snapshot 조회 |
| remote registry query client | integration-multi-process | `EmbeddedRegistryTest.remoteRegistryQueryClient_canReadTopologySnapshot` | remote topology snapshot 조회 |
| monitoring source validation | unit | `RegistryAndMonitoringTest.addZLinkMonitoring_throws_whenSocketSourceIsUnknownOnStartup` | source 이름 불일치 차단 |
| socket event | integration-single-process | `MonitoringEventsTest.spotMonitoring_emitsSubjectsChanged_whenSpotIsCreated` | typed event handler 호출 |
| registry/spot snapshot diff | integration-single-process | `MonitoringEventsTest.registryMonitoring_emitsStatusChanged_forEmbeddedRegistry` | snapshot diff typed event handler 호출 |
| handler failure policy | unit | `MonitoringRunnerTest.handlerFailure_recordsDiagnostic_withoutStopping` | monitoring runner 중단 없이 diagnostic 기록 |

## 5.1 Lifecycle regression

| 항목 | 계층 | JUnit 테스트 | 통과 기준 |
|------|------|--------------|-----------|
| host start/stop | integration-single-process | `HostTest.host_startsAndStops_frameworkRuntimeContext` | `SmartLifecycle` 시작·종료에 맞춰 runtime context 생성·정리 |
| registry before framework | integration-single-process | `HostTest.host_startsEmbeddedRegistry_beforeFrameworkRuntime` | embedded registry가 framework runtime보다 먼저 시동 |
| stream transport error scope | integration-multi-process | `StreamSessionTest.onError_reportsTransportError_forRemoteDisconnect` | remote disconnect만 session `onError`로 보고 |

## 6. Sample release gate

| Sample | 확인 기준 |
|--------|-----------|
| `TicTacToe` | direct STREAM + Spot + channel 흐름 성공 |
| `TicTacToe.SessionGateway` | reconnect 후 같은 actor id로 새 session binding |
| `Bingo` | 4 connector client, matching, timer, bound push 성공 |
| `StreamingClient` | connector send/request/on/manual dispatch/lifecycle event/reconnect smoke |

sample source와 runner 구조는
`SampleReleaseGateContractTest.requiredSamplesExposeExecutableEntryPoints`,
`sampleSourcesUseOnlyPublicFrameworkAndConnectorApi`,
`ticTacToeSessionGatewayUsesActorGatewayAndFrameworkActorLocator`,
`bingoMirrorsFourClientMatchingTimerAndBoundPushGate`,
`streamingClientMirrorsConnectorSmokeGate`가 고정한다. 실제 실행 self-check는 아래
release gate command가 담당한다.

release gate command:

```bash
./framework/languages/java/samples/run_samples.sh
```
