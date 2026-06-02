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

JUnit 테스트 이름은 `.NET` 테스트 메서드를 camelCase로 옮긴 대응을 우선하되,
Java 구현에서 이미 고정된 이름이 있으면 현재 JUnit 이름을 그대로 쓴다. 확인
기준(의미)은 그대로 유지하며 `.NET` 코드가 최종 기준이다.

아래 표의 항목은 현재 Java 구현에서 실제 JUnit 이름과 gate 의미를 함께 고정한다.
새 항목을 "필수 예정 gate"로 추가할 때는 sample release gate를 완료했다고 판단하기
전에 실제 구현과 테스트 이름을 확정해야 한다.

## 2. Channel regression

`ChannelMessagingTest.discoveryClientServer_requestReplySucceeds`는 embedded registry,
server runtime, client runtime을 나누어 시작한 뒤 `useDiscovery(...)`로 registry에
붙은 DEALER client가 ROUTER server를 찾아 request/reply 하는 경로를 검증한다.

| 항목 | 계층 | JUnit 테스트 | 통과 기준 |
|------|------|--------------|-----------|
| duplicate channel name | unit | `DefaultZLinkFrameworkOptionsTest.addClientServerChannelRejectsDuplicateChannelName` | startup validation 오류 |
| handler duplicate mapping | unit | `DefaultZLinkFrameworkOptionsTest.clientServerChannelRejectsDuplicateRequestHandlerPacketName` | 같은 channel의 `kind + packetName` 중복 차단 |
| client without peer acquisition | unit | `DefaultZLinkFrameworkOptionsTest.clientServerChannelClientWithoutPeerAcquisitionPathIsRejected` | startup validation 오류 |
| discovery/manual mixed capability | unit | `DefaultZLinkFrameworkOptionsTest.clientServerChannelClientCannotMixDiscoveryAndManualConnections` | startup validation 오류 |
| fanout publisher without bind | unit | `DefaultZLinkFrameworkOptionsTest.fanoutChannelPublisherWithoutBindIsRejected` | startup validation 오류 |
| fanout subscriber without peer acquisition | unit | `DefaultZLinkFrameworkOptionsTest.fanoutChannelSubscriberWithoutPeerAcquisitionPathIsRejected` | startup validation 오류 |
| fanout discovery/manual mixed capability | unit | `DefaultZLinkFrameworkOptionsTest.fanoutChannelSubscriberCannotMixDiscoveryAndManualConnections` | startup validation 오류 |
| fanout duplicate mapping | unit | `DefaultZLinkFrameworkOptionsTest.fanoutChannelRejectsDuplicatePublishHandlerPacketName` | 같은 fanout channel의 `packetName` 중복 차단 |
| manual client/server request | integration-single-process | `ChannelMessagingTest.manualClientServer_requestReplySucceeds` | request/reply 성공 |
| discovery client/server request | integration-single-process | `ChannelMessagingTest.discoveryClientServer_requestReplySucceeds` | registry discovery 기반 request/reply 성공 |
| fanout publish/subscribe | integration-single-process | `ChannelMessagingTest.publisherAndSubscriber_workAcrossHosts` | publish 수신 |
| route mesh request | integration-single-process | `ChannelMessagingTest.routeMesh_requestByRoutingIdSucceeds` | `configureRouting(...)`으로 지정한 target `RoutingId` request 성공 |
| client/server Spot route egress without client | unit | `DefaultZLinkFrameworkOptionsTest.clientServerSpotRouteEgressRequiresClientCapability` | `enableSpotRouteEgress(...)`는 client capability 없는 client/server channel에서 startup validation 오류 |
| route shared packet reply correlation | integration-single-process | `ChannelMessagingTest.routeMesh_matchesRepliesByRequestSequenceWhenPacketNameIsShared` | 같은 packet 이름의 동시 route request가 request sequence로 자기 reply를 받음 |
| annotation handler dispatch | integration-single-process | `ChannelMessagingTest.scannedMethodHandlerGroup_requestAndSendDispatch` / `scannedMethodHandlerGroup_publishDispatches` | `@ZLinkRequest`, `@ZLinkSend`, `@ZLinkPublish` method handler가 scanner와 runtime dispatch에 연결됨 |
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
| actor manager runtime 노출 | unit | `NodesAndServicesTest.addZLinkFramework_registersActorManager_whenSpotNodeAndActorFactoryExist` | SpotNode + actor factory 시 runtime manager 등록 |
| Spring Spot/Actor DI 노출 | unit | `ZLinkFrameworkAutoConfigurationTest.spotAndActorManagersAreNotBeansWithoutSpotNode` / `spotManagerIsBeanWhenSpotNodeExists` / `actorManagerIsBeanWhenSpotNodeAndActorFactoryExist` | SpotNode와 actor factory capability에 맞춰 Spring bean 등록/미등록, SpotNode가 있을 때 `ZLinkSpotOutbound` 등록 |
| local-only SpotNode | integration-single-process | `NodesAndServicesTest.addZLinkFramework_allowsStandaloneLocalSpotNode` | discovery 없이 local Spot 생성 |
| Spot create/get/list/remove | integration-single-process | `SpotManagerTest.spotManager_createListRemoveAndPublish_workThroughFrameworkRuntime` | lifecycle callback과 조회 일관 |
| ambient Spot outbound | fake backend | `SpotRuntimeFakeBackendTest.ambientSpotOutboundWorksInsideSpotCallback` | DI-style `ZLinkSpotOutbound`가 active Spot callback 안에서 current Spot backend를 사용 |
| Spot outbound send/request to Spot | fake backend | `SpotRuntimeFakeBackendTest.spotOutboundSpotCallsUseBackendSpotRouteOperations` | `ZLinkSpotOutbound.sendToSpot(...)` / `requestToSpot(...)`가 실패 stub 없이 backend Spot route send/request operation으로 이어짐 |
| routed Spot egress relay | fake backend | `SpotRuntimeFakeBackendTest.spotOutboundUsesConfiguredClientServerEgressChannel` / `spotOutboundUsesConfiguredRouteMeshEgressChannel` / `routeMeshSpotEgressUsesDiscoveryMemberPeerRoutingIdBeforeRegistryQuery` / `routeMeshSpotEgressUsesRegistryQueryRoutingId` / `routeMeshSpotEgressRequiresTargetRoutePeerRoutingId` / `spotOutboundRejectsAmbiguousEgressChannels` | egress channel이 하나로 명확한 구성에서 Spot outbound가 reserved relay packet을 만들고 client/server dealer 또는 route mesh router를 통해 전송함. ingress는 backend router의 public `sendToSpot` / `requestToSpot` port로 이어짐. route mesh egress는 target route channel의 명시적 routing id, local discovery member peer routing id, registry query topology routing id 순서로 peer id를 찾는다. 모두 없으면 configuration error를 내고, egress channel이 여러 개면 local route로 숨기지 않는다 |
| registry-backed Spot remote address resolver | unit/fake backend | `DefaultZLinkFrameworkOptionsTest.registrySpotRemoteAddressesRejectsCustomResolverDuplicate` / `registrySpotRemoteAddressesRequiresDiscoveryEndpoint` / `registrySpotRemoteAddressesRequiresRouterChannelWhenAmbiguous` / `SpotRuntimeFakeBackendTest.registrySpotRemoteAddressResolverReturnsRouteModelFromSpotDiscovery` | `useRegistrySpotRemoteAddresses(...)`가 custom resolver 중복, discovery 누락, route mesh ambiguity를 validation에서 거부하고, resolver는 Spot discovery의 owner node rid, spot rid, spot kind, router channel id를 `ZLinkSpotRemoteAddress`로 반환 |
| Spot publisher client | fake backend | `SpotRuntimeFakeBackendTest.spotPublisherClientPublishesThroughAttachedPublisherSpot` / `spotPublisherClientRejectsUnattachedChannel` | attached publisher capability가 있는 channel만 lazy publisher Spot으로 publish하고, 미등록 channel은 configuration error |
| Spot router/pubsub manual peers | fake backend | `SpotRuntimeFakeBackendTest.spotRouterAndPubSubManualPeersConnectThroughBackendNode` | router/pubsub `useManualConnections(...)`가 registration에 남고 backend SpotNode peer 연결로 이어짐 |
| SpotNode routing id | fake backend | `DefaultZLinkFrameworkOptionsTest.spotNodeRejectsConflictingRouterAndPubSubRoutingIds` / `SpotRuntimeFakeBackendTest.spotRouterAndPubSubManualPeersConnectThroughBackendNode` | router/pubsub `setRoutingId(...)`가 단일 SpotNode routing id로 검증되고 backend SpotNode에 적용됨 |
| Entry Spot activation lifecycle | fake backend | `DefaultZLinkFrameworkOptionsTest.entrySpotRoutingIdMutatesRegistrationModel` / `SpotRuntimeFakeBackendTest.entrySpotRoutingIdAppliesToBackendEntrySpotBeforeBind` / `registeredEntrySpotIsActivatedAndClosedWithBackendEntrySpot` | `configureEntrySpot(...)` routing id가 backend Entry Spot에 bind 전 적용되고, 등록된 Entry Spot은 startup에서 `configure()`와 `onInitializeAsync()`를 실행하며 shutdown에서 `onClosingAsync()`와 native Entry Spot close를 수행 |
| Entry Spot dispatch backend port | fake backend | `SpotRuntimeFakeBackendTest.registeredEntrySpotIsActivatedAndClosedWithBackendEntrySpot` / `entrySpotDispatchReadableDrainsUnhandledActorJoinWithRejectReply` / `entrySpotActorJoinReadableInvokesRegisteredHandlerAndRepliesOk` | 등록된 Entry Spot activation이 backend Entry Spot의 dispatch handler를 연결한다. framework backend port는 binding public `Spot.setDispatchHandler(...)`, `recvActorJoin(...)`, `replyActorJoin(...)`만 감싸며 binding internal/private API를 호출하지 않는다. actor join readable event는 요청을 drain하고 handler가 없는 join을 reject reply로 닫아 대기 상태로 남기지 않는다. 등록된 `@ZLinkSpotActorJoin` handler가 있으면 managed actor와 request payload를 전달하고 OK reply를 보낸다. join 성공 후 actor context는 joined 상태가 되고 `@ZLinkSpotPostActorJoined` handler가 `JOIN_ENTRY_SPOT` 결과를 받는다 |
| Entry Spot actor packet dispatch | fake backend | `SpotRuntimeFakeBackendTest.entrySpotActorReadableInvokesRegisteredActorSendHandlerOnActorQueue` / `entrySpotActorReadableInvokesRegisteredActorRequestHandlerAndRepliesBoundSession` | backend actor readable event의 actor frame을 packet/body로 읽고, local actor를 찾아 actor별 serial queue에서 등록된 `@ZLinkSpotActorSend` 또는 `@ZLinkSpotActorRequest` handler를 실행한다. STREAM header에 request sequence가 있는 frame은 request handler 응답을 actor bound-session 송신 경로로 보낸다 |
| Spot actor left lifecycle | fake backend | `SpotRuntimeFakeBackendTest.spotContextLeaveActorMarksActorLeftAndInvokesRegisteredHandler` | `ZLinkSpotContext.leaveActorAsync(...)`가 no-op이 아니라 actor context의 joined 상태를 해제하고, 등록된 `@ZLinkSpotActorLeft` handler에 `LEAVE_SPOT` 결과를 전달한다 |
| Entry Spot actor lifecycle readable | fake backend | `SpotRuntimeFakeBackendTest.entrySpotActorLifecycleReadableDrainsLeftEventAndInvokesRegisteredHandler` | backend `ACTOR_LIFECYCLE_READABLE` event가 무시되지 않고 `recvActorLifecycle(DONT_WAIT)`로 drain된다. native lifecycle `LEFT` event는 managed actor 상태를 left로 전환하고 등록된 `@ZLinkSpotActorLeft` handler를 실행한다 |
| Actor context joined user Spot surface | fake backend | `SpotRuntimeFakeBackendTest.entrySpotActorLifecycleJoinedBindsActorContextToUserSpot` | native lifecycle `JOINED` event가 user Spot `spotRid`를 포함하면 actor context의 `spotRid()`, `getSpot()`, `getSpot(Class)`가 해당 runtime Spot 인스턴스를 가리킨다. Entry Spot 단계처럼 user Spot이 없는 상태에서 `getSpot()`은 성공으로 가장하지 않는다 |
| Spot actor disconnected lifecycle | fake backend | `ActorSessionStateTest.actorSessionState_filtersStaleDisconnect_andOnlyDisconnectsCurrentStream` | session actor `notifyDisconnectedAsync()`가 backend unbind 뒤 current binding이 실제로 해제된 경우에만 등록된 `@ZLinkSpotActorDisconnected` handler를 actor serial queue에서 실행한다. stale binding disconnect는 현재 bound session과 disconnected lifecycle을 건드리지 않는다 |
| Bound session disconnect | fake backend | `BoundSessionTest.boundSessionDisconnect_unbindsCurrentActorSession` | `ZLinkBoundSession.disconnectAsync()`가 no-op이 아니라 backend `unbindActor(...)`를 호출하고 actor context의 current bound session을 해제한다. 이 경로는 server가 client session을 닫는 의미이므로 actor disconnected lifecycle을 대신 발생시키지 않는다 |
| Session actor relay | fake backend | `BoundSessionTest.sessionActorRelay_sendsPacketThroughBoundActorBackendRoute` | `ZLinkSessionActor.relayAsync(...)`가 no-op이 아니라 framework backend stream의 bound-actor route로 header와 payload copy를 보낸다. 호출자가 넘긴 payload 소유권은 relay 호출이 가져가지 않는다 |
| Spot attached channel client | fake backend | `SpotRuntimeFakeBackendTest.attachedSpotChannelClientManualConnectionAttachesDealerToBackendNode` / `attachedSpotChannelClientDiscoveryAttachesDealerToBackendNode` | `attachChannelClient(...)`가 manual/discovery 경로로 backend dealer를 SpotNode에 attach |
| Spot route acceptance | fake backend | `DefaultZLinkFrameworkOptionsTest.acceptedSpotRouteChannelManualConnectionsMutateRegistrationModel` / `acceptedSpotRouteChannelRequiresRouterCapability` / `acceptedSpotRouteChannelRejectsMissingOrWrongChannelKind` / `acceptedSpotRouteChannelRequiresDiscoveryOrManualConnection` / `acceptedSpotRouteChannelRejectsDuplicateRegistration` / `SpotRuntimeFakeBackendTest.acceptedSpotRouteChannelManualConnectionsAttachRouterChannelPeers` / `acceptedSpotRouteChannelDiscoveryAttachesRouteDiscovery` | `acceptSpotRoutesFromChannel(...)`이 router capability, channel kind, peer acquisition을 검증하고 backend SpotNode의 router-channel peer/discovery attach로 이어짐 |
| Spot getOrCreate 1회 생성 | integration-single-process | `SpotManagerTest.spotManager_getOrCreate_createsOnceAndReusesExistingSpot` | 첫 호출은 생성하고 같은 rid의 두 번째 호출은 기존 Spot 재사용 |
| Spot timer/publish/remove | integration-single-process | `SpotManagerTest.spot_publishTimerAndRemove_stopCallbacksWork` | timer/publish/remove 의미 유지 |
| actor manager factory | integration-single-process | `ActorManagerTest.actorManager_createGetOrCreateFind_work` | create/getOrCreate/find 동작 |
| actor Entry Spot join call | fake backend | `ActorRuntimeFakeBackendTest.actorContextJoinEntrySpotUsesBackendSpotNodeJoinOperation` | `ZLinkActorContext.joinEntrySpot(...).submitAsync()`가 framework backend port를 통해 binding public `SpotNode.joinActorEntrySpot(...)` 의미로 이어지고, 결과 `ZLinkActorRef`를 actor context에 반영 |
| actor user Spot join call | fake backend | `ActorRuntimeFakeBackendTest.actorContextJoinSpotUsesBackendSpotNodeJoinOperationAndUpdatesContext` | `ZLinkActorContext.joinSpot(...).submitAsync(replyType)`가 framework backend port를 통해 binding public `SpotNode.joinActor(...)` 의미로 이어지고, join 성공 즉시 actor context의 joined 상태, `spotRid()`, `getSpot(Class)`를 user Spot 인스턴스로 갱신한 뒤 reply payload를 deserialize한다. 이후 Entry Spot join은 이전 user Spot 참조를 비운다 |
| actor Entry Spot route join handler | fake backend | `ActorRuntimeFakeBackendTest.actorEntrySpotRouteJoinHandlerCreatesLocalActorAndReturnsActorRefReply` | reserved route packet `__zlink.actor.joinEntrySpot` 처리 경로가 actor runtime에서 local actor를 생성하고 target node rid와 actor generation을 reply로 반환 |
| framework-owned session context | fake backend / sample regression / contract | `StreamSessionTest.constructorSessionContextExposesClientAndActorsFromFrameworkRuntime` / `SampleReleaseGateContractTest.ticTacToeSessionGatewayUsesActorGatewayAndFrameworkActorLocator` / `./framework/languages/java/samples/run_samples.sh` | STREAM session type은 framework-owned `ZLinkSessionContext` constructor를 받을 수 있고, context의 `client()`와 `actors()`는 backend stream send와 ActorGateway bind로 이어진다. sample은 `SampleSessionContext`, `SampleSessionActors`, `SampleBoundSession` 같은 local stand-in 없이 public `ZLinkFramework.sessionActors(...)`와 session context를 사용한다 |
| session packet dispatcher | fake backend / sample regression | `StreamSessionTest.sessionPacketDispatcher_handlesRegisteredPacketsAndLetsSessionRelayUnhandledPackets` / `./framework/languages/java/samples/run_samples.sh` | STREAM session type은 framework-owned `ZLinkSessionPacketDispatcher<ZLinkSessionContext>` constructor를 받을 수 있다. 등록된 `ZLinkSessionPacketHandler`는 packet 이름으로 찾아 실행되고, 미등록 packet은 session이 actor relay, reject, ignore 같은 결정을 직접 내리도록 `false`를 반환한다. Java/Kotlin `TicTacToe.SessionGateway` sample은 session handler 파일을 수동 helper가 아니라 framework dispatcher에 등록한다 |
| session actor relay | integration-single-process | `SessionActorsRuntimeIntegrationTest.bindAsyncUsesStreamActorGatewayBindingPath` | `bindAsync`와 ActorGateway binding path 동작 |
| remote ActorGateway relay | integration-multi-process | `RemoteActorGatewayTest.sessionAndPlayServers_relaySucceeds` | Session 서버와 Play 서버 사이 relay 성공 |
| stale binding token guard | integration-multi-process | `ActorSessionStateTest.actorSessionState_filtersStaleDisconnect_andOnlyDisconnectsCurrentStream` | 이전 binding이 새 binding을 지우지 않음 |
| bound session push | integration-multi-process | `BoundSessionTest.playActorPush_arrivesAtClientStream` | Play actor push가 client stream에 도착 |

## 4. STREAM/Connector regression

connector 테스트 이름은 `Systems.Zlink.Stream.Connector.Tests`의 메서드를 camelCase로
옮긴 대응이다.

| 항목 | 계층 | JUnit 테스트 | 통과 기준 |
|------|------|--------------|-----------|
| stream node duplicate session | unit | `NodesAndServicesTest.addZLinkFramework_throws_whenStreamNodeRegistersMultipleSessions` | startup validation 오류 |
| session connected/dispatch/reply | fake backend | `StreamSessionTest.headerSession_connectedDispatchReply_succeeds` | header session callback 성공 |
| session reply correlation | fake backend | `StreamSessionTest.constructorSessionContextExposesClientAndActorsFromFrameworkRuntime` / `sessionReply_failsOutsideRequestPacket` | session `client().reply(...)`는 현재 dispatch header의 request sequence가 있을 때만 response로 전송된다. plain send packet 처리 중에는 `.NET`과 같이 실패하고, request packet 처리 중에는 backend reply port에 원래 request sequence와 packet name을 유지한다 |
| session serial dispatch | fake backend | `StreamSessionTest.sameSessionCallbacks_runSerially` | 같은 session callback 순서 보장 |
| payload borrowed lifetime | contract | `StreamPayloadTest.responsePayload_requiresCopyOutsideTransportBuffer` | 실제 TCP response frame에서 받은 payload를 transport buffer 밖에서 안전하게 copy해 읽음 |
| connector TCP send frame | unit/integration-single-process | `ZLinkStreamConnectorTest.requestWritesFrameAndCorrelatesResponse` | connector가 loopback TCP server에 STREAM request frame을 쓰고 같은 request sequence의 response frame으로 pending request를 완료함 |
| connector WebSocket binary frame | unit/integration-single-process | `ZLinkStreamConnectorTest.webSocketRequestUsesBinaryFrameAndCorrelatesResponse` | connector가 `ws://` endpoint에 연결해 STREAM request frame을 binary WebSocket message로 보내고 같은 request sequence의 response binary message로 pending request를 완료함 |
| connector TLS raw stream | unit/integration-single-process | `ZLinkStreamConnectorTest.tlsRequestUsesEncryptedFrameAndSkippedCertificateValidation` | connector가 `tls://` endpoint에 연결해 self-signed server certificate 검증 우회 옵션으로 TLS handshake를 마치고 STREAM request/response frame을 주고받음 |
| connector WSS binary frame | unit/integration-single-process | `ZLinkStreamConnectorTest.wssRequestUsesBinaryFrameAndSkippedCertificateValidation` | connector가 `wss://` endpoint에 연결해 self-signed server certificate 검증 우회 옵션으로 WebSocket TLS handshake를 마치고 binary STREAM request/response frame을 주고받음 |
| connector TLS/WSS option parity | unit | `ZLinkStreamConnectorTest.skipServerCertificateValidationDefaultsToFalseAndCanBeEnabled` | `.NET`의 `SkipServerCertificateValidation`에 대응하는 Java public option 기본값과 명시 설정을 고정 |
| connector transport inference | unit | `ZLinkStreamConnectorTest.uriSchemeAndTransportMismatchIsRejected` | unsupported scheme 차단 |
| connector manual dispatch | unit/integration-single-process | `ConnectorDispatchTest.dispatch_invokesCallback` | loopback TCP server가 보낸 SEND frame을 manual queue에 넣고 `dispatchAsync()` 호출 시 callback 실행 |
| connector request timeout | unit/integration-single-process | `LifecycleTest.requestTimeoutFailsPendingRequestsWithTimeoutCause` / `ZLinkStreamConnectorTest.requestWithoutReplyFailsWithTimeoutCause` | 실제 TCP 연결 위에서 response가 오지 않는 pending request를 timeout으로 정리 |
| connector heartbeat | unit/integration-single-process | `LifecycleTest.heartbeatSendsReservedControlPing` / `inboundHeartbeatPingReceivesPongWhenHeartbeatDisabled` / `heartbeatTimeoutFailsPendingRequestsWithTimeoutCause` | reserved control ping/pong frame과 heartbeat timeout pending failure 의미 유지 |
| connector reconnect | unit/integration-single-process | `LifecycleTest.reconnectRestoresConnectionAfterTransportClose` / `reconnectFailsAfterMaxAttemptsWhenEndpointUnavailable` / `closeWhileReconnectingKeepsConnectorClosed` | 같은 endpoint로 TCP reconnect 상태 전환 성공, unavailable endpoint에서는 max attempts 뒤 DISCONNECTED로 전환, reconnect 지연 중 close는 CLOSED 상태를 유지 |
| reserved packet name 거부 | contract | `ZLinkStreamConnectorTest.reservedPacketNamesAreRejectedForUserHandlers` | 예약 packet 이름 거부 |
| stream wire protocol golden vector | contract | `ZLinkStreamWireProtocolTest.headerProtocol_matchesDotnetAndNodeGoldenVector` / `frameProtocol_matchesDotnetAndNodePrefixLayout` | `.NET`/Node와 같은 STREAM header/frame byte layout |
| Java/Node stream interop | contract | `JavaNodeStreamInteropTest.nodeConnector_decodesJavaRequestFrame_andJavaDecodesNodeResponse` | Java가 만든 STREAM request frame을 Node connector가 decode하고 Node response frame을 Java가 decode |
| connector codec helper | contract | `ConnectorCodecContractTest.jsonMsgpackProtobufTypedHelperRoundtrip` | JSON/MessagePack/Protobuf typed helper roundtrip |
| Kotlin connector wrapper | contract | `KotlinConnectorWrapperTest.suspendWrapperPreservesConnectorSemantics` / `connectorMessagesFlowUsesJavaManualDispatchSemantics` | suspend wrapper와 connector `Flow` wrapper가 Java connector lifecycle, manual dispatch, request/reply 의미를 바꾸지 않음 |

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
| socket event | unit | `MonitoringEventsTest.socketMonitoring_emitsConnectedEvent` | typed event handler 호출 |
| registry/spot snapshot diff | unit | `MonitoringEventsTest.registryMonitoring_emitsStatusChanged_forEmbeddedRegistry` / `spotMonitoring_emitsSubjectsChanged_whenSpotIsCreated` | 명시 `pollSnapshots()`가 변경분만 typed event handler로 발행 |
| handler failure policy | unit | `MonitoringRunnerTest.handlerFailure_recordsDiagnostic_withoutStopping` | monitoring runner 중단 없이 diagnostic 기록 |

## 5.1 Lifecycle regression

`StreamSessionTest.onError_reportsTransportError_forRemoteDisconnect`는
이미 생성된 session에 매칭되는 transport error만 `onErrorAsync(...)`로 전달하고,
session이 없는 transport error나 application dispatch 오류는 session error로
올리지 않는 정책을 fake backend로 고정한다. 실제 remote disconnect를 native
integration에서 닫으려면 Java binding `StreamSocket`에 session-correlatable
transport error callback public API가 추가되어야 한다.

| 항목 | 계층 | JUnit 테스트 | 통과 기준 |
|------|------|--------------|-----------|
| host start/stop | integration-single-process | `HostTest.host_startsAndStops_frameworkRuntimeContext` | `SmartLifecycle` 시작·종료에 맞춰 runtime context 생성·정리 |
| registry before framework + query DI | integration-single-process | `HostTest.host_startsEmbeddedRegistry_beforeFrameworkRuntime` / `host_doesNotRegisterRegistryQuery_withoutEmbeddedRegistry` | embedded registry가 framework runtime보다 먼저 시동하고, embedded registry가 있을 때만 `ZLinkRegistryQuery` bean 등록 |
| remote registry query client DI | integration-single-process | `HostTest.host_doesNotRegisterRegistryQueryClient_withoutCustomizer` / `host_registersRegistryQueryClient_whenCustomizerExists` / `host_rejectsRegistryQueryClientCustomizer_withoutEndpoint` | query client customizer가 있을 때만 `ZLinkRegistryQueryClient` bean 등록, endpoint 누락 validation |
| Spring multi-target clients | unit | `ZLinkFrameworkAutoConfigurationTest.autoConfigurationStartsFrameworkLifecycleAndExposesClientBean` / `multiTargetClientsThrowConfigurationExceptionWhenChannelIsMissing` | channel/fanout/route client bean 노출과 missing channel configuration error |
| Spring Spot publisher DI 노출 | unit | `ZLinkFrameworkAutoConfigurationTest.spotPublisherClientIsBeanOnlyWhenPublisherCapabilityExists` | attached Spot publisher capability가 있을 때만 `ZLinkSpotPublisherClient` bean 등록 |
| Spring handler constructor injection | unit | `ZLinkFrameworkAutoConfigurationTest.handlerFactoryCreatesHandlersWithSpringConstructorInjection` | Spring `BeanFactory` 기반 handler 생성으로 constructor dependency 주입 |
| Spring runtime event dispatcher | unit | `ZLinkFrameworkAutoConfigurationTest.runtimeEventDispatcherIsAlwaysRegistered` / `autoConfigurationKeepsUserRuntimeEventDispatcher` | framework runtime과 함께 dispatcher bean 등록, 사용자 제공 bean 유지 |
| stream transport error scope | fake backend | `StreamSessionTest.onError_reportsTransportError_forRemoteDisconnect` | remote disconnect만 session `onError`로 보고 |

## 6. Sample release gate

| Sample | 확인 기준 |
|--------|-----------|
| `TicTacToe` | direct STREAM + Spot + channel 흐름 성공 |
| `TicTacToe.SessionGateway` | reconnect 후 같은 actor id로 새 session binding |
| `Bingo` | 4 connector client, matching, timer, bound push 성공 |
| `StreamingClient` | connector send/request/on/manual dispatch/lifecycle event/reconnect smoke |
| `Async` | Java `CompletionStage` continuation과 Kotlin `suspend` wrapper smoke |

위 sample은 `samples/java/*`와 `samples/kotlin/*` 양쪽에 있어야 한다. sample source와 runner 구조는
`SampleReleaseGateContractTest.requiredSamplesExposeExecutableEntryPoints`,
`sampleSourcesUseOnlyPublicFrameworkAndConnectorApi`,
`ticTacToeSessionGatewayUsesActorGatewayAndFrameworkActorLocator`,
`ticTacToeSessionGatewayKotlinSampleMirrorsJavaRoleLayout`,
`bingoMirrorsFourClientMatchingTimerAndBoundPushGate`,
`bingoKotlinSampleMirrorsJavaRoleLayout`,
`streamingClientMirrorsConnectorSmokeGate`,
`streamingClientKotlinMirrorsConnectorSmokeGate`가 고정한다. Kotlin mirror gate도
파일 존재만 보지 않고 framework facade, ActorGateway attach, registry-backed remote
address, public session actor binding, connector manual dispatch/reconnect 사용을
검사한다. 실제 실행 self-check는 아래 release gate command가 담당한다.

`TicTacToe`, `TicTacToe.SessionGateway`, `Bingo`의 sample release gate는 단일 entry
file만 확인하지 않는다. Java/Kotlin 양쪽에서 `.NET` sample의 역할 package, handler,
model, player-client 파일이 존재하는지 함께 검사한다. `TicTacToe.SessionGateway`는
sample-local actor/session context stand-in 없이 framework-owned session context와
public session actor binding API를 사용해야 한다. 이렇게 해야 sample이 smoke check로
축소되는 회귀를 막을 수 있다.

release gate command:

```bash
./framework/languages/java/samples/run_samples.sh
```
