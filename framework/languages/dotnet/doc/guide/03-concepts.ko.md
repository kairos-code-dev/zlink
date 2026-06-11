<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Getting Started](./02-getting-started.ko.md) | [다음: Channel Messaging — request · send · pub/sub](./04-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->

# 핵심 개념 — .NET 표면 멘탈 모델

> 개념의 정식 의미는 공통 스펙([interaction-model](../../../../doc/spec/interaction-model.ko.md),
> [message-model](../../../../doc/spec/message-model.ko.md),
> [channel-topology](../../../../doc/spec/channel-topology.ko.md))이, 인터페이스의
> 정식 정의는 [spec/handler-interfaces](../spec/handler-interfaces.ko.md)가
> 소유한다. 이 문서는 그 의미가 `.NET`에서 어떤 모양으로 보이는지 정리한다.

이 다섯 가지 개념만 잡으면 나머지 챕터가 전부 변주로 읽힌다: **channel · capability ·
handler · client · DI/lifecycle**.

## 0. 용어 빠르게 잡기 (주니어용)

가이드에 자주 나오는 용어를 **한 줄 풀이**로 먼저 잡는다. 다른 챕터에서 낯선 단어가
나오면 이 표로 돌아오면 된다(정식 정의는 위 spec 링크가 소유).

| 용어 | 한 줄 풀이 |
|------|-----------|
| **channel(채널)** | 호출을 묶는 **논리 이름**. `host:port` 주소 대신 `"orders"` 같은 이름으로 부른다 |
| **capability(역할/능력)** | 한 channel 이 맡는 역할 — 서버로 **받기**(EnableServer) / 클라이언트로 **보내기**(EnableClient) / **발행**(Publisher) / **구독**(Subscriber) |
| **handler(핸들러)** | 들어온 메시지를 처리하는 메서드·클래스. `ASP.NET Core` 의 컨트롤러 액션과 같은 위치 |
| **client(클라이언트)** | 다른 서비스로 호출을 **보내는** 주입 객체(예: `IZLinkChannelClient`) |
| **request / send / publish** | 각각 **응답 받는 호출** / **응답 없는 단방향 통지** / **여러 구독자에게 발행** |
| **pub/sub · fan-out** | 한 번 발행한 이벤트가 **여러 구독자에게 동시에 퍼지는** 것 |
| **packet name(패킷 이름)** | 같은 channel 안에서 **어느 메시지 종류인지** 구분하는 키 |
| **codec(코덱)** | payload(메시지 본문)를 바이트로 **직렬화/역직렬화**하는 방식(json·protobuf·messagepack) |
| **SPOT(스팟)** | room/zone 처럼 **동적으로 생겼다 사라지는 상태 노드**. 한 SPOT 의 콜백은 **한 줄로 직렬** 실행돼 lock 이 필요 없다 |
| **actor(액터)** | **ID 로 식별되는 상태 보유 객체**. 같은 ID 로 온 메시지는 늘 같은 인스턴스가 처리 |
| **Entry Spot** | actor 가 생성 직후 머무는 **기본 실행 위치** |
| **STREAM(스트림)** | 외부 client(모바일·게임)와의 **연결 지향 양방향 채널**. 연결 수명·재연결을 framework 가 관리 |
| **session(세션)** | STREAM 연결 하나에 대응하는 **서버 측 객체** |
| **Registry(레지스트리)** | 어떤 서비스가 어디 떠 있는지 모으는 **중앙 디렉터리 서버** |
| **Discovery(디스커버리)** | client 가 Registry 를 보고 **연결 대상을 자동으로 찾는** 것 |
| **RoutingId** | 노드·스팟의 **논리 주소**(특정 인스턴스를 가리키는 식별자) |
| **correlation(상관)** | 요청과 그 응답을 **짝지어 주는** 식별 정보. framework 가 자동 처리 |
| **deadline / timeout** | 응답을 **얼마나 기다릴지**의 상한 시간 |
| **DI / lifecycle** | `ASP.NET Core` 의존성 주입 + hosted service **시작/종료** 수명 관리 |
| **mesh / sidecar**(비교용) | 서비스 옆에 붙어 라우팅·분배를 대신하는 **별도 프록시**(Envoy/Istio). ZLink 는 이게 없어도 된다 |

## 1. channel name 이 호출의 중심이다

호출 단위는 주소가 아니라 논리 `channel name` 이다. 같은 channel 이 여러 노드에
떠 있어도 응용은 channel 이름만 쓰고, channel 별 `Discovery`가 위치를 해결한다.
배포 환경 값(주소·topology)은 handler 가 아니라 **channel 등록**이 소유한다.

그래서 `[ZLinkRequest]`, `[ZLinkSend]`, `[ZLinkPublish]` attribute 는 channel
이름을 인자로 받지 않는다. channel 이름은 배포의 값이고, 코드의 값이 아니다.

> **주의:** channel 이름과 handler **group 이름**은 서로 다른 namespace 다.
> group 은 코드 안 논리 묶음(`"api"`)이고, channel 은 배포 식별자(`"tictactoe.api"`)다.
> 같은 group 을 여러 channel 에 매핑할 수 있다.

## 2. 상호작용 모델 ↔ .NET 표면

| 공통 모델 | handler 인터페이스 | attribute | outbound 호출 |
|-----------|--------------------|-----------|----------------|
| request-response | `IZLinkRequestHandler<TReq,TRes>` | `[ZLinkRequest]` | `client.RequestToChannel(...).Async<TRes>(ct)` |
| command(단방향 send) | `IZLinkSendHandler<TMsg>` | `[ZLinkSend]` | `client.SendToChannel(...).Async(ct)` |
| publish-subscribe | `IZLinkPublishHandler<TEvt>` | `[ZLinkPublish]` | `publisher.PublishSpot(...).Async(ct)` |
| SPOT 내부/외부 | `IZLinkSpot*Handler<...>` | (Spot 등록) | `IZLinkSpotOutbound` |
| STREAM session | `IZLinkSession` | (stream 등록) | `IZLinkSessionContext` / `IZLinkBoundSession` |

handler 는 결과를 **반환값**으로 돌려준다. request handler 는 `ValueTask<TReply>`,
send/publish handler 는 `ValueTask` 다.

## 3. capability — channel 이 맡는 역할

channel 은 `AddZLinkFramework(options => ...)` 안에서 등록하고, 그 channel 이 가질
역할을 capability 로 선언한다. transport 매핑은 channel 종류가 정한다.

| 등록 메서드 | transport | capability |
|-------------|-----------|------------|
| `AddClientServerChannel` | DEALER → ROUTER | `EnableServer()` / `EnableClient()` |
| `AddFanoutChannel` | PUB / SUB | `EnablePublisher()` / `EnableSubscriber()` |

| capability | 의미 | 비고 |
|------------|------|------|
| `EnableServer()` | 이 channel 의 request/send 를 local handler 가 받는다 | `Bind(...)` 필수 |
| `EnableClient()` | 이 channel 로 request/send 를 내보낸다 | outbound 전용 앱 가능 |
| `EnablePublisher()` | 이 channel 로 이벤트를 publish 한다 | `Bind(...)` 필수 |
| `EnableSubscriber()` | 이 channel 의 이벤트를 구독한다 | |

한 channel 이 여러 capability 를 가질 수 있다(예: 서버이면서 다른 노드의 이벤트를
구독). server/publisher 는 외부가 접근할 endpoint 가 필요하므로 `Bind(...)`가
필수고, client/subscriber 는 필요 없다.

## 4. handler 노출은 명시적이다

framework 는 발견한 handler 를 모든 channel 에 자동으로 열지 않는다. 두 단계로
나뉜다.

1. **발견(scan):** `options.AddHandlersFromAssemblyOf<T>()`가 assembly 를 훑어
   handler 후보를 DI 에 등록한다. 이것만으로는 **아무 channel 에도 안 붙는다.**
2. **노출(map):** 실제 노출은 둘 중 하나가 정한다.
   - handler class 에 `[ZLinkHandlerGroup("api")]`를 붙이고, channel 등록에서
     `channel.AddHandlerGroup("api")` 로 그 group 을 그 channel 에 매핑.
   - channel builder 에서 `AddRequestHandler<...>()` / `AddSendHandler<...>()` /
     `AddPublishHandler<...>()` 로 개별 typed handler 를 등록.

이 분리 덕분에 같은 handler 묶음을 여러 channel(`tictactoe.api`, `chess.api`)에
재사용할 수 있고, 배포 topology 가 코드에서 빠진다.

**시작 단계 검증(fail-fast).** 다음은 lazy first-call 로 미루지 않고 host
startup 에서 즉시 예외로 막힌다.

- channel 이름 중복 등록
- 같은 channel 안에서 `kind + packet name` 중복(요청/단방향/이벤트 × packet 이름)
- client capability 에 `Discovery`도 수동 연결도 없는 경우
- 허용되지 않는 handler 반환형

## 5. 연결: Discovery vs 수동

- **기본은 channel 별 `Discovery` 자동 연결**이다. `options.UseDiscovery(...AddRegistryEndpoint...)` 를
  한 번 호출하면 이후 등록되는 모든 client/subscriber 가 이 Registry view 를
  기본 연결로 쓴다.
- **수동 연결**이 필요하면 capability builder 의
  `UseManualConnections(peers => peers.Connect("tcp://10.0.10.15:7301"))` 로
  지정한다. 수동 연결은 **capability 단위**(같은 channel 의 client 와 subscriber 는
  별도 연결 집합)이고, endpoint 만 받는다(remote routing id 는 받지 않는다).

| 전역 `UseDiscovery` | capability `UseManualConnections` | 결과 |
|:---:|:---:|---|
| O | X | Discovery 자동 연결 |
| O | O | 수동 연결 우선 |
| X | O | 수동 연결 |
| X | X | startup validation 오류 |

같은 앱에서 channel 마다 다른 방식을 골라도 된다(예: `profile`=Discovery,
`account`=수동). 단 **같은 channel 의 같은 client 안에서 두 방식을 섞을 수는
없다**(zlink core 제약). 수동 endpoint 는 startup builder 에서 등록하며,
framework public 계약은 host 시작 뒤 endpoint 를 바꾸는 별도 연결 관리 API 를
제공하지 않는다.

## 6. DI 와 lifecycle

- handler·client·filter 의 생성은 모두 `ASP.NET Core` 의 동일한 DI 컨테이너를
  따른다. handler 의존성은 **생성자 주입**으로 받는다(context 에서 service locator
  로 꺼내지 않는다).
- framework runtime 은 hosted service 로 호스트 시작/종료에 함께 묶인다. channel
  runtime 은 startup 에서 등록 capability 를 보고 생성되고, shutdown 에서
  정리된다.
- outbound 는 **목적별 client 를 주입**받아 호출한다.

| client | 언제 |
|--------|------|
| `IZLinkChannelClient` | 일반 channel request/send |
| `IZLinkFanoutClient` | pub/sub publish |
| `IZLinkSpotOutbound` | current Spot callback 안에서 다른 Spot, channel, publish 로 outbound |

channel 이름은 위치마다 뜻이 다르다는 점에 주의한다.

| 위치 | channel 이름의 뜻 |
|------|------------------|
| `client.RequestToChannel("profile", ...)` | request/send 를 보낼 **target** channel |
| `spot.Context.Outbound.SendToChannel("orders", ...)` | current Spot 이 attach 해서 사용할 **target** channel |
| `IZLinkSpotPublisherClient.PublishSpot("game.stage", topic, ...)` | local spot 없는 노드가 publish 할 **target SPOT channel** |

## 7. send 는 async submit

send/publish 의 public 호출은 기본 async submit 이다. blocking/nonblocking 을
public 동사나 builder 옵션으로 나누지 않으며, backpressure 는 framework 내부의
nonblocking send + pending queue + ready notification 으로 처리한다.

- `Async(...)` / `Async<T>(...)` 의 완료는 **transport 위임까지**만
  보장한다. remote handler 완료나 subscriber 수신을 보장하지 않는다.
- `Request(...).Timeout(...)` 은 **reply 대기 시간**만 정한다. submit 단계의
  backpressure 대기 한계는 channel/socket 의 `SendTimeout`(기본 200ms)을 따른다.
- `Send`/`Publish` 에는 `Timeout` 이 없다(응답을 기다리지 않으므로).

## 8. 더 깊이

- request/send/pub-sub 전체 사용법: [04-channel-messaging](./04-channel-messaging.ko.md)
- 전체 인터페이스/attribute/context: [spec/handler-interfaces](../spec/handler-interfaces.ko.md)
- 기능 선택 기준: [10-feature-map](./10-feature-map.ko.md)
