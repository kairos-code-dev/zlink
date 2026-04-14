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
- discovery 기반 peer 구성
- background subscriber handler

## 2. 기반이 되는 .NET binding

현재 하부 토대는 아래 binding 표면이다.

- `Discovery`
- `SpotNode`
- `Spot`

즉 이 문서의 핵심은 `SPOT` 기능을 새로 만드는 일이 아니라,
기존 binding 기능을 `ASP.NET Core` 안에 녹이는 방법이다.

## 3. ASP.NET Core 등록 모델 초안

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
    });
});
```

이 등록은 아래를 의미한다.

- 논리 `service_name` = `game.stage`
- backing `SpotNode` 생성
- discovery attach
- host shutdown 시 lifecycle 정리

## 4. publish 모델 초안

### 4.1 topic publish

```csharp
public interface IStageSyncPublisher
{
    ValueTask PublishAsync<T>(
        string topic,
        T message,
        CancellationToken cancellationToken = default);
}
```

또는 범용 표면으로 아래 같은 shape도 가능하다.

```csharp
public interface IZLinkSpotPublisher
{
    ValueTask PublishAsync<T>(
        string serviceName,
        string topic,
        T message,
        CancellationToken cancellationToken = default);
}
```

핵심은 publish caller가 low-level `Spot` handle보다 framework-friendly publisher를
보게 하는 것이다.

## 5. subscribe 모델 초안

### 5.1 handler 기반 구독

```csharp
public sealed class StageHandlers
{
    [ZLinkSpotSubscription("game.stage", "zone.*.state")]
    public ValueTask OnZoneStateAsync(
        ZoneStateEvent message,
        ZLinkSpotContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}
```

여기서 기대하는 점은 아래와 같다.

- 구독 등록은 framework startup에서 이뤄진다.
- 수신 메시지는 typed object로 역직렬화된다.
- `ZLinkSpotContext`에서 topic, source rid, dispatch metadata를 본다.

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

## 6. SPOT과 direct call의 관계

`ZLink Framework`는 direct service call만 제공하는 계층처럼 보이면 안 된다.
`SPOT`도 framework 안에서 동등한 축으로 다뤄야 한다.

즉 아래 둘이 함께 있어야 한다.

- `service_name` 기반 direct call
- `SPOT` 기반 pub/sub 및 state sync

이 점은 `playhouse` 시나리오에서 특히 중요하다.

- play -> api 는 direct call
- stage/state sync 는 `SPOT`

## 7. discovery와 service name

`SPOT`도 `service_name` 단위로 설명되어야 한다.

예를 들면:

- `game.stage`
- `game.presence`
- `match.realtime`

애플리케이션은 어떤 `SpotNode`가 어느 endpoint에 붙는지보다,
어느 논리 service에 속하는 `SPOT` runtime인지를 먼저 보게 하는 편이 좋다.

## 8. 아직 확정하지 않는 것

- `SpotNode`를 하나만 둘지 service별로 여러 개 둘지
- publish API를 typed publisher로 얼마나 세분화할지
- `SPOT` routed request/reply까지 framework 표면에 바로 올릴지
- subscriber concurrency와 backpressure를 어떻게 설정으로 노출할지
