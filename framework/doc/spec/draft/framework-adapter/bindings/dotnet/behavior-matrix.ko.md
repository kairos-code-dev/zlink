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
| `EnableServer(server => server.Bind(...))`만 등록 | 허용 | local request/send handler만 받는다 |
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

## 4. Spot Capability Matrix

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddSpotNode(...)` + 전역 `UseSpotDiscovery(...)` 있음 | 허용 | active SPOT channel view를 공유한다 |
| `AddSpotNode(...)`만 있고 `UseSpotDiscovery(...)` 없음 | 비허용 | discovery 기반 SPOT mesh를 만들 수 없으므로 startup validation 오류 |
| 같은 앱에 `AddSpotNode(...)` 여러 개 | 허용 | 같은 `UseSpotDiscovery(...)`가 만든 channel view를 공유한다 |
| 같은 `SpotNode`에 같은 `spotName` factory 중복 등록 | 비허용 | startup validation 오류 |
| `router` capability만 등록 | 허용 | inbound routed call만 받는다 |
| attach된 channel client capability 등록 + global channel discovery/manual 경로 있음 | 허용 | spot 내부 outbound channel call 가능 |
| attach된 channel client capability 등록 + channel peer acquisition 경로 없음 | 비허용 | startup validation 오류 |
| local spot 없는 외부 publish는 `IZLinkSpotPublisherClient` 사용 | 허용 | 특정 SPOT channel publish만 수행한다 |

## 5. Stream Node Matrix

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddPacketSession<T>()` 하나 등록 | 허용 | packet session node를 만든다 |
| `AddRawSession<T>()` 하나 등록 | 허용 | raw session node를 만든다 |
| 같은 node에 `AddPacketSession<T>()`와 `AddRawSession<T>()` 함께 등록 | 비허용 | startup validation 오류 |
| 같은 node에 packet session 둘 이상 등록 | 비허용 | startup validation 오류 |
| 같은 node에 raw session 둘 이상 등록 | 비허용 | startup validation 오류 |
| bind endpoint 없음 | 비허용 | startup validation 오류 |

## 6. Monitoring Registration Matrix

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `AddZLinkMonitoring(...)` + 등록된 socket/discovery source 이름 | 허용 | 해당 source에 event handler를 붙인다 |
| `AddZLinkMonitoring(...)` + 등록된 registry source 이름 | 허용 | snapshot diff polling을 시작한다 |
| `AddZLinkMonitoring(...)` + 등록된 spot source 이름 | 허용 | snapshot diff polling을 시작한다 |
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
- duplicate `actorType` factory
- outbound capability에 discovery/manual 경로가 모두 없음
- 같은 capability 안에서 discovery/manual 혼용
- 존재하지 않는 source를 monitoring에 등록
- 같은 stream node에 raw/packet session 혼용
- bind endpoint가 없는 stream node

이 오류들은 런타임에 늦게 드러내지 않고 startup 단계에서 막는 편을 기본으로 본다.
