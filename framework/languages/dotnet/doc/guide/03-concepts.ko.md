<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Getting Started](./02-getting-started.ko.md) | [다음: 기능 맵 — 무엇을, 얼마나 쉽게, 언제](./04-feature-map.ko.md)
<!-- framework-adapter-nav:end -->

# 핵심 개념 — .NET 표면 멘탈 모델

> 개념의 정식 의미는 공통 스펙([interaction-model](../../../../doc/spec/interaction-model.ko.md),
> [message-model](../../../../doc/spec/message-model.ko.md),
> [channel-topology](../../../../doc/spec/channel-topology.ko.md))이 소유한다.
> 인터페이스의 정식 정의는 [spec/handler-interfaces](../spec/handler-interfaces.ko.md)다.
> 이 문서는 그 의미가 `.NET`에서 어떤 모양으로 보이는지만 정리한다.

## 1. channel name이 중심이다

호출 단위는 주소가 아니라 논리 `channel name`이다. 같은 channel이 여러 노드에
떠 있어도 응용은 channel 이름만 쓰고, channel별 `Discovery`가 위치를 해결한다.
배포 환경 값(주소·topology)은 handler가 아니라 **channel 등록**이 소유한다.

## 2. 상호작용 모델 ↔ .NET 표면

| 공통 모델 | .NET handler 인터페이스 | attribute | outbound |
|-----------|--------------------------|-----------|----------|
| request-response | `IZLinkRequestHandler<TReq,TRes>` | `[ZLinkRequest]` | `client.Request(...).SubmitAsync<TRes>(ct)` |
| command(단방향) | `IZLinkSendHandler<TMsg>` | `[ZLinkSend]` | `client.Send(...).Submit(ct)` |
| publish-subscribe | `IZLinkPublishHandler<TEvt>` | `[ZLinkPublish]` | `publisher.Publish(...).Submit(ct)` |
| SPOT 내부/외부 | `IZLinkSpot*Handler<...>` | (spot 등록) | `IZLinkSpotClient` |

handler는 attribute scan으로 발견되고, 전역 노출이 아니라 `EnableServer(...)`
/ `EnableSubscriber(...)` 같은 inbound capability 등록 시점에 channel로 매핑된다.

## 3. 등록 모델 — capability

channel은 `AddZLinkFramework(options => ...)` 안에서 등록하고, 그 channel이
가질 역할을 capability로 선언한다.

| capability | 의미 |
|------------|------|
| `EnableServer()` | 이 channel로 들어오는 request/send를 local handler가 받는다 |
| `EnableClient()` | 이 channel로 request/send를 내보낸다 (outbound 전용 앱 가능) |
| `EnableSubscriber()` | 이 channel의 이벤트를 구독한다 |

## 4. DI / lifecycle

- handler·client·filter 생성은 `ASP.NET Core`의 동일한 DI 컨테이너를 따른다.
- framework runtime은 hosted service로 호스트 시작/종료에 함께 묶인다.
- outbound는 `IZLinkClient` / `IZLinkSpotClient`를 주입받아 호출한다. 두
  인터페이스는 서로 다른 C API를 감싸는 별개 표면이다.

## 5. 연결: Discovery vs 수동

- 기본은 channel별 `Discovery` 기반 자동 연결이다.
- 수동 연결이 필요하면 `EnableClient(client => client.UseManualConnections(...))`
  형태로 `channel + capability` 단위로 지정한다. 같은 capability 안에서
  자동·수동을 섞지 않는다.

## 6. send는 async submit

send/publish의 public 호출은 기본 async submit으로 설명한다. blocking/nonblocking을
public 동사나 builder 옵션으로 나누지 않으며, backpressure는 framework 내부의
nonblocking send + pending queue + ready notification으로 처리한다(`SendTimeout`
기반 대기). 네이밍은 공통 [naming policy](../../../../doc/spec/README.ko.md)를 따른다.

## 7. 더 깊이

- 전체 인터페이스/attribute/context: [spec/handler-interfaces](../spec/handler-interfaces.ko.md)
- 프로그래밍 모델·dispatch 흐름: [spec/aspnet-core-channel-messaging](../spec/aspnet-core-channel-messaging.ko.md)
- 기능 선택 기준: [04-feature-map](./04-feature-map.ko.md)
