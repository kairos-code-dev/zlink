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

등록부터 handler, `SendToSpot`, topic publish까지 이어서 보는 샘플은
[spot-samples.ko.md](./spot-samples.ko.md)를 참고한다.

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

이 관점에서 중요한 점은 아래와 같다.

- `server -> spot` 요청이 가능해야 한다.
- `spot -> server` 요청도 가능해야 한다.
- `spot -> spot` 요청도 가능해야 한다.
- 다만 `router rid` direct 전송과 `spot rid` 전송은 주소 체계가 다르므로,
  `SendTo(...)`와 `SendToSpot(...)`를 구분하는 편이 더 명확하다.

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

- 논리 `service_name` = `game.stage`
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

필요하면 둘을 서로 다른 서비스에 붙이는 구성도 열어 둘 수 있다.

## 5. SPOT outbound 모델 초안

현재 방향에서는 아래 세 종류를 구분하는 편이 더 자연스럽다.

- `serviceName` 기준 일반 호출
- `router rid` 기준 direct peer 호출
- `spot rid` 기준 spot 호출

즉 `SendTo(...)`와 `SendToSpot(...)`는 분리하고, `spot` 쪽은 별도 오버로드를 둔다.

```csharp
public interface IZLinkSpotClient
{
    ValueTask SendAsync<TMessage>(
        string serviceName,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToAsync<TMessage>(
        RoutingId targetRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToSpotAsync<TMessage>(
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId spotRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId spotRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToSpotAsync<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);
}
```

예를 들면 아래처럼 쓸 수 있다.

```csharp
await client.SendToSpotAsync(
    stageRid,
    new RoomNoticeMessage(),
    cancellationToken);

var reply = await client.RequestToSpotAsync<GetStageStateReply>(
    stageRid,
    new GetStageStateRequest(),
    TimeSpan.FromMilliseconds(200),
    cancellationToken);
```

node `rid`까지 이미 알고 있으면 아래처럼 보낼 수도 있다.

```csharp
await client.SendToSpotAsync(
    targetRid,
    stageRid,
    new RoomNoticeMessage(),
    cancellationToken);
```

이 모델이면 아래를 함께 설명할 수 있다.

- `server -> server`는 `SendAsync(...)`, `RequestAsync(...)`, `SendTo(...)`, `RequestTo(...)`
- `server -> spot`는 `SendToSpot(...)` / `RequestToSpot(...)`
- `spot -> server`도 같은 `IZLinkSpotClient`를 써서 `SendAsync(...)` / `RequestAsync(...)`
- `spot -> spot`는 `SendToSpot(...)` / `RequestToSpot(...)`

하지만 이것을 `IZLinkClient` 위에 `IZLinkSpotClient`를 얹는 관계로 설명하면 안
된다. 두 인터페이스는 하부에서 서로 다른 C API를 감싼다. 다만 C API 단계부터
겹치는 기능이 있으므로, 각 인터페이스가 `serviceName` 기반 호출, `router rid`
기반 직접 호출, `spot rid` 기반 호출의 일부 또는 전부를 각각 가질 수 있다.

## 6. publish 모델 초안

### 4.1 topic publish

```csharp
public interface IZLinkSpotClient
{
    ValueTask SendToSpotAsync<T>(
        RoutingId spotRid,
        T message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId spotRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId spotRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask PublishAsync<T>(
        string serviceName,
        string topic,
        T message,
        CancellationToken cancellationToken = default);
}
```

현재 방향에서는 `SPOT` 쪽은 direct call과 publish를 함께 쓰는 경우가 많으므로,
`IZLinkSpotClient` 하나에 같이 두는 편이 더 실용적이다.
또한 `IZLinkSpotClient`는 `spot -> server`와 `spot -> router rid` direct 호출도
직접 제공할 수 있다. 이는 일반 client를 감싸서 주는 뜻이 아니라, `SPOT` 쪽 C API
자체가 그 기능을 가지기 때문이다.

여기서 `topic`은 direct target address가 아니라 fan-out 주제 이름이다.
즉 `spotRid`와 `topic`은 역할이 다르다.

- `spotRid`: 특정 room/stage/zone 인스턴스를 직접 가리키는 주소
- `topic`: 여러 subscriber가 함께 듣는 주제 이름

## 7. subscribe 모델 초안

실제 handler 인터페이스 초안은 [handler-interfaces.ko.md](./handler-interfaces.ko.md)를
기준으로 본다.

### 5.1 handler 기반 구독

```csharp
public sealed class StageHandlers
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

추가로 routed request/reply를 handler로 올릴 경우 아래 attribute 후보를 쓴다.

```csharp
public sealed class StageQueryHandlers
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
모델과 targeted call까지 함께 설명해야 한다.

## 9. discovery와 service name

`SPOT`도 `service_name` 단위로 설명되어야 한다.

예를 들면:

- `game.stage`
- `game.presence`
- `match.realtime`

애플리케이션은 어떤 `SpotNode`가 어느 endpoint에 붙는지보다,
어느 논리 service에 속하는 `SPOT` runtime인지를 먼저 보게 하는 편이 좋다.

## 10. 아직 확정하지 않는 것

- `SpotNode`를 하나만 둘지 service별로 여러 개 둘지
- `IZLinkSpotClient`에서 publish를 분리할지, 그대로 둘지
- `SendToSpot(spotRid, ...)`만으로 충분할지, `SendToSpot(targetRid, spotRid, ...)`
  를 항상 같이 둘지
- `spotRid` 타입을 `RoutingId` 그대로 쓸지, 별도 wrapper로 올릴지
- subscriber concurrency와 backpressure를 어떻게 설정으로 노출할지
