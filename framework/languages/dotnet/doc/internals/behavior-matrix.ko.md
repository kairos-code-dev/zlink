<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Bingo Game Sample](../guide/samples/bingo-game-sample.ko.md) | [다음: ZLink Framework .NET DI Capability Exposure Policy](./di-capability-exposure-policy.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[.NET 묶음](../README.ko.md) | [인터페이스](../spec/handler-interfaces.ko.md) | [channel](../spec/aspnet-core-channel-messaging.ko.md) | [SPOT](../spec/aspnet-core-spot.ko.md) | [STREAM](../spec/aspnet-core-stream.ko.md) | [Lifecycle](./lifecycle-and-failure-semantics.ko.md)

# ZLink Framework .NET Behavior Matrix

## 1. 목적

인터페이스 정의와 샘플 코드만 봐서는 다음 같은 질문에 답하기 어려울 수 있다.

- "이 조합을 허용해도 되는가"
- "무엇을 startup 실패로 봐야 하는가"

이 문서의 목적은 구현자가 registration surface[^registration-surface] 를 만들
때, 같은 입력에 대해 항상 같은 판정을 내리도록 기준을 미리 고정해 두는 것이다.

## 2. 공통 원칙

각 capability 표를 읽기 전에, 표 전체에 공통으로 적용되는 다음 네 가지를 먼저
짚어 둔다.

- 등록 단계에서 이미 판정할 수 있는 설정 오류는 host 가 시작되기 전에
  fail-fast[^fail-fast] 한다.
- 같은 capability 안에서는 `Discovery` 기반 자동 연결과 manual 연결을 섞지
  않는다.
- outbound capability 에 manual 연결도 없고 discovery 등록도 없으면 거부한다.
  peer 를 어디서 가져올지 알 길이 없기 때문이다.
- 같은 항목을 중복으로 등록한 경우, 조용히 덮어쓰지 않고 예외로 처리한다.

## 3. Channel Capability Matrix

다음 표는 channel 의 capability 조합별 허용 여부와 기대 동작을 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `EnableServer(server => server.Bind(...))`만 등록 | 허용 | server capability만 열리고, handler 매핑이 없으면 처리할 packet도 없다 |
| `EnableServer()`만 등록 + bind endpoint 없음 | 비허용 | startup validation 오류 |
| `EnableClient()`만 등록 + 전역 `UseDiscovery(...)` 있음 | 허용 | outbound request/send runtime을 만든다 |
| `EnableClient()`만 등록 + `UseManualConnections(...)` 있음 | 허용 | manual 기반 outbound request/send runtime을 만든다 |
| `EnableClient()`만 등록 + discovery/manual 둘 다 없음 | 비허용 | startup validation 오류 |
| `EnablePublisher(publisher => publisher.Bind(...))`만 등록 | 허용 | event publish만 가능하다 |
| `EnablePublisher()`만 등록 + bind endpoint 없음 | 비허용 | startup validation 오류 |
| `EnableSubscriber()`만 등록 + 전역 `UseDiscovery(...)` 있음 | 허용 | discovery 기반 event subscribe runtime을 만든다 |
| `EnableSubscriber()`만 등록 + `UseManualConnections(...)` 있음 | 허용 | manual 기반 subscribe runtime을 만든다 |
| `EnableSubscriber()`만 등록 + discovery/manual 둘 다 없음 | 비허용 | startup validation 오류 |
| 같은 channel에서 `server + client` 함께 등록 | 허용 | inbound와 outbound runtime을 모두 가진다 |
| 같은 channel에서 `publisher + subscriber` 함께 등록 | 허용 | event fan-out과 수신을 모두 가진다 |
| 같은 channel capability 안에서 discovery + manual 함께 등록 | 비허용 | startup validation 오류 |
| 같은 channel server에 같은 `kind + packetName` handler 중복 | 비허용 | startup validation 오류 |
| 다른 channel server에 같은 `kind + packetName` handler 등록 | 허용 | channel별로 handler namespace가 분리되어 있다 |
| `channel.MapHandlerGroup("...")`로 명시한 그룹의 handler만 그 channel에서 dispatch | 허용 | `[ZLinkHandlerGroup("...")]` attribute와 매핑을 조합해서 노출 범위를 제한한다 |
| 같은 channel에 여러 그룹 매핑 | 허용 | `MapHandlerGroup`을 여러 번 호출해 그룹들의 합집합을 한 채널에 노출한다 |
| `MapHandlerGroup`이 가리키는 그룹에 handler 0개 | 비허용 | startup validation 오류 또는 경고 |

## 4. Spot Capability Matrix

다음 표는 SPOT 의 등록 조합별 허용 여부와 기대 동작을 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddSpotMesh(channel, configureMesh)` | 허용 | mesh가 활성 SPOT[^spot] channel view와 node 집합을 함께 소유한다 |
| `AddSpotMesh(...)`에 `UseDiscovery(...)` 없음 | 비허용 | discovery 기반 SPOT mesh를 만들 수 없으므로 startup validation 오류 |
| `AddSpotNode(...)` standalone + local-only spot factory | 허용 | discovery mesh 없이 단일 local SpotNode 하나만 띄운다 |
| `AddSpotNode(...)` standalone + mesh capability 사용 | 비허용 | router, pub/sub mesh, channel attach 같은 mesh 기능은 반드시 `AddSpotMesh(...)` 안에서 등록해야 한다 |
| `UseSpotDiscovery(...)` + `AddSpotNode(...)` 분리 등록 | 허용(호환) | 기존 초안과의 호환 경로다. 새 문서와 샘플은 `AddSpotMesh(...)`를 권장한다 |
| 같은 mesh에 `AddNode(...)` 여러 개 | 허용 | 같은 channel view를 공유하는 여러 SpotNode를 등록한다 |
| 같은 `SpotNode`에 같은 `spotName` factory 중복 등록 | 비허용 | startup validation 오류 |
| 같은 `SpotNode`에 Entry Spot[^entry-spot] registry 중복 등록 | 비허용 | startup validation 오류 |
| `router` capability만 등록 | 허용 | inbound routed call만 받는다 |
| attach된 channel client capability 등록 + channel discovery/manual 경로 있음 | 허용 | spot 내부에서 outbound channel 호출이 가능하다 |
| attach된 channel client capability 등록 + channel peer acquisition 경로 없음 | 비허용 | startup validation 오류 |
| local spot factory 없는 외부 publish node는 `IZLinkSpotPublisherClient` 사용 | 허용 | Spot publisher client capability 만 둔 `SpotNode` 로 특정 SPOT channel publish만 수행한다 |

## 5. Stream Node Matrix

다음 표는 stream node 등록의 허용 조합을 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddHeaderSession<T>()` 하나 등록 | 허용 | zlink stream header session node를 만든다 |
| 같은 node에 stream session을 둘 이상 등록 | 비허용 | startup validation 오류 |
| bind endpoint 없음 | 비허용 | startup validation 오류 |

### 5.1 Session Actor Dispatch Matrix

다음 표는 session actor dispatch 와 routed channel 의 허용 조합을 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddRouteChannel(...)` + 전역 `UseDiscovery(...)` 있음 | 허용 | discovery 기반 routed channel node를 만든다 |
| `AddRouteChannel(...)` + `UseManualConnections(...)` 있음 | 허용 | manual 기반 routed channel node를 만든다 |
| `AddRouteChannel(...)` + discovery/manual 둘 다 없음 | 비허용 | startup validation 오류 |
| routed channel bind endpoint 없음 | 비허용 | startup validation 오류 |
| 같은 routed channel에 같은 `kind + packetName` handler 중복 | 비허용 | startup validation 오류 |
| 같은 actor type factory 중복 등록 | 비허용 | builder 등록 시점에 오류 |
| actor play route resolver 중복 등록 | 비허용 | builder 등록 시점에 오류 |
| 같은 actor id가 새 stream session에서 다시 bind | 허용 | 기존 actor 인스턴스를 재사용하고 session binding token[^binding-token]만 갱신한다 |
| actor factory가 요청 actor id와 다른 id를 반환 | 비허용 | actor route와 binding id가 갈라지므로 actor 생성 오류 |
| `context.SessionProxy.Send(...)` 또는 `IZLinkActorSessionClient.Send(actorId, ...)` 가 오래된 binding이나 이미 닫힌 stream에 도착 | 허용된 실패 | 해당 push만 실패하고, route loop와 host shutdown은 계속 진행한다 |
| converter 없는 abstract/interface payload를 reply DTO에 포함 | 비허용 | startup validation 또는 첫 submit 직전에 configuration 오류 |
| spot route resolver 중복 등록 | 비허용 | builder 등록 시점에 오류 |
| Registry actor route 기본 구현 + custom actor route resolver 함께 등록 | 비허용 | startup validation 오류 |
| Registry Spot route 기본 구현 + custom Spot route resolver 함께 등록 | 비허용 | startup validation 오류 |
| Registry actor-session binding 기본 구현 + custom actor-session binding store 함께 등록 | 비허용 | startup validation 오류 |
| Registry route 기본 구현 + `UseDiscovery(...)` 없음 | 비허용 | startup validation 오류 |
| Registry route 기본 구현 + route mesh channel이 둘 이상이고 channel id 생략 | 비허용 | startup validation 오류 |
| spot name/id 기반 client 또는 `JoinSpot(...)` 사용 + spot route resolver 없음 | 비허용 | service 생성 또는 첫 호출에서 명확한 오류 |
| `IZLinkSessionProxy` 또는 `IZLinkActorSessionClient` 사용 + actor-session binding 없음 | 비허용 | 대상 actor에 묶인 session이 없으면 명확한 오류 |

## 6. Monitoring Registration Matrix

다음 표는 monitoring 등록의 허용 조합을 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddZLinkMonitoring(...)` + 등록된 socket source 이름 | 허용 | 해당 source에 event handler를 연결한다 |
| `AddZLinkMonitoring(...)` + 등록된 registry source 이름 | 허용 | snapshot diff polling[^snapshot-diff-polling]을 시작한다 |
| `AddZLinkMonitoring(...)` + 등록된 spot source 이름 | 허용 | snapshot diff polling을 시작한다 |
| `AddZLinkMonitoring(...)` + discovery source 이름 | 비허용 | discovery 상태는 registry snapshot/query로 조회하는 것이 원칙이다 |
| 존재하지 않는 source 이름 등록 | 비허용 | startup validation 오류 |
| polling source인데 interval이 0 이하 | 비허용 | startup validation 오류 |

## 7. Registry Matrix

다음 표는 Registry 등록 조합의 허용 여부를 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddZLinkRegistry(...)`만 등록 | 허용 | standalone registry host가 된다 |
| `AddZLinkFramework(...)` + `AddZLinkRegistry(...)` 함께 등록 | 허용 | embedded registry와 framework runtime을 한 호스트에서 함께 띄운다 |
| embedded 구성에서 `UseDiscovery(...)` endpoint를 자동 추론 | 비허용 | endpoint는 반드시 명시적으로 적어야 한다 |
| `AddZLinkRegistryQueryClient(...)`만 등록 | 허용 | 원격 topology[^topology] 조회만 수행한다 |

## 8. Startup Validation Error 목록

아래 항목은 구현체가 공통적으로 예외를 던져야 하는 잘못된 입력이다.

- duplicate channel 이름
- duplicate `spotNodeName`
- duplicate `spotName` factory
- outbound capability에 discovery/manual 경로가 둘 다 없는 경우
- 같은 capability 안에서 discovery/manual 혼용
- monitoring 등록 시 존재하지 않는 source를 지정한 경우
- 같은 stream node에 session을 중복 등록한 경우
- bind endpoint가 없는 stream node
- routed channel bind endpoint가 없는 경우
- 같은 channel server에서 `kind + packetName`이 같은 handler 중복
- 같은 routed channel에서 `kind + packetName`이 같은 handler 중복

이 오류들은 런타임에 뒤늦게 드러내지 않고, startup 단계에서 미리 막는 것이
기본이다.

다만 actor resolver 누락처럼 실제로 service 가 사용되는 시점이 되어야 드러나는
항목도 있다. 이런 항목은 해당 service 가 생성되거나 처음 호출되는 시점에, 같은
error family 로 명확하게 실패시킨다.

## 9. 회귀 테스트

Behavior Matrix 는 허용 조합과 비허용 조합을 테스트 이름 단위로 고정한다. 새
capability 조합을 추가할 때는 표만 늘리지 않는다. startup validation 또는
runtime integration 테스트도 같은 변경에 함께 포함시킨다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegistrationValidationTests.AddZLinkFramework_Throws_WhenCompatibilityChannelMixesAutoConnectTypes` | client/server와 fanout capability를 호환 등록 경로에서 잘못 섞으면 실패한다. |
| `RegistrationValidationTests.AddZLinkFramework_Throws_WhenRouteChannelMixesDiscoveryAndManualConnections` | 같은 routed capability에서 Discovery와 manual 연결을 섞으면 실패한다. |
| `RegistrationValidationTests.AddZLinkFramework_Throws_WhenServerHasNoBindEndpoint` | server capability에 bind endpoint가 없으면 실패한다. |
| `RegistrationValidationTests.AddZLinkFramework_Throws_WhenPublisherHasNoBindEndpoint` | publisher capability에 bind endpoint가 없으면 실패한다. |
| `RegistrationValidationTests.AddZLinkFramework_AllowsStandaloneLocalSpotNode` | Discovery mesh 없이 local-only SpotNode를 단독으로 시작할 수 있다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^capability]: capability 는 어떤 노드(channel, spot 등)가 외부에 노출하는 역할이나 기능 단위(예: server, client, publisher, subscriber)를 가리킨다.
[^startup-validation]: startup validation 은 host가 시작되기 전에 등록된 설정을 검사해 잘못된 조합을 미리 거부하는 단계다.
[^registration-surface]: registration surface 는 DI 컨테이너에 framework 구성 요소를 등록할 때 사용자가 호출하는 builder 메서드 묶음을 가리킨다.
[^fail-fast]: fail-fast 는 잘못된 설정이나 상태를 발견하면 즉시 예외를 던지고 실행을 멈추는 전략이다. 늦게 발견되어 더 큰 문제로 번지는 것을 막는다.
[^spot]: `SPOT` 은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다. `SpotNode` 는 하나 이상의 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^entry-spot]: Entry Spot 은 SpotNode 가 접속한 actor 를 가장 먼저 받아들이는 진입용 spot 이다. 이후 user Spot 으로 옮겨 가기 전 단계 역할을 한다.
[^binding-token]: session binding token 은 actor 와 stream session 의 연결 상태를 식별하는 토큰으로, 재연결 시 어느 binding 이 최신인지 구분하는 데 쓰인다.
[^snapshot-diff-polling]: snapshot diff polling 은 일정 주기로 전체 스냅샷을 가져온 뒤 직전 스냅샷과 비교해 변경된 부분만 이벤트로 올리는 방식이다.
[^topology]: topology 는 어떤 노드(channel, spot, registry 등)가 어디에 있는지, 그리고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
