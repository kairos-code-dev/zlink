[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework .NET SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`에서 `SPOT`을 실제 코드 흐름으로 한 번에 보기
> 위한 샘플 문서다.

## 1. 이 문서의 목적

`SPOT`은 room, stage, zone 같은 논리 인스턴스 모델과 pub/sub, targeted
request/reply, timer가 함께 나오기 때문에 설명만 보면 감이 잘 안 올 수 있다.
이 문서는 아래를 한 번에 모아 보여 준다.

1. `SpotNode` 등록
2. spot 객체 생성
3. request 처리
4. subscribe 처리
5. timer 처리
6. `SendTo` / `RequestTo`
7. topic publish

## 2. 한 번에 보는 전체 예시

아래 코드는 `game.stage`라는 `SpotNode`를 등록하고, `StageSpot`을 실제 spot
객체로 만들어 request, subscribe, timer를 함께 처리하는 예시다.

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

        spot.AddSpotFactory<StageSpot>();

        spot.MapRequest<GetStageStateRequest, GetStageStateReply>(
            "stage.state.get",
            static (stage, request, cancellationToken) =>
                stage.GetStageStateAsync(request, cancellationToken));

        spot.MapSubscription<StageStateUpdatedEvent>(
            "stage.state.updated",
            static (stage, message, cancellationToken) =>
                stage.OnStageStateUpdatedAsync(message, cancellationToken));

        spot.AddPeriodicTimer(
            name: "heartbeat",
            period: TimeSpan.FromSeconds(1),
            static (stage, fireCount, cancellationToken) =>
                stage.OnHeartbeatAsync(fireCount, cancellationToken));
    });
});

var app = builder.Build();
app.Run();

public abstract class ZLinkSpot
{
    protected ZLinkSpot(RoutingId spotRid, RoutingId nodeRid, IZLinkSpotClient spotClient)
    {
        SpotRid = spotRid;
        NodeRid = nodeRid;
        SpotClient = spotClient;
    }

    public RoutingId SpotRid { get; }
    public RoutingId NodeRid { get; }
    protected IZLinkSpotClient SpotClient { get; }
}

public sealed class StageSpot : ZLinkSpot
{
    private readonly IZLinkSpotDirectory<string> _spotDirectory;

    public StageSpot(
        RoutingId spotRid,
        RoutingId nodeRid,
        IZLinkSpotClient spotClient,
        IZLinkSpotDirectory<string> spotDirectory)
        : base(spotRid, nodeRid, spotClient)
    {
        _spotDirectory = spotDirectory;
    }

    public ValueTask<GetStageStateReply> GetStageStateAsync(
        GetStageStateRequest request,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new GetStageStateReply
        {
            StageId = request.StageId,
            SpotRid = SpotRid,
            UserCount = 10
        });
    }

    public async ValueTask OnStageStateUpdatedAsync(
        StageStateUpdatedEvent message,
        CancellationToken cancellationToken)
    {
        await SpotClient.SendAsync(
            "api",
            new ReportStageStateCommand
            {
                StageId = message.StageId,
                UserCount = message.UserCount
            },
            cancellationToken);
    }

    public ValueTask OnHeartbeatAsync(
        ulong fireCount,
        CancellationToken cancellationToken)
    {
        return SpotClient.PublishAsync(
            "game.stage",
            "stage.state.updated",
            new StageStateUpdatedEvent
            {
                StageId = SpotRid.ToString(),
                UserCount = 10
            },
            cancellationToken);
    }

    public ValueTask<RoutingId> GetCurrentSpotRidAsync()
    {
        return ValueTask.FromResult(SpotRid);
    }

    public async ValueTask<GetStageStateReply?> QueryByStageIdAsync(
        string stageId,
        CancellationToken cancellationToken)
    {
        ZLinkSpotAddress? address = await _spotDirectory.ResolveAsync(
            stageId,
            cancellationToken);

        if (address is null)
            return null;

        return await SpotClient.RequestToAsync(
            address.Value.TargetRid,
            address.Value.SpotRid,
            new GetStageStateRequest { StageId = stageId },
            TimeSpan.FromMilliseconds(200),
            cancellationToken);
    }
}

public sealed class GetStageStateRequest : IZLinkRequest<GetStageStateReply>
{
    public string StageId { get; set; } = "";
}

public sealed class GetStageStateReply
{
    public string StageId { get; set; } = "";
    public RoutingId SpotRid { get; set; } = default!;
    public int UserCount { get; set; }
}

public sealed class StageStateUpdatedEvent
{
    public string StageId { get; set; } = "";
    public int UserCount { get; set; }
}

public sealed class ReportStageStateCommand
{
    public string StageId { get; set; } = "";
    public int UserCount { get; set; }
}
```

## 3. 이 샘플을 어떻게 읽으면 되는가

이 샘플에서 중요한 부분은 아래 여덟 가지다.

- `game.stage`는 `SpotNode` 이름이다.
- `StageSpot`은 단순 handler class가 아니라 실제 spot 객체다.
- `StageSpot`은 `ZLinkSpot`을 상속받고 자기 `SpotRid`, `NodeRid`를 상태로 가진다.
- request 처리는 `MapRequest(...)`로 `StageSpot` 인스턴스 메서드에 연결한다.
- subscribe 처리는 `MapSubscription(...)`로 `StageSpot` 인스턴스 메서드에 연결한다.
- timer 처리는 `AddPeriodicTimer(...)`로 `StageSpot` 인스턴스 메서드에 연결한다.
- 실제 전송에는 `targetRid`와 `spotRid`를 함께 넘긴다.
- 외부 lookup은 `IZLinkSpotDirectory<TKey>`가 맡고, 자기 identity는 `StageSpot`이 직접 가진다.

즉 이 샘플의 핵심은 "`StageSpot`이 이미 생성된 spot 객체"라는 점이다.
그래서 현재 spot rid는 요청 scope에서 따로 꺼내는 값이 아니라, `StageSpot.SpotRid`
속성으로 바로 읽는다.

## 4. client 와 directory 시그니처를 따로 보면

```csharp
public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    bool Created);

public readonly record struct ZLinkSpotAddress(
    RoutingId TargetRid,
    RoutingId SpotRid);

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

public interface IZLinkSpotDirectory<TKey>
{
    ValueTask<ZLinkSpotAddress?> ResolveAsync(
        TKey key,
        CancellationToken cancellationToken = default);
}

public interface IZLinkRequest<TReply>
{
}

public interface IZLinkSpotClient
{
    ValueTask SendAsync<TMessage>(
        string serviceName,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        IZLinkRequest<TReply> request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        IZLinkRequest<TReply> request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToAsync<TMessage>(
        RoutingId targetRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        IZLinkRequest<TReply> request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        IZLinkRequest<TReply> request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToAsync<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        IZLinkRequest<TReply> request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        IZLinkRequest<TReply> request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask PublishAsync<TEvent>(
        string serviceName,
        string topic,
        TEvent message,
        CancellationToken cancellationToken = default);
}
```

## 5. subscribe 와 timer 처리를 어떻게 보는가

이 샘플에서는 subscribe 와 timer를 별도 request handler처럼 보지 않는다.
둘 다 이미 생성된 `StageSpot` 객체의 인스턴스 메서드로 연결한다.

```csharp
spot.MapSubscription<StageStateUpdatedEvent>(
    "stage.state.updated",
    static (stage, message, cancellationToken) =>
        stage.OnStageStateUpdatedAsync(message, cancellationToken));

spot.AddPeriodicTimer(
    name: "heartbeat",
    period: TimeSpan.FromSeconds(1),
    static (stage, fireCount, cancellationToken) =>
        stage.OnHeartbeatAsync(fireCount, cancellationToken));
```

즉 정리하면:

- request: `StageSpot.GetStageStateAsync(...)`
- subscribe: `StageSpot.OnStageStateUpdatedAsync(...)`
- timer: `StageSpot.OnHeartbeatAsync(...)`

모두 같은 `StageSpot` 객체 위에서 처리된다.

## 6. 어떤 상황에 어떤 API를 쓰는가

- 새 spot 인스턴스를 만들고 싶다
  - `IZLinkSpotManager.CreateAsync(...)`
- 특정 stage 하나에 보내고 싶다
  - `SendTo(targetRid, spotRid, ...)`
- 특정 stage 하나에 요청하고 싶다
  - `RequestTo(targetRid, spotRid, ...)`
- stageId 같은 논리 키로 주소를 찾고 싶다
  - `IZLinkSpotDirectory<TKey>.ResolveAsync(...)`
- 현재 spot 자신의 rid를 알고 싶다
  - `StageSpot.SpotRid`
- stage 안에서 fan-out 하고 싶다
  - `PublishAsync(serviceName, topic, ...)`
- stage 안에서 heartbeat 같은 주기 작업을 돌리고 싶다
  - `AddPeriodicTimer(...)` -> `StageSpot.OnHeartbeatAsync(...)`

## 7. 피드백 포인트

- `StageSpot : ZLinkSpot` 같은 상속 모델이 자연스러운가
- request, subscribe, timer를 모두 같은 spot 객체 메서드로 처리하는 방향이 맞는가
- `IZLinkSpotManager`가 spot 객체 factory와 어떻게 연결되는 것이 자연스러운가
- `IZLinkSpotDirectory<TKey>`가 framework 기본 범위인지 wrapper 범위인지
- `AddPeriodicTimer(...)` 같은 timer 매핑 표면을 더 줄일 수 있는가
