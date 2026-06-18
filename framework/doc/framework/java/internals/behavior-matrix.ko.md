<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [Lifecycle](lifecycle-and-failure-semantics.ko.md)
<!-- framework-adapter-nav:end -->

# Java Behavior Matrix

## 1. Startup validation

| 항목 | 판정 |
|------|------|
| duplicate channel name | startup validation 오류 |
| 같은 channel에 중복 `kind + packetName` handler | startup validation 오류 |
| client 역할에 discovery/manual 경로 없음 | startup validation 오류 |
| 같은 역할에서 discovery와 manual connection 혼용 | startup validation 오류 |
| server/publisher/route/stream endpoint 누락 | startup validation 오류 |
| duplicate actor type factory | startup validation 오류 |
| actor factory without SpotNode | startup validation 오류 |
| duplicate Spot factory type | startup validation 오류 |
| duplicate Entry Spot registration | startup validation 오류 |
| stream node에 session type 둘 이상 등록 | startup validation 오류 |
| route mesh 없이 Registry-backed Spot remote resolver 사용 | startup validation 오류 |
| monitoring source 이름 불일치 | startup validation 오류 |
| registry pub/router endpoint 누락 | startup validation 오류 |

## 2. Runtime event 또는 호출 실패

| 항목 | 판정 |
|------|------|
| request timeout | 호출 실패, pending request 정리 |
| discovery provider down | runtime event |
| peer disconnect | runtime event |
| stale bound session send | 호출 실패 또는 diagnostic event, host shutdown 실패로 번지지 않음 |
| timer handler exception | Spot monitoring event |
| connector user callback exception | connector error event |
| registry polling 실패 | monitoring diagnostic event |

## 3. Dispatch ordering

| 항목 | 기준 |
|------|------|
| channel request handler | local server ingress 기준 dispatch |
| outbound dealer receive | reply correlation 전용 |
| Spot user handler | 같은 Spot 실행 문맥에서 직렬화 |
| Entry Spot actor packet | Entry Spot 실행 줄에서 직렬화 |
| STREAM session callback | 같은 session 안에서 직렬화 |
| actor/session relay | ActorGateway 경로 사용 |

## 4. Sample 금지 회귀

| 항목 | 판정 |
|------|------|
| sample-only route store | 금지 |
| sample-only metadata store | 금지 |
| session relay JSON envelope | 금지 |
| readiness sleep 우회 | 금지 |
| connector 없이 client stream 흉내 | 금지 |

## 5. JUnit 테스트 이름 매핑

Behavior Matrix의 판정은 `.NET` 테스트(`Channels`, `HandlerExposure`,
`NodesAndServices`, `RegistryAndMonitoring`)를 camelCase JUnit 메서드로 옮겨 고정한다.
확인 기준(의미)은 그대로 유지하며 `.NET` 코드가 최종 기준이다. (`.NET`의
`AddZLinkFramework_*` prefix는 Java에서 `addZLinkFramework_*`로 옮긴다.)

| 판정 항목 | JUnit 테스트 (클래스 / 메서드) |
|-----------|--------------------------------|
| duplicate channel name | `ChannelsTest.addZLinkFramework_throws_whenChannelNameIsDuplicated` |
| client 역할에 discovery/manual 경로 없음 | `ChannelsTest.addZLinkFramework_throws_whenClientHasNoPeerAcquisitionPath` |
| 같은 route 역할에서 discovery + manual 혼용 | `ChannelsTest.addZLinkFramework_throws_whenRouteChannelMixesDiscoveryAndManualConnections` |
| server endpoint 누락 | `HandlerExposureTest.addZLinkFramework_throws_whenServerHasNoBindEndpoint` |
| publisher endpoint 누락 | `RegistryAndMonitoringTest.addZLinkFramework_throws_whenPublisherHasNoBindEndpoint` |
| handler group exposure 없음 | `HandlerExposureTest.addZLinkFramework_throws_whenServerHasNoHandlerExposure` |
| client-server spotRouteEgress에 client 없음 | `HandlerExposureTest.addZLinkFramework_throws_whenClientServerSpotRouteEgressHasNoClient` |
| stream node에 session 둘 이상 등록 | `NodesAndServicesTest.addZLinkFramework_throws_whenStreamNodeRegistersMultipleSessions` |
| duplicate Spot factory type | `NodesAndServicesTest.addZLinkFramework_throws_whenSpotFactoryTypeIsDuplicatedAcrossNodes` |
| duplicate Entry Spot registration | `NodesAndServicesTest.addZLinkFramework_throws_whenSpotNodeRegistersMultipleEntrySpots` |
| duplicate actor type factory | `NodesAndServicesTest.addZLinkFramework_throws_whenActorFactoryNameIsDuplicated` |
| actor factory without SpotNode | `NodesAndServicesTest.addZLinkFramework_throws_whenActorFactoryWithoutSpotNode` |
| stream ActorGateway node without router | `NodesAndServicesTest.addZLinkFramework_throws_whenStreamAttachesActorGatewayNodeWithoutRouter` |
| local-only SpotNode 허용 | `NodesAndServicesTest.addZLinkFramework_allowsStandaloneLocalSpotNode` |
| spot mesh가 전역 discovery 상속 허용 | `NodesAndServicesTest.addZLinkFramework_allowsSpotMeshToInheritGlobalDiscovery` |
| monitoring source 이름 불일치 | `RegistryAndMonitoringTest.addZLinkMonitoring_throws_whenSocketSourceIsUnknownOnStartup` |
| monitoring source가 등록 역할과 불일치 | `RegistryAndMonitoringTest.addZLinkMonitoring_throws_whenSocketSourceDoesNotMatchRegisteredCapability` |
| registry pub endpoint 누락 | `RegistryAndMonitoringTest.addZLinkRegistry_throws_whenPubEndpointIsMissing` |
| registry router endpoint 누락 | `RegistryAndMonitoringTest.addZLinkRegistry_throws_whenRouterEndpointIsMissing` |
| session actor dispatch가 actor remote address resolver를 요구하지 않음 | `NodesAndServicesTest.sessionActorDispatch_doesNotRequireActorRemoteAddressResolver` |
| send/publish async submit가 caller를 막지 않음 | `ZLinkAsyncSubmitterTest.submitAsync_failsPendingItemWhenSendTimeoutExpires` |
| pending request cleanup(stop) | `ZLinkAsyncSubmitterTest.disposeAsync_failsPendingItems` |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [Lifecycle](lifecycle-and-failure-semantics.ko.md)
<!-- framework-adapter-nav:bottom:end -->
