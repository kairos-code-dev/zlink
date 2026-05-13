<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework ASP.NET Core Registry Integration](aspnet-core-registry.ko.md) | [다음: ZLink Framework .NET Regression Test Matrix](regression-test-matrix.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [Lifecycle](./lifecycle-and-failure-semantics.ko.md)

# Draft -- ZLink Framework .NET Behavior Matrix

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` framework 구현에서 capability 조합별 기대 동작과
> startup validation 기준을 정리한다.

## 1. 목적

인터페이스와 샘플만으로는 "이 조합이 허용되는가", "무엇이 startup 실패인가"가
분명하지 않을 수 있다. 이 문서는 구현자가 registration surface를 만들 때 같은
입력에 항상 같은 판정을 하도록 기준을 고정한다.

## 2. 공통 원칙

- registration 단계에서 판정 가능한 설정 오류는 host startup 전에 fail-fast 한다.
- 같은 capability 안에서는 `Discovery`와 manual 연결을 섞지 않는다.
- manual 연결과 discovery 등록이 없는 outbound capability는 peer acquisition 경로가
  없으므로 startup validation에서 거부한다.
- duplicate registration은 조용히 덮어쓰지 않고 예외로 처리한다.

## 3. Channel Capability Matrix

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `EnableServer(server => server.Bind(...))`만 등록 | 허용 | server capability만 열고, handler mapping이 없으면 처리할 packet이 없다 |
| `EnableServer()`만 등록 + bind endpoint 없음 | 비허용 | startup validation 오류 |
| `EnableClient()`만 등록 + 전역 `UseDiscovery(...)` 있음 | 허용 | outbound request/send runtime을 만든다 |
| `EnableClient()`만 등록 + `UseManualConnections(...)` 있음 | 허용 | manual outbound request/send runtime을 만든다 |
| `EnableClient()`만 등록 + discovery/manual 둘 다 없음 | 비허용 | startup validation 오류 |
| `EnablePublisher(publisher => publisher.Bind(...))`만 등록 | 허용 | event publish만 가능하다 |
| `EnablePublisher()`만 등록 + bind endpoint 없음 | 비허용 | startup validation 오류 |
| `EnableSubscriber()`만 등록 + 전역 `UseDiscovery(...)` 있음 | 허용 | discovery 기반 event subscribe runtime을 만든다 |
| `EnableSubscriber()`만 등록 + `UseManualConnections(...)` 있음 | 허용 | manual subscribe runtime을 만든다 |
| `EnableSubscriber()`만 등록 + discovery/manual 둘 다 없음 | 비허용 | startup validation 오류 |
| 같은 channel에서 `server + client` 함께 등록 | 허용 | inbound와 outbound runtime을 모두 가진다 |
| 같은 channel에서 `publisher + subscriber` 함께 등록 | 허용 | event fan-out과 수신을 모두 가진다 |
| 같은 channel capability 안에서 discovery + manual 함께 등록 | 비허용 | startup validation 오류 |
| 같은 channel server에 같은 `kind + packetName` handler 중복 | 비허용 | startup validation 오류 |
| 다른 channel server에 같은 `kind + packetName` handler 등록 | 허용 | channel별 handler namespace를 분리한다 |
| `channel.MapHandlerGroup("...")`로 명시한 그룹의 handler만 그 channel에서 dispatch | 허용 | `[ZLinkHandlerGroup("...")]`와 mapping 조합으로 노출 범위 제한 |
| 같은 channel에 여러 그룹 매핑 | 허용 | `MapHandlerGroup`을 여러 번 호출해 그룹 union을 한 채널에 노출 |
| `MapHandlerGroup`이 가리키는 그룹에 handler 0개 | 비허용 | startup validation 오류 또는 경고 |

## 4. Spot Capability Matrix

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddSpotMesh(channel, configureMesh)` | 허용 | mesh가 active SPOT channel view와 node 집합을 함께 소유한다 |
| `AddSpotMesh(...)`에 `UseDiscovery(...)` 없음 | 비허용 | discovery 기반 SPOT mesh를 만들 수 없으므로 startup validation 오류 |
| `AddSpotNode(...)` standalone + local-only spot factory | 허용 | discovery mesh 없이 단일 local SpotNode를 띄운다 |
| `AddSpotNode(...)` standalone + mesh capability 사용 | 비허용 | router, pub/sub mesh, channel attach 같은 mesh 기능은 `AddSpotMesh(...)` 안에서 등록한다 |
| `UseSpotDiscovery(...)` + `AddSpotNode(...)` 분리 등록 | 허용(호환) | 기존 초안 compatibility 경로다. 새 문서와 샘플은 `AddSpotMesh(...)`를 권장한다 |
| 같은 mesh에 `AddNode(...)` 여러 개 | 허용 | 같은 channel view를 공유하는 여러 SpotNode를 등록한다 |
| 같은 `SpotNode`에 같은 `spotName` factory 중복 등록 | 비허용 | startup validation 오류 |
| 같은 `SpotNode`에 Entry Spot registry 중복 등록 | 비허용 | startup validation 오류 |
| `router` capability만 등록 | 허용 | inbound routed call만 받는다 |
| attach된 channel client capability 등록 + channel discovery/manual 경로 있음 | 허용 | spot 내부 outbound channel call 가능 |
| attach된 channel client capability 등록 + channel peer acquisition 경로 없음 | 비허용 | startup validation 오류 |
| local spot 없는 외부 publish는 `IZLinkSpotPublisherClient` 사용 | 허용 | 특정 SPOT channel publish만 수행한다 |

## 5. Stream Node Matrix

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddHeaderSession<T>()` 하나 등록 | 허용 | zlink stream header session node를 만든다 |
| 같은 node에 stream session을 둘 이상 등록 | 비허용 | startup validation 오류 |
| bind endpoint 없음 | 비허용 | startup validation 오류 |

### 5.1 Session Actor Dispatch Matrix

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddRouteChannel(...)` + 전역 `UseDiscovery(...)` 있음 | 허용 | discovery 기반 routed channel node를 만든다 |
| `AddRouteChannel(...)` + `UseManualConnections(...)` 있음 | 허용 | manual routed channel node를 만든다 |
| `AddRouteChannel(...)` + discovery/manual 둘 다 없음 | 비허용 | startup validation 오류 |
| routed channel bind endpoint 없음 | 비허용 | startup validation 오류 |
| 같은 routed channel에 같은 `kind + packetName` handler 중복 | 비허용 | startup validation 오류 |
| 같은 actor type factory 중복 등록 | 비허용 | builder 등록 시점 오류 |
| actor play route resolver 중복 등록 | 비허용 | builder 등록 시점 오류 |
| 같은 actor id가 새 stream session에서 다시 bind | 허용 | 기존 actor instance를 재사용하고 session binding token만 갱신 |
| actor factory가 요청 actor id와 다른 id 반환 | 비허용 | actor route와 binding id가 갈라지므로 actor 생성 오류 |
| `SessionProxy.Send(...)`가 stale binding 또는 닫힌 stream에 도착 | 허용된 실패 | 해당 push만 실패하고 route loop와 host shutdown은 계속 진행 |
| converter 없는 abstract/interface payload를 reply DTO에 포함 | 비허용 | startup validation 또는 첫 submit 전 configuration 오류 |
| spot route resolver 중복 등록 | 비허용 | builder 등록 시점 오류 |
| `IZLinkActorClient` 사용 + play route resolver 없음 | 비허용 | service 생성 또는 첫 호출에서 명확한 오류 |
| spot name/id 기반 client 또는 `JoinSpot(...)` 사용 + spot route resolver 없음 | 비허용 | service 생성 또는 첫 호출에서 명확한 오류 |
| `IZLinkSessionProxy` 사용 + actor-session binding 없음 | 비허용 | 현재 actor에 연결된 session이 없으면 명확한 오류 |

## 6. Monitoring Registration Matrix

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddZLinkMonitoring(...)` + 등록된 socket source 이름 | 허용 | 해당 source에 event handler를 붙인다 |
| `AddZLinkMonitoring(...)` + 등록된 registry source 이름 | 허용 | snapshot diff polling을 시작한다 |
| `AddZLinkMonitoring(...)` + 등록된 spot source 이름 | 허용 | snapshot diff polling을 시작한다 |
| `AddZLinkMonitoring(...)` + discovery source 이름 | 비허용 | discovery 상태는 registry snapshot/query로 조회한다 |
| 존재하지 않는 source 이름 등록 | 비허용 | startup validation 오류 |
| polling source인데 interval이 0 이하 | 비허용 | startup validation 오류 |

## 7. Registry Matrix

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddZLinkRegistry(...)`만 등록 | 허용 | standalone registry host가 된다 |
| `AddZLinkFramework(...)` + `AddZLinkRegistry(...)` 함께 등록 | 허용 | embedded registry + framework runtime을 함께 띄운다 |
| embedded 구성에서 `UseDiscovery(...)` endpoint를 자동 추론 | 비허용 | endpoint는 명시적으로 적어야 한다 |
| `AddZLinkRegistryQueryClient(...)`만 등록 | 허용 | 원격 topology query만 수행한다 |

## 8. Startup Validation Error 목록

아래 항목은 구현 시 공통적으로 예외를 던져야 하는 입력이다.

- duplicate channel 이름
- duplicate `spotNodeName`
- duplicate `spotName` factory
- outbound capability에 discovery/manual 경로가 모두 없음
- 같은 capability 안에서 discovery/manual 혼용
- 존재하지 않는 source를 monitoring에 등록
- 같은 stream node에 session 중복 등록
- bind endpoint가 없는 stream node
- routed channel bind endpoint 없음
- 같은 channel server의 같은 `kind + packetName` handler 중복
- 같은 routed channel의 같은 `kind + packetName` handler 중복

이 오류들은 런타임에 늦게 드러내지 않고 startup 단계에서 막는 편을 기본으로 본다.
다만 actor resolver 누락처럼 실제 service 사용 여부가 있어야 드러나는 항목은
해당 service 생성 또는 첫 호출에서 같은 error family로 명확하게 실패시킨다.

## 9. 회귀 테스트

Behavior Matrix는 허용 조합과 비허용 조합을 테스트 이름으로 고정한다. 새 capability
조합을 추가할 때는 표만 늘리지 말고, startup validation 또는 runtime integration
테스트도 같은 변경에 포함한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegistrationValidationTests.AddZLinkFramework_Throws_WhenCompatibilityChannelMixesAutoConnectTypes` | client/server와 fanout capability를 호환 등록 경로에서 잘못 섞으면 실패한다. |
| `RegistrationValidationTests.AddZLinkFramework_Throws_WhenRouteChannelMixesDiscoveryAndManualConnections` | 같은 routed capability에서 Discovery와 manual 연결을 섞으면 실패한다. |
| `RegistrationValidationTests.AddZLinkFramework_Throws_WhenServerHasNoBindEndpoint` | server capability에 bind endpoint가 없으면 실패한다. |
| `RegistrationValidationTests.AddZLinkFramework_Throws_WhenPublisherHasNoBindEndpoint` | publisher capability에 bind endpoint가 없으면 실패한다. |
| `RegistrationValidationTests.AddZLinkFramework_AllowsStandaloneLocalSpotNode` | Discovery mesh 없이 local-only SpotNode를 단독으로 시작할 수 있다. |
