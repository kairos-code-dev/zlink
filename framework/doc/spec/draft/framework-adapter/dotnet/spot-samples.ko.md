[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework .NET SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`에서 `SPOT`을 실제 코드 흐름으로 한 번에 보기
> 위한 샘플 문서다.

## 1. 이 문서의 목적

`SPOT`은 room, stage, zone 같은 논리 인스턴스 모델과 targeted packet, subscribe,
timer가 함께 나오기 때문에 설명만 보면 감이 잘 안 올 수 있다.
이 문서는 아래를 한 번에 모아 보여 준다.

1. `SpotNode` 등록
2. spot 객체 생성
3. packet handler 등록
4. subscribe handler 등록
5. timer 등록
6. `SendTo` / `RequestTo`
7. topic publish

## 2. 인터페이스 초안

먼저 이 문서에서 전제로 두는 인터페이스 초안을 따로 본다.
아래는 샘플 구현이 기대하는 최소 표면이다.

```csharp
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

    protected void AddPacket<THandler>(IServiceProvider services)
        where THandler : class
    {
    }

    protected void AddSubscribe<THandler>(
        string topic,
        IServiceProvider services)
        where THandler : class
    {
    }

    protected ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        IServiceProvider services,
        CancellationToken cancellationToken)
        where THandler : class
    {
        return SpotClient.SchedulePeriodicAsync(period, _ => ValueTask.CompletedTask, cancellationToken);
    }

    public virtual ValueTask OnInitializeAsync(
        IServiceProvider services,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkSpotPacketHandler<TSpot, in TMessage>
    where TSpot : ZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRequestHandler<TSpot, in TRequest, TReply>
    where TSpot : ZLinkSpot
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotSubscriptionHandler<TSpot, in TEvent>
    where TSpot : ZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TEvent message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : ZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        CancellationToken cancellationToken);
}

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

## 3. 샘플 코드

### 3.1 등록 코드

```csharp
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Stage.Protocol; // protoc generated

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
    });
});

var app = builder.Build();
app.Run();
```

### 3.2 spot 객체

```csharp
public sealed class StageSpot : ZLinkSpot
{
    private IZLinkTimer? _heartbeat;

    public StageSpot(
        RoutingId spotRid,
        RoutingId nodeRid,
        IZLinkSpotClient spotClient)
        : base(spotRid, nodeRid, spotClient)
    {
    }

    public int UserCount { get; private set; } = 10;

    public override async ValueTask OnInitializeAsync(
        IServiceProvider services,
        CancellationToken cancellationToken)
    {
        AddPacket<GetStageStateHandler>(services);
        AddPacket<ReportStageStateHandler>(services);

        AddSubscribe<StageStateUpdatedHandler>(
            "stage.state.updated",
            services);

        _heartbeat = await AddTimer<StageHeartbeatHandler>(
            "heartbeat",
            TimeSpan.FromSeconds(1),
            services,
            cancellationToken);
    }

    public void ApplyReportedState(ReportStageStateCommand command)
    {
        UserCount = command.UserCount;
    }
}
```

### 3.3 handler 클래스

```csharp
public sealed class GetStageStateHandler
    : IZLinkSpotRequestHandler<StageSpot, GetStageStateRequest, GetStageStateReply>
{
    private readonly IZLinkClient _client;

    public GetStageStateHandler(IZLinkClient client)
    {
        _client = client;
    }

    public async ValueTask<GetStageStateReply> HandleAsync(
        StageSpot spot,
        GetStageStateRequest request,
        CancellationToken cancellationToken)
    {
        await _client.SendAsync(
            "api",
            new ReportStageLookupCommand(),
            cancellationToken);

        return new GetStageStateReply
        {
            StageId = request.StageId,
            SpotRid = spot.SpotRid,
            UserCount = spot.UserCount
        };
    }
}

public sealed class ReportStageStateHandler
    : IZLinkSpotPacketHandler<StageSpot, ReportStageStateCommand>
{
    private readonly IZLinkSpotClient _spotClient;

    public ReportStageStateHandler(IZLinkSpotClient spotClient)
    {
        _spotClient = spotClient;
    }

    public async ValueTask HandleAsync(
        StageSpot spot,
        ReportStageStateCommand message,
        CancellationToken cancellationToken)
    {
        spot.ApplyReportedState(message);

        await _spotClient.SendAsync(
            "api",
            new ReportStageLookupCommand(),
            cancellationToken);
    }
}

public sealed class StageStateUpdatedHandler
    : IZLinkSpotSubscriptionHandler<StageSpot, StageStateUpdatedEvent>
{
    private readonly IZLinkSpotDirectory<string> _spotDirectory;
    private readonly IZLinkSpotClient _spotClient;

    public StageStateUpdatedHandler(
        IZLinkSpotDirectory<string> spotDirectory,
        IZLinkSpotClient spotClient)
    {
        _spotDirectory = spotDirectory;
        _spotClient = spotClient;
    }

    public async ValueTask HandleAsync(
        StageSpot spot,
        StageStateUpdatedEvent message,
        CancellationToken cancellationToken)
    {
        ZLinkSpotAddress? address = await _spotDirectory.ResolveAsync(
            message.StageId,
            cancellationToken);

        if (address is not null)
        {
            await _spotClient.RequestToAsync(
                address.Value.TargetRid,
                address.Value.SpotRid,
                new GetStageStateRequest { StageId = message.StageId },
                TimeSpan.FromMilliseconds(200),
                cancellationToken);
        }

        await _spotClient.SendToAsync(
            spot.NodeRid,
            spot.SpotRid,
            new StagePingCommand(),
            cancellationToken);
    }
}

public sealed class StageHeartbeatHandler : IZLinkSpotTimerHandler<StageSpot>
{
    private readonly IZLinkSpotClient _spotClient;

    public StageHeartbeatHandler(IZLinkSpotClient spotClient)
    {
        _spotClient = spotClient;
    }

    public ValueTask HandleAsync(
        StageSpot spot,
        CancellationToken cancellationToken)
    {
        return _spotClient.PublishAsync(
            "game.stage",
            "stage.state.updated",
            new StageStateUpdatedEvent
            {
                StageId = spot.SpotRid.ToString(),
                UserCount = spot.UserCount
            },
            cancellationToken);
    }
}
```

### 3.4 protobuf packet 타입

```csharp
// GetStageStateRequest, GetStageStateReply, ReportStageStateCommand,
// StageStateUpdatedEvent, ReportStageLookupCommand, StagePingCommand는
// .proto에서 생성된 protobuf message 타입이라고 가정한다.
// 실제 generated code는 샘플에서 생략한다.
```

## 4. 이 샘플을 어떻게 읽으면 되는가

이 샘플에서 중요한 부분은 아래 아홉 가지다.

- `game.stage`는 `SpotNode` 이름이다.
- `StageSpot`은 단순 handler class가 아니라 실제 spot 객체다.
- `StageSpot`은 `ZLinkSpot`을 상속받고 자기 `SpotRid`, `NodeRid`를 상태로 가진다.
- `StageSpot` 안에는 상태와 코어 로직만 두고, packet 처리는 별도 handler class로 뺄 수 있다.
- handler는 `IZLinkClient`, `IZLinkSpotClient`를 constructor injection으로 받을 수 있다.
- `AddPacket<THandler>(...)`는 request와 send packet을 함께 등록한다.
- `AddSubscribe<THandler>(...)`는 topic subscription consumer를 등록한다.
- `AddTimer<THandler>(...)`는 spot lifecycle 안에서 timer를 등록한다.
- 실제 전송에는 `targetRid`와 `spotRid`를 함께 넘긴다.
- 외부 lookup은 `IZLinkSpotDirectory<TKey>`가 맡고, 자기 identity는 `StageSpot`이 직접 가진다.

즉 이 샘플의 핵심은 "`StageSpot`이 이미 생성된 spot 객체"라는 점이다.
그래서 현재 spot rid는 요청 scope에서 따로 꺼내는 값이 아니라, `StageSpot.SpotRid`
속성으로 바로 읽는다.

## 5. packet 등록과 subscribe 등록은 어떻게 다른가

이 초안에서는 `StageSpot`이 자기 초기화 단계에서 직접 아래 세 종류를 등록한다.

- `AddPacket<THandler>(...)`
- `AddSubscribe<THandler>(topic, ...)`
- `AddTimer<THandler>(name, period, ...)`

packet은 request와 send를 함께 묶는 상위 개념이다.
즉 `GetStageStateHandler`처럼 reply를 돌려주는 handler도 packet이고,
`ReportStageStateHandler`처럼 응답 없이 상태만 반영하는 handler도 packet이다.

반면 subscribe는 packet이 아니다.
subscribe는 topic fan-out consumer 등록이고, packet은 targeted delivery다.

handler가 다른 서버나 다른 spot으로 outbound 호출을 해야 하면, `StageSpot`에서
client를 꺼내 쓰기보다 handler가 `IZLinkClient`나 `IZLinkSpotClient`를 직접
주입받는 쪽이 더 자연스럽다. 이러면 `StageSpot`은 상태와 코어 로직에 집중하고,
outbound 호출 책임은 handler가 맡을 수 있다.

이 샘플 기준으로 `StageSpot` 안에 남는 것은 아래 정도다.

- `SpotRid`, `NodeRid` 같은 자기 identity
- `UserCount` 같은 현재 상태
- `ApplyReportedState(...)` 같은 순수 도메인 메서드
- 초기 registration을 위한 `OnInitializeAsync(...)`

## 6. packet 매핑은 무엇을 기준으로 하는가

이 초안에서는 `AddPacket<THandler>(...)`가 문자열을 따로 받지 않는다.
packet 매핑 기준은 header의 `msgId`다.

이 문서의 전체 샘플 코드는 `options.Codecs.AddProtobuf()`를 쓰므로 기본 예시는
`protobuf` 기준이다. 즉 위 코드에서 packet 타입은 모두 `protoc`가 생성한
`IMessage<T>` 계열 타입이라고 보면 된다. 이 문서는 generated protobuf 타입에
framework용 marker interface를 직접 붙이는 방식을 전제로 하지 않는다.

즉 위 코드에서:

- `AddPacket<GetStageStateHandler>(services)`
  - `GetStageStateRequest`의 protobuf `msgName`으로 등록
- `AddPacket<ReportStageStateHandler>(services)`
  - `ReportStageStateCommand`의 protobuf `msgName`으로 등록

같은 형태로 읽으면 된다.

- `protobuf`를 쓰는 경우
  - `msgId = msgName`
- `json`을 쓰는 경우
  - `msgId = class name`

즉 framework는 `GetStageStateHandler` 같은 handler 타입에서 packet 타입을 읽고,
그 packet 타입의 `msgId`를 dispatch key로 등록하는 방향을 기본으로 본다.

예를 들면 아래처럼 읽는다.

- `GetStageStateRequest`의 `msgId`
  - `protobuf`면 protobuf message name
  - `json`이면 `GetStageStateRequest`
- `ReportStageStateCommand`의 `msgId`
  - `protobuf`면 protobuf message name
  - `json`이면 `ReportStageStateCommand`

즉 `AddPacket<GetStageStateHandler>(...)`는 결국
`GetStageStateRequest`의 `msgId`를 기준으로 등록되는 셈이다.

## 7. 어떤 상황에 어떤 API를 쓰는가

- 새 spot 인스턴스를 만들고 싶다
  - `IZLinkSpotManager.CreateAsync(...)`
- 특정 spot 하나에 send packet을 보내고 싶다
  - `SendTo(targetRid, spotRid, ...)`
- 특정 spot 하나에 request packet을 보내고 싶다
  - `RequestTo(targetRid, spotRid, ...)`
- stageId 같은 논리 키로 spot 주소를 찾고 싶다
  - `IZLinkSpotDirectory<TKey>.ResolveAsync(...)`
- 현재 spot 자신의 rid를 알고 싶다
  - `StageSpot.SpotRid`
- stage 안에서 fan-out 하고 싶다
  - `PublishAsync(serviceName, topic, ...)`
- stage 안에서 heartbeat 같은 주기 작업을 돌리고 싶다
  - `StageSpot.OnInitializeAsync()`에서 `AddTimer<THandler>(...)` 등록

## 8. room처럼 써도 되는가

`SPOT`이 room, stage, zone처럼 핫패스가 있는 용도로 쓰이더라도, 이 샘플 구조
자체가 바로 과한 오버헤드를 뜻하는 것은 아니다. 다만 `SPOT` packet handler 쪽은
room 핫패스로 쓰일 수 있으므로, 일반 service messaging보다 더 강한 최적화 기준이
필요하다.

- `AddPacket`, `AddSubscribe`, `AddTimer`는 registration 단계에서만 동작해야 한다.
- reflection은 registration 단계에서 끝나야 한다.
- 런타임 packet 처리 경로에서는 캐시된 dispatch table을 써야 한다.
- handler 호출은 캐시된 delegate 또는 그와 비슷한 저비용 경로여야 한다.
- per-packet allocation과 불필요한 DI 재구성은 줄여야 한다.

즉 샘플의 class 구조보다 더 중요한 것은 실제 실행 경로가 얼마나 짧은가다.
핫패스는 대략 아래 정도로 끝나야 한다.

1. `msgId` 읽기
2. dispatch table lookup
3. packet decode
4. handler resolve 또는 재사용
5. `HandleAsync(...)` 호출

이 문서의 샘플은 "등록 구조"를 보여 주는 예시이고, 실제 성능은 런타임이 reflection을
언제까지 허용하느냐보다, 핫패스에서 그 비용을 제거했느냐에 더 크게 좌우된다.
반대로 일반 socket/service handler가 성능이 낮아도 된다는 뜻은 아니다. 차이는
`SPOT` packet 처리 쪽은 room 핫패스이므로 더 강한 최적화를 먼저 요구하고, 일반
service messaging 쪽은 편의 기능을 조금 더 허용할 여지가 있다는 점이다.

## 9. 피드백 포인트

- `StageSpot : ZLinkSpot` 같은 상속 모델이 자연스러운가
- `AddPacket / AddSubscribe / AddTimer` 같은 등록 표면이 자연스러운가
- packet 매핑을 header `msgId` 기준으로 보는 것이 자연스러운가
- `IZLinkSpotManager`가 spot 객체 factory와 어떻게 연결되는 것이 자연스러운가
- `IZLinkSpotDirectory<TKey>`가 framework 기본 범위인지 wrapper 범위인지
