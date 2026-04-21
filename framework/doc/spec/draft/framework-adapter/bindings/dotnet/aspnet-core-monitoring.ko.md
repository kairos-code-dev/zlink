[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [Registry](./aspnet-core-registry.ko.md)

# Draft -- ZLink Framework ASP.NET Core Monitoring

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`과 `ASP.NET Core`에서 socket, discovery,
> registry, spot runtime 이벤트를 어떤 표면으로 올릴지 정리한다.

## 1. 목표

운영 관점에서는 handler 호출만 보이는 것으로는 부족하다.
아래 같은 runtime 변화도 프레임워크 표면에서 받을 수 있어야 한다.

- socket connect / disconnect / handshake 실패
- discovery provider up / down / changed
- registry status / topology 변화
- spot node peer / subject 변화

하지만 하부 `.NET zlink` 표면은 source마다 다르다.

- socket: `SocketMonitor`
- discovery: `ServiceMonitor`
- registry: snapshot/query만 있음
- spot: status/peer/subject snapshot만 있음

그래서 framework는 이 네 축을 같은 raw monitor API로 보이게 하기보다,
**typed runtime event**로 다시 올리는 편이 더 자연스럽다.

## 2. 기본 방향

이 초안은 아래 규칙을 기본으로 본다.

- event kind는 enum으로 둔다.
- 실제 callback payload는 record struct로 둔다.
- socket/discovery는 하부 monitor를 감싼다.
- registry/spot는 polling + snapshot diff로 event를 만든다.
- application은 `IZLinkRuntimeEventHandler<TEvent>`를 구현해서 이벤트를 받는다.

enum만으로는 충분하지 않다.
운영 코드에서는 event 종류뿐 아니라 source 이름, endpoint, routing id,
snapshot 본문도 같이 필요하기 때문이다.

## 3. 등록 모델 초안

framework 등록은 아래처럼 두는 편이 자연스럽다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddChannel("profile", channel =>
    {
        channel.EnableServer();
        channel.EnableClient(client =>
        {
            client.UseDiscovery(registry =>
            {
                registry.Add("tcp://registry-1:5551");
            });
        });
    });

    options.AddSpotNode("stage-node", spot =>
    {
        spot.UseSpotDiscovery("game.stage", registry =>
        {
            registry.Add("tcp://registry-1:5551");
        });
    });
});

builder.Services.AddZLinkMonitoring(monitor =>
{
    monitor.AddSocketEvents(
        "profile.server",
        SocketEvent.ConnectionReady | SocketEvent.Disconnected);

    monitor.AddDiscoveryEvents(
        "profile.client.discovery",
        ServiceMonitorEventMask.All);

    monitor.AddRegistryEvents(
        "registry",
        TimeSpan.FromSeconds(1));

    monitor.AddSpotEvents(
        "stage-node",
        TimeSpan.FromSeconds(1));
});
```

여기서 source 이름은 아래처럼 잡는 편이 자연스럽다.

- socket
  - `channel + capability`
  - 예: `profile.server`, `profile.client`
- discovery
  - logical discovery registration 이름
  - 예: `profile.client.discovery`, `game.stage.discovery`
- registry
  - infrastructure source 이름
  - 예: `registry`
- spot
  - spot node 등록 이름
  - 예: `stage-node`

## 4. 인터페이스 초안

기준 인터페이스는 [handler-interfaces.ko.md](./handler-interfaces.ko.md)의 section
10.3을 참고한다. 핵심 모양은 아래 정도다.

```csharp
public interface IZLinkMonitoringOptions
{
    void AddSocketEvents(
        string sourceName,
        SocketEvent events = SocketEvent.All);

    void AddDiscoveryEvents(
        string sourceName,
        params ServiceMonitorEventMask[] events);

    void AddRegistryEvents(
        string sourceName,
        TimeSpan interval);

    void AddSpotEvents(
        string sourceName,
        TimeSpan interval);
}

public interface IZLinkRuntimeEvent
{
    string SourceName { get; }

    DateTimeOffset Timestamp { get; }
}

public interface IZLinkRuntimeEventHandler<in TEvent>
    where TEvent : IZLinkRuntimeEvent
{
    ValueTask HandleAsync(
        TEvent @event,
        CancellationToken cancellationToken);
}
```

socket/discovery/registry/spot은 각각 event kind enum과 record payload를 가진다.

## 5. 샘플 코드

### 5.1 socket 이벤트

```csharp
public sealed class ProfileServerSocketMonitor
    : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    private readonly ILogger<ProfileServerSocketMonitor> _logger;

    public ProfileServerSocketMonitor(
        ILogger<ProfileServerSocketMonitor> logger)
    {
        _logger = logger;
    }

    public ValueTask HandleAsync(
        ZLinkSocketEvent @event,
        CancellationToken cancellationToken)
    {
        switch (@event.Event)
        {
            case ZLinkSocketEventKind.ConnectionReady:
                _logger.LogInformation(
                    "socket ready: {Source} {RemoteAddr}",
                    @event.SourceName,
                    @event.RemoteAddr);
                break;

            case ZLinkSocketEventKind.Disconnected:
                _logger.LogWarning(
                    "socket disconnected: {Source} {RemoteAddr} value={Value}",
                    @event.SourceName,
                    @event.RemoteAddr,
                    @event.Value);
                break;
        }

        return ValueTask.CompletedTask;
    }
}
```

### 5.2 discovery 이벤트

```csharp
public sealed class ProfileDiscoveryMonitor
    : IZLinkRuntimeEventHandler<ZLinkDiscoveryEvent>
{
    private readonly ILogger<ProfileDiscoveryMonitor> _logger;

    public ProfileDiscoveryMonitor(
        ILogger<ProfileDiscoveryMonitor> logger)
    {
        _logger = logger;
    }

    public ValueTask HandleAsync(
        ZLinkDiscoveryEvent @event,
        CancellationToken cancellationToken)
    {
        switch (@event.Event)
        {
            case ZLinkDiscoveryEventKind.ServiceUp:
            case ZLinkDiscoveryEventKind.ServiceDown:
            case ZLinkDiscoveryEventKind.ProvidersChanged:
                _logger.LogInformation(
                    "discovery changed: {Source} {Event} service={ServiceName}",
                    @event.SourceName,
                    @event.Event,
                    @event.ServiceName);
                break;

            case ZLinkDiscoveryEventKind.Error:
                _logger.LogError(
                    "discovery error: {Source} error={ErrorCode}",
                    @event.SourceName,
                    @event.ErrorCode);
                break;
        }

        return ValueTask.CompletedTask;
    }
}
```

### 5.3 registry 이벤트

```csharp
public sealed class RegistryMonitor
    : IZLinkRuntimeEventHandler<ZLinkRegistryEvent>
{
    private readonly ILogger<RegistryMonitor> _logger;

    public RegistryMonitor(ILogger<RegistryMonitor> logger)
    {
        _logger = logger;
    }

    public ValueTask HandleAsync(
        ZLinkRegistryEvent @event,
        CancellationToken cancellationToken)
    {
        switch (@event.Event)
        {
            case ZLinkRegistryEventKind.StatusChanged:
                _logger.LogInformation(
                    "registry status changed: {State}",
                    @event.Status?.State);
                break;

            case ZLinkRegistryEventKind.TopologyChanged:
                _logger.LogInformation(
                    "registry topology changed: {Count}",
                    @event.Topology?.Count ?? 0);
                break;
        }

        return ValueTask.CompletedTask;
    }
}
```

registry는 하부 raw monitor가 없으므로, framework가 주기적으로 snapshot을 읽고
직전 값과 비교해서 event를 만든다.

### 5.4 spot 이벤트

```csharp
public sealed class StageNodeMonitor
    : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
{
    private readonly ILogger<StageNodeMonitor> _logger;

    public StageNodeMonitor(ILogger<StageNodeMonitor> logger)
    {
        _logger = logger;
    }

    public ValueTask HandleAsync(
        ZLinkSpotEvent @event,
        CancellationToken cancellationToken)
    {
        switch (@event.Event)
        {
            case ZLinkSpotEventKind.PeersChanged:
                _logger.LogInformation(
                    "spot peers changed: {Source} peers={Count}",
                    @event.SourceName,
                    @event.Peers?.Count ?? 0);
                break;

            case ZLinkSpotEventKind.SubjectsChanged:
                _logger.LogInformation(
                    "spot subjects changed: {Source} subjects={Count}",
                    @event.SourceName,
                    @event.Subjects?.Count ?? 0);
                break;
        }

        return ValueTask.CompletedTask;
    }
}
```

spot도 registry와 같은 이유로 raw monitor보다 snapshot diff 표면이 더 맞다.
즉 `StatusSnapshot()`, `PeersSnapshot()`, `SubjectsSnapshot()`를 주기적으로 읽고,
변화를 typed event로 올리는 방향을 기본으로 본다.

## 6. 왜 raw monitor를 그대로 노출하지 않는가

한 가지 API로 네 source를 다 덮으려면 결국 가장 낮은 수준의 모양으로 내려가야
한다. 그러면 registry와 spot는 실제 하부 표면보다 더 많은 것을 약속하게 된다.

따라서 현재 초안은 아래처럼 나누는 편을 기본으로 본다.

- socket/discovery
  - raw monitor 기반
- registry/spot
  - snapshot diff 기반
- application
  - typed runtime event handler 기반

이 구분이 있어야 framework가 source별 구현 차이를 숨기면서도, 없는 기능을 있는
것처럼 보이지 않을 수 있다.

## 7. 아직 확정하지 않는 것

- `AddZLinkMonitoring(...)`를 `AddZLinkFramework(...)` 안의 하위 builder로 넣을지
- registry/spot polling 기본 주기를 둘지, 항상 명시하게 할지
- registry event를 `StatusChanged`, `TopologyChanged`, `ServiceSummaryChanged` 세
  가지로 둘지 더 줄일지
- spot event를 `StatusChanged`, `PeersChanged`, `SubjectsChanged` 세 가지로 둘지 더
  줄일지
- socket/discovery event payload에서 raw native enum을 어디까지 같이 노출할지
