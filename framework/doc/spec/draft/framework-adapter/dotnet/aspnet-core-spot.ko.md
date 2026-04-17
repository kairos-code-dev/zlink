[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework ASP.NET Core SPOT Integration

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `ASP.NET Core`에서 `SPOT`을 framework lifecycle 안에
> 넣는 방향을 정리한다.

## 1. 목표

`SPOT`은 zlink 쪽에서 이미 독립된 개념과 runtime을 갖는다.
`ZLink Framework`의 목적은 그 개념을 없애는 것이 아니라, `ASP.NET Core`에서
아래처럼 자연스럽게 다루게 만드는 것이다.

- `SpotNode` lifecycle 관리
- `Spot` publish/subscribe facade 주입
- room, stage, zone 같은 논리 인스턴스 모델 설명
- `spot -> server`, `server -> spot`, `spot -> spot` direct call
- discovery 기반 peer 구성
- background subscriber handler

## 2. 기반이 되는 .NET binding

현재 하부 토대는 아래 binding 표면이다.

- `Discovery`
- `SpotNode`
- `Spot`
- `Spot.RequestServiceAsync(...)` / `Spot.RequestToSpotAsync(...)`

즉 이 문서의 핵심은 `SPOT` 기능을 새로 만드는 일이 아니라,
기존 binding 기능을 `ASP.NET Core` 안에 녹이는 방법이다.

등록부터 handler, `SendTo`, topic publish까지 이어서 보는 샘플은
[spot-samples.ko.md](./spot-samples.ko.md)를 참고한다.
`playhouse`의 `Stage` 같은 상위 모델을 `SPOT` 위에 다시 감쌀 때 필요한 추가 조건은
[stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md)를 참고한다.

## 3. SPOT을 무엇으로 보는가

현재 초안에서 `SPOT`은 단순 pub/sub helper가 아니다.
오히려 아래처럼 **주소 가능한 논리 인스턴스**로 보는 편이 맞다.

- 게임 room
- playhouse stage
- 채팅 room
- MMORPG zone
- 필요하면 Redis pub/sub 같은 fan-out 주제 공간

즉 `SPOT`은 "토픽 시스템"보다 먼저 "논리 대상 인스턴스"로 설명되어야 한다.
publish/subscribe는 그 안에서 함께 쓰일 수 있는 한 가지 사용 방식이다.

여기서 관계를 더 정확히 잡으면 아래와 같다.

- `Spot`은 특정 service에 속하지 않는다.
- `Spot`은 `SpotNode`에 종속된다.
- `SpotNode`가 여러 service의 `router`와 `pub/sub` surface에 연결될 수 있다.
- 따라서 `spotRid`는 service에서 생기는 값이 아니라, `SpotNode`가 개별 spot
  인스턴스를 만들 때 생기는 식별자다.

이 관점에서 중요한 점은 아래와 같다.

- `server -> spot` 요청이 가능해야 한다.
- `spot -> server` 요청도 가능해야 한다.
- `spot -> spot` 요청도 가능해야 한다.
- 다만 `router rid` direct 전송과 `spot` 전송은 주소 체계가 다르므로,
  `SendTo(...)` / `RequestTo(...)` 오버로드에서 `targetRid, spotRid`를 함께 받는
  편이 더 명확하다.

여기서 경계를 분명히 하면, `SPOT`이 제공하는 것은 주소 가능한 논리 인스턴스와
그 인스턴스에 대한 메시징, publish/subscribe, timer, lifecycle까지다.
반면 membership, actor/session, room broadcast 정책 같은 것은 기본 `zlink
framework` 범위를 넘어가는 상위 wrapper의 책임으로 두는 편이 맞다.

## 4. ASP.NET Core 등록 모델 초안

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
    });

    options.AddSpotNode("game.stage", spot =>
    {
        spot.Bind("tcp://0.0.0.0:9000");
        spot.AttachRouterService("play");
        spot.AttachPubSubService("play");
    });
});
```

이 등록은 아래를 의미한다.

- 논리 `SpotNode` 이름 = `game.stage`
- backing `SpotNode` 생성
- 어떤 서비스의 `router`와 `pub/sub`에 붙는지 지정
- discovery attach
- host shutdown 시 lifecycle 정리

즉 `SpotNode`는 독립 인스턴스이지만, 실제로는 어떤 서비스의 transport surface에
붙는지도 함께 알아야 한다. 현재 초안은 아래 두 attachment를 함께 두는 쪽을
기본으로 본다.

```csharp
spot.AttachRouterService("play");
spot.AttachPubSubService("play");
```

여기서 중요한 점은 사용자가 low-level router 또는 pub/sub 객체를 직접 만들지
않는다는 것이다. `AttachRouterService("play")`, `AttachPubSubService("play")`처럼
`serviceId`만 넘기면 framework가 내부에서 해당 service runtime의 surface를
생성하거나 재사용하고, 필요한 `Discovery` attach까지 자동으로 처리하는 방향을
기본으로 본다.

필요하면 둘을 서로 다른 서비스에 붙이는 구성도 열어 둘 수 있다.

### 4.1 spot 실행 문맥과 timer

이 부분은 framework가 새로 의미를 만드는 일보다, 하부 C API 계약을 그대로
framework 표면으로 끌어올리는 성격이 더 강하다.

현재 core spec 기준으로 아래는 이미 정해져 있다.

- 같은 `spot`의 dispatch callback delivery는 직렬화된다.
- subscribe, routed, timer readable 알림은 같은 dispatch event 축으로 올라온다.
- `zlink_spot_timer_new(spot)`로 만든 timer는 해당 `spot`에 종속된다.
- dispatch callback 안에서는 `zlink_timer_recv()`로 pending timer fire를 drain할 수
  있다.

즉 framework 문서에서 "같은 spot 문맥"이라고 설명하는 부분은 새 semantics를
정하는 일이 아니라, 기존 core 계약을 `.NET` 사용자 눈높이로 풀어 적는 일에
가깝다.

### 4.2 Spot 생성과 lifecycle 초안

현재 방향에서는 handler class가 spot을 만드는 것이 아니다.
`Spot` 인스턴스는 `SpotNode`가 생성하고 소유한다. handler는 이미 존재하는 spot으로
들어오는 request, publish, subscribe를 처리할 뿐이다.

이 기준에서 manager는 `serviceName`이 아니라 현재 앱의 `SpotNode`를 대상으로
동작하는 편이 더 자연스럽다.

```csharp
public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    bool Created);

public interface IZLinkSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}
```

이 표면은 아래 상황을 함께 설명한다.

- runtime이 UUID 같은 새 `spotRid`를 발급하는 생성
- 호출자가 특정 `spotRid`를 지정해서 생성하는 경우
- 이미 존재하는 `spotRid`면 그대로 얻는 `get-or-create` 성격의 동작

중요한 점은 반환값이 장기적으로 들고 다닐 spot instance handle이 아니라는 점이다.
생성 결과는 `spotRid`와 `Created` 정도면 충분하고, 이후 메시징은
`IZLinkSpotClient`가 `targetRid + spotRid`를 기준으로 처리하는 편이 더 자연스럽다.

즉 사용자는 생성 직후 식별자만 얻고:

```csharp
var stage = await spotManager.CreateAsync("stage-1001", cancellationToken);

await spotClient.SendToAsync(
    targetRid,
    stage.SpotRid,
    new StageNoticeMessage(),
    cancellationToken);
```

처럼 사용하면 된다. 생성된 `Spot` 인스턴스를 응용이 직접 오래 관리하는 모델은
현재 방향으로 보지 않는다.

초기 metadata를 같이 넘기는 create 표면은 `Stage wrapper` 같은 상위 계층에서는
유용할 수 있다. 다만 현재 하부 C API 공개 계약에서 바로 읽히는 내용은 아니므로,
framework 기본 계약처럼 적기보다 wrapper 확장 후보로 따로 다루는 편이 맞다.

## 5. SPOT outbound 모델 초안

현재 방향에서는 아래 세 종류를 구분하는 편이 더 자연스럽다.

- `serviceName` 기준 일반 호출
- `router rid` 기준 direct peer 호출
- `targetRid + spotRid` 기준 spot 호출

즉 별도 spot 전용 함수 이름보다, `SendTo(...)` / `RequestTo(...)`의
오버로드로 두는 편이 더 맞다.
여기서 중요한 점은 framework가 `spotRid`만 받고 내부에서 `discovery`로
`targetRid`를 찾아 주는 축약 API를 기본으로 두지 않는다는 것이다. `discovery`
사용은 선택 사항이므로, public API는 필요한 주소를 호출자가 직접 넘기는 편이
더 정직하다.

`IZLinkSpotClient` 인터페이스 전체 정의는
[handler-interfaces.ko.md](./handler-interfaces.ko.md)의 section 5.2를
참고한다. 호출 축은 `IZLinkClient`와 동일하게 `serviceName`, `targetRid`,
`targetRid + spotRid` 세 가지이며, 추가로 `PublishAsync`와 spot 문맥 timer를
제공한다.

예를 들면 아래처럼 쓸 수 있다.

```csharp
await client.SendToAsync(
    targetRid,
    stageRid,
    new RoomNoticeMessage(),
    cancellationToken);

var reply = await client.RequestToAsync(
    targetRid,
    stageRid,
    new GetStageStateRequest(),
    TimeSpan.FromMilliseconds(200),
    cancellationToken);
```

`Stage wrapper` 같은 상위 모델을 생각하면 timer도 같이 필요하다.
이 문서의 초안 기준으로 `IZLinkSpotClient` timer callback은 가능하면 같은 spot 실행
문맥으로 들어오는 쪽이 더 자연스럽다. 그래야 stage state를 별도 lock 없이 다루는
상위 모델을 설명하기 쉽다.

이 모델이면 아래를 함께 설명할 수 있다.

- `server -> server`는 `SendAsync(...)`, `RequestAsync(...)`, `SendTo(...)`, `RequestTo(...)`
- `server -> spot`는 `SendTo(targetRid, spotRid, ...)` / `RequestTo(targetRid, spotRid, ...)`
- `spot -> server`도 같은 `IZLinkSpotClient`를 써서 `SendAsync(...)` / `RequestAsync(...)`
- `spot -> spot`는 `SendTo(targetRid, spotRid, ...)` / `RequestTo(targetRid, spotRid, ...)`

하지만 이것을 `IZLinkClient` 위에 `IZLinkSpotClient`를 얹는 관계로 설명하면 안
된다. 두 인터페이스는 하부에서 서로 다른 C API를 감싼다. 다만 C API 단계부터
겹치는 기능이 있으므로, 각 인터페이스가 `serviceName` 기반 호출, `router rid`
기반 직접 호출, `targetRid + spotRid` 기반 호출의 일부 또는 전부를 각각 가질 수
있다.

## 6. publish 모델 초안

### 6.1 topic publish

`IZLinkSpotClient`는 direct call과 publish를 함께 가진다
([handler-interfaces.ko.md](./handler-interfaces.ko.md) section 5.2 참고).
이는 `SPOT` 쪽이 두 기능을 함께 쓰는 경우가 많기 때문이다.

여기서 `topic`과 `spotRid`는 역할이 다르다.

- `spotRid`: 특정 room/stage/zone 인스턴스를 가리키는 논리 주소
- `topic`: 여러 subscriber가 함께 듣는 fan-out 주제 이름

추가로 `SPOT` 쪽에는 외부 lookup과 현재 spot 자기 조회를 분리해서 둘 필요가 있다.
예를 들면 아래 두 축이다.

- `IZLinkSpotDirectory<TKey>`: 논리 키를 `(targetRid, spotRid)` 주소로 해석
- `ZLinkSpotContext.Self`: 현재 spot의 `spotRid`, `nodeRid` 조회

즉 어떤 spot이 생성될 때 받은 `spotRid`는 이후 그 spot에서 실행되는 handler 안에서
`context.Self.SpotRid`로 다시 조회할 수 있어야 한다. 외부 address lookup과 내부
self identity 조회는 서로 다른 문제다.

## 7. subscribe 모델 초안

실제 handler 인터페이스 초안은 [handler-interfaces.ko.md](./handler-interfaces.ko.md)를
기준으로 본다.

### 5.1 handler 기반 구독

```csharp
public sealed class StageSpot
{
    [ZLinkSpotSubscription("game.stage", "zone.*.state")]
    public ValueTask OnZoneStateAsync(
        ZoneStateEvent message,
        ZLinkSpotSubscriptionContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}
```

여기서 기대하는 점은 아래와 같다.

- 구독 등록은 framework startup에서 이뤄진다.
- 수신 메시지는 typed object로 역직렬화된다.
- `ZLinkSpotSubscriptionContext`에서 topic, source rid, dispatch metadata를 본다.
- `ZLinkSpotSubscriptionContext.Self`에서 현재 spot 자신의 identity를 본다.

추가로 routed request/reply를 handler로 올릴 경우 아래 attribute 후보를 쓴다.

```csharp
public sealed class StageSpot
{
    [ZLinkSpotRequest("stage.query")]
    public ValueTask<StageQueryReply> HandleAsync(
        StageQueryRequest request,
        ZLinkSpotRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new StageQueryReply());
    }
}
```

### 5.2 background service 모델

경우에 따라 attribute보다 `BackgroundService` 스타일이 더 자연스러울 수 있다.

```csharp
public sealed class StageSyncWorker : BackgroundService
{
    protected override Task ExecuteAsync(CancellationToken stoppingToken)
    {
        return Task.CompletedTask;
    }
}
```

현재 초안은 두 모델 모두 가능하다고 보되, 1차는 handler registration 쪽이 더
일관적일 수 있다고 본다.

## 8. SPOT과 direct call의 관계

`ZLink Framework`는 direct service call만 제공하는 계층처럼 보이면 안 된다.
`SPOT`도 framework 안에서 동등한 축으로 다뤄야 한다.

즉 아래 둘이 함께 있어야 한다.

- `service_name` 기반 direct call
- `SPOT` 기반 pub/sub 및 state sync

또한 현재 하부 binding은 `SPOT` routed request/reply도 제공하므로, framework는
앞으로 아래 두 종류를 구분해 설명할 필요가 있다.

- broad fan-out 또는 state sync 성격의 publish/subscribe
- 특정 stage 또는 spot 대상으로 보내는 targeted request/reply

이 점은 `playhouse` 시나리오에서 특히 중요하다.

- play -> api 는 direct call
- stage/state sync 는 `SPOT`

즉 `SPOT`은 pub/sub만으로 설명하면 부족하고, room/stage/zone 같은 논리 인스턴스
모델과 targeted call, 그리고 `SpotNode`가 spot 인스턴스를 생성하고 소유하는
lifecycle까지 함께 설명해야 한다.

## 9. discovery와 service name

`SPOT`도 `service_name` 단위로 설명되어야 한다.

예를 들면:

- `game.stage`
- `game.presence`
- `match.realtime`

애플리케이션은 어떤 `SpotNode`가 어느 endpoint에 붙는지보다,
어느 논리 service에 속하는 `SPOT` runtime인지를 먼저 보게 하는 편이 좋다.

## 10. 아직 확정하지 않는 것

- `SpotNode`를 하나만 둘지 여러 개 둘지
- `IZLinkSpotClient`에서 publish를 분리할지, 그대로 둘지
- `SendTo(targetRid, spotRid, ...)`와 `RequestTo(targetRid, spotRid, ...)`만으로
  충분한지
- `IZLinkSpotManager`가 `Created` 외에 어떤 생성 메타데이터를 더 돌려줄지
- `spotRid` 타입을 `RoutingId` 그대로 쓸지, 별도 wrapper로 올릴지
- subscriber concurrency와 backpressure를 어떻게 설정으로 노출할지

추가로 `Stage wrapper`를 염두에 둘 때는 아래 축도 따로 정리해야 한다.

- spot 실행 문맥을 어떻게 보장할지
- timer callback을 어떤 문맥에서 실행할지
- create 시 초기 metadata를 어떻게 전달할지
- membership 와 directory 같은 상위 모델 축을 어디서 맡을지
