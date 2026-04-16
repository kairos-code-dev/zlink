[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework .NET SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`에서 `SPOT`을 실제 코드 흐름으로 한 번에 보기
> 위한 샘플 문서다.

## 1. 이 문서의 목적

`SPOT`은 room, stage, zone 같은 논리 인스턴스 모델과 pub/sub, targeted
request/reply가 함께 나오기 때문에, 설명만 보면 감이 잘 안 올 수 있다.
이 문서는 아래를 한 번에 모아 보여 준다.

1. `SpotNode` 등록
2. spot subscribe handler
3. `SendToSpot` / `RequestToSpot`
4. topic publish
5. `server -> spot`, `spot -> server`, `spot -> spot`

## 2. 한 번에 보는 전체 예시

아래 코드는 `game.stage`라는 spot service를 등록하고, 특정 stage를 직접
타겟팅하면서 state sync topic도 함께 쓰는 예시다.

```csharp
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.ServiceId = "play";
    options.NodeName = "play-1";
    options.Codecs.AddProtobuf();

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

builder.Services.AddZLinkHandlersFromAssemblyContaining<Program>();

var app = builder.Build();
app.Run();

public sealed class StageHandlers
{
    private readonly IZLinkSpotClient _spotClient;

    public StageHandlers(IZLinkSpotClient spotClient)
    {
        _spotClient = spotClient;
    }

    [ZLinkSpotRequest("stage.state.get")]
    public ValueTask<GetStageStateReply> GetStageStateAsync(
        GetStageStateRequest request,
        ZLinkSpotRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new GetStageStateReply
        {
            StageId = request.StageId,
            UserCount = 10
        });
    }

    [ZLinkSpotSubscription("game.stage", "stage.state.updated")]
    public async ValueTask OnStageStateUpdatedAsync(
        StageStateUpdatedEvent message,
        ZLinkSpotSubscriptionContext context,
        CancellationToken cancellationToken)
    {
        await _spotClient.SendAsync(
            "api",
            new ReportStageStateCommand
            {
                StageId = message.StageId,
                UserCount = message.UserCount
            },
            cancellationToken);
    }

    public async ValueTask BroadcastStageStateAsync(
        CancellationToken cancellationToken)
    {
        await _spotClient.PublishAsync(
            "game.stage",
            "stage.state.updated",
            new StageStateUpdatedEvent
            {
                StageId = "stage-1001",
                UserCount = 10
            },
            cancellationToken);
    }

    public async ValueTask NotifyTargetStageAsync(
        RoutingId stageRid,
        CancellationToken cancellationToken)
    {
        await _spotClient.SendToSpotAsync(
            stageRid,
            new StageNoticeMessage { Text = "match-start" },
            cancellationToken);
    }

    public async ValueTask<GetStageStateReply> QueryTargetStageAsync(
        RoutingId stageRid,
        CancellationToken cancellationToken)
    {
        return await _spotClient.RequestToSpotAsync<GetStageStateReply>(
            stageRid,
            new GetStageStateRequest { StageId = "stage-1001" },
            TimeSpan.FromMilliseconds(200),
            cancellationToken);
    }

    public async ValueTask NotifyTargetStageWithNodeAsync(
        RoutingId targetRid,
        RoutingId stageRid,
        CancellationToken cancellationToken)
    {
        await _spotClient.SendToSpotAsync(
            targetRid,
            stageRid,
            new StageNoticeMessage { Text = "sync-now" },
            cancellationToken);
    }
}

public sealed class GetStageStateRequest
{
    public string StageId { get; set; } = "";
}

public sealed class GetStageStateReply
{
    public string StageId { get; set; } = "";
    public int UserCount { get; set; }
}

public sealed class StageStateUpdatedEvent
{
    public string StageId { get; set; } = "";
    public int UserCount { get; set; }
}

public sealed class StageNoticeMessage
{
    public string Text { get; set; } = "";
}

public sealed class ReportStageStateCommand
{
    public string StageId { get; set; } = "";
    public int UserCount { get; set; }
}
```

## 3. 이 샘플을 어떻게 읽으면 되는가

이 샘플에서 중요한 부분은 아래 일곱 가지다.

- `game.stage`는 spot service다.
- `SpotNode`는 `play` 서비스의 `router`와 `pub/sub`에 붙는다.
- 개별 room/stage/zone은 `spotRid`로 주소를 가진다.
- `SendToSpot(spotRid, ...)`는 특정 논리 인스턴스로 바로 보낸다.
- `SendToSpot(targetRid, spotRid, ...)`는 node와 spot을 함께 지정하는 버전이다.
- `PublishAsync(serviceName, topic, ...)`는 fan-out 주제 전파다.
- subscribe handler는 topic을 받고, request handler는 특정 spot request를 처리한다.

즉 `spotRid` direct call과 `topic` publish는 역할이 다르다.

그리고 `SpotNode`는 논리 service 이름만 아는 것으로 끝나지 않는다.
실제로는 어느 서비스의 `router`와 `pub/sub` surface에 붙는지도 같이 정해야 한다.

- `AttachRouterService("play")`
- `AttachPubSubService("play")`

## 4. client 함수 시그니처를 따로 보면

`SPOT`에서 중요한 outbound 함수는 아래 정도다.

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

    ValueTask PublishAsync<TEvent>(
        string serviceName,
        string topic,
        TEvent message,
        CancellationToken cancellationToken = default);
}
```

## 5. 함수 호출 예시만 따로 보면

```csharp
await spotClient.SendAsync(
    "api",
    new ReportStageStateCommand
    {
        StageId = "stage-1001",
        UserCount = 10
    },
    cancellationToken);
```

```csharp
var serverReply = await spotClient.RequestAsync<GetMatchConfigReply>(
    "api.match",
    new GetMatchConfigRequest { StageId = "stage-1001" },
    TimeSpan.FromMilliseconds(150),
    cancellationToken);
```

```csharp
await spotClient.SendToAsync(
    targetRid,
    new StageNoticeMessage { Text = "direct-router-send" },
    cancellationToken);
```

```csharp
await spotClient.SendToSpotAsync(
    stageRid,
    new StageNoticeMessage { Text = "match-start" },
    cancellationToken);
```

```csharp
var reply = await spotClient.RequestToSpotAsync<GetStageStateReply>(
    stageRid,
    new GetStageStateRequest { StageId = "stage-1001" },
    TimeSpan.FromMilliseconds(200),
    cancellationToken);
```

```csharp
await spotClient.SendToSpotAsync(
    targetRid,
    stageRid,
    new StageNoticeMessage { Text = "sync-now" },
    cancellationToken);
```

```csharp
await spotClient.PublishAsync(
    "game.stage",
    "stage.state.updated",
    new StageStateUpdatedEvent
    {
        StageId = "stage-1001",
        UserCount = 10
    },
    cancellationToken);
```

## 6. handler 시그니처만 따로 보면

```csharp
public sealed class StageHandlers
{
    [ZLinkSpotRequest("stage.state.get")]
    public ValueTask<GetStageStateReply> GetStageStateAsync(
        GetStageStateRequest request,
        ZLinkSpotRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new GetStageStateReply());
    }

    [ZLinkSpotSubscription("game.stage", "stage.state.updated")]
    public ValueTask OnStageStateUpdatedAsync(
        StageStateUpdatedEvent message,
        ZLinkSpotSubscriptionContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}
```

핵심은 이렇다.

- request handler는 특정 spot request를 처리한다.
- subscription handler는 topic fan-out을 처리한다.
- 둘은 모두 `SPOT`이지만 의미가 다르다.

## 7. 어떤 상황에 어떤 API를 쓰는가

- 특정 room/stage/zone 하나에 직접 보내고 싶다
  - `SendToSpot(spotRid, ...)`
- 특정 node 위의 특정 spot으로 정확히 보내고 싶다
  - `SendToSpot(targetRid, spotRid, ...)`
- 여러 subscriber에게 fan-out 하고 싶다
  - `PublishAsync(serviceName, topic, ...)`
- spot에서 일반 서버로 요청하고 싶다
  - 같은 `IZLinkClient`의 `SendAsync` / `RequestAsync`

## 8. 피드백 포인트

- `IZLinkSpotClient`가 `serviceName` 기준 호출과 `router rid` direct 호출도 같이 가져야 하는가
- `IZLinkSpotClient` 안에 `publish`까지 같이 두는 것이 맞는가
- `SendToSpot(spotRid, ...)`와 `SendToSpot(targetRid, spotRid, ...)`를 둘 다 두는 것이 맞는가
- `spotRid`와 `topic`의 구분이 문서에서 충분히 선명한가
- room/stage/zone 모델로 `SPOT`을 설명하는 방식이 자연스러운가
- `spot -> server`, `server -> spot`, `spot -> spot`을 같은 응용 안에서 설명하기 쉬운가
