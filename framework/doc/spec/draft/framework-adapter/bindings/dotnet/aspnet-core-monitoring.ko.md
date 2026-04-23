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
    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry-1:5551");
    });

    options.UseSpotDiscovery("game.stage", registry =>
    {
        registry.Add("tcp://registry-1:5551");
    });

    options.AddChannel("profile", channel =>
    {
        channel.EnableServer(server =>
        {
            server.Bind("tcp://0.0.0.0:7101");
        });
        channel.EnableClient();
    });

    options.AddSpotNode("stage-node", spot =>
    {
        spot.Bind("tcp://0.0.0.0:9000");
        spot.EnableRouter();
    });
});

builder.Services.AddZLinkMonitoring(monitor =>
{
    monitor.AddSocketEvents(
        "profile.server",
        ZLinkSocketEventKind.ConnectionReady,
        ZLinkSocketEventKind.Disconnected);

    monitor.AddDiscoveryEvents(
        "profile.client.discovery",
        ZLinkDiscoveryEventKind.ServiceUp,
        ZLinkDiscoveryEventKind.ServiceDown,
        ZLinkDiscoveryEventKind.ProvidersChanged,
        ZLinkDiscoveryEventKind.Error,
        ZLinkDiscoveryEventKind.Closed);

    monitor.AddRegistryEvents(
        "registry",
        TimeSpan.FromSeconds(1));

    monitor.AddSpotEvents(
        "stage-node",
        TimeSpan.FromSeconds(1));
});
```

`AddZLinkMonitoring(...)`은 source 등록만 맡는다. 실제 socket, discovery, registry,
spot source는 같은 애플리케이션에 `AddZLinkFramework(...)` 또는
`AddZLinkRegistry(...)`로 먼저 올라와 있어야 한다.

여기서 중요한 점은 discovery registration이 capability builder 아래가 아니라
framework 등록 루트에 있다는 점이다. 일반 channel capability는
`UseDiscovery(...)`가, SPOT mesh는 `UseSpotDiscovery(...)`가 registry endpoint
집합을 공급한다.

source 이름은 아래처럼 잡는 편이 자연스럽다.

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
        params ZLinkSocketEventKind[] events);

    void AddDiscoveryEvents(
        string sourceName,
        params ZLinkDiscoveryEventKind[] events);

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

socket/discovery/registry/spot은 각각 framework 소유 event kind enum과 record
payload를 가진다. backend raw monitor enum과 status 값이 필요하면 event 안의
optional diagnostic detail로만 노출한다.

여기서 "optional diagnostic"도 framework 소유 타입으로 다시 감싼다.
즉 backend `.NET` binding의 `MonitorEventType`, `ServiceEventType`,
`SubjectKind`, `RegistryStatus`, `SpotNodeStatus` 같은 타입을 framework public
surface에 직접 노출하지 않는다.

`AddSocketEvents(...)`, `AddDiscoveryEvents(...)`에 event kind를 따로 넘기지 않으면,
그 source에서 지원하는 모든 logical event를 받는 뜻으로 읽는다.

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
                    @event.Diagnostic?.NativeValue);
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
                    @event.Diagnostic?.ErrorCode);
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

## 7. 결정된 기준

- registry/spot polling 주기는 monitoring 등록 시 항상 명시한다.
  숨은 기본 주기를 두지 않고, 운영 코드가 polling cost를 설정에서 바로 읽을 수 있게
  하는 편을 기본으로 본다.
- registry event 종류는 `StatusChanged`, `TopologyChanged`,
  `ServiceSummaryChanged` 세 가지로 고정한다.
- spot event 종류는 `StatusChanged`, `PeersChanged`, `SubjectsChanged`
  세 가지로 고정한다.
- socket/discovery event payload는 raw native enum과 상태 코드를 함께 노출한다.
  반면 registry/spot event는 snapshot diff 기반 synthetic event이므로 별도 native
  enum 필드를 두지 않는다.
