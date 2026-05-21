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
| request-response | `IZLinkRequestHandler<TReq,TRes>` | `[ZLinkRequest]` | `client.Request(...).SubmitAsync<TRes>(ct)` |
| command(단방향 send) | `IZLinkSendHandler<TMsg>` | `[ZLinkSend]` | `client.Send(...).Submit(ct)` |
| publish-subscribe | `IZLinkPublishHandler<TEvt>` | `[ZLinkPublish]` | `publisher.Publish(...).Submit(ct)` |
| SPOT 내부/외부 | `IZLinkSpot*Handler<...>` | (Spot 등록) | `IZLinkSpotClient`, `IZLinkRoutedSpotClient` |
| STREAM session | `IZLinkSession` | (stream 등록) | `IZLinkSessionContext` / `IZLinkSessionProxy` |

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

- **기본은 channel 별 `Discovery` 자동 연결**이다. `options.UseDiscovery(...)` 를
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
없다**(zlink core 제약). 런타임에 연결을 추가/제거하려면
`IZLinkChannelConnectionManager` 를 쓴다([04-channel-messaging](./04-channel-messaging.ko.md) §6).

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
| `IZLinkClient` | 일반 channel request/send |
| `IZLinkFanoutPublisher` | pub/sub publish |
| `IZLinkSpotClient` | current Spot callback 안에서의 outbound |
| `IZLinkRoutedSpotClient` | current Spot 없이 target Spot 으로 호출(HTTP/세션 gateway 등) |

channel 이름은 위치마다 뜻이 다르다는 점에 주의한다.

| 위치 | channel 이름의 뜻 |
|------|------------------|
| `client.Request("profile", ...)` | request/send 를 보낼 **target** channel |
| `routedSpots.ViaEgressChannel("gateway.client")` | 호출 프로세스가 쓸 **local egress** channel |
| `EnableSpotRouteEgress("play.route")` | target SpotNode 가 `AcceptSpotRoutesFromChannel(...)`로 연 **ingress** channel |

## 7. send 는 async submit

send/publish 의 public 호출은 기본 async submit 이다. blocking/nonblocking 을
public 동사나 builder 옵션으로 나누지 않으며, backpressure 는 framework 내부의
nonblocking send + pending queue + ready notification 으로 처리한다.

- `Submit(...)` / `SubmitAsync<T>(...)` 의 완료는 **transport 위임까지**만
  보장한다. remote handler 완료나 subscriber 수신을 보장하지 않는다.
- `Request(...).Timeout(...)` 은 **reply 대기 시간**만 정한다. submit 단계의
  backpressure 대기 한계는 channel/socket 의 `SendTimeout`(기본 200ms)을 따른다.
- `Send`/`Publish` 에는 `Timeout` 이 없다(응답을 기다리지 않으므로).

## 8. 더 깊이

- request/send/pub-sub 전체 사용법: [04-channel-messaging](./04-channel-messaging.ko.md)
- 전체 인터페이스/attribute/context: [spec/handler-interfaces](../spec/handler-interfaces.ko.md)
- 기능 선택 기준: [10-feature-map](./10-feature-map.ko.md)
