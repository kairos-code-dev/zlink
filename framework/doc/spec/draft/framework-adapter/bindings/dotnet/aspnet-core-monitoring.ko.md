<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework .NET STREAM Decisions](stream-open-items.ko.md) | [다음: ZLink Framework ASP.NET Core Registry Integration](aspnet-core-registry.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [Registry](./aspnet-core-registry.ko.md)

# Draft -- ZLink Framework ASP.NET Core Monitoring

> 이 문서는 **구현 전 초안**이다.
> 아직 공개 계약[^public-contract]이 아니며, `.NET`과 `ASP.NET Core`에서
> socket, discovery, registry, spot runtime 이벤트를 어떤 표면으로 노출할지를
> 정리한 문서다.

## 1. 목표

운영 관점에서는 handler 호출만 관측할 수 있는 것으로는 부족하다. 다음과 같은
runtime 변화도 framework 표면에서 함께 받을 수 있어야 한다.

- socket connect / disconnect / handshake[^handshake] 실패
- discovery[^discovery] provider up / down / changed
- registry status / topology[^topology] 변화
- spot node[^spot-node] peer / subject 변화

문제는 하부 `.NET zlink` 표면이 source마다 모양이 다르다는 점이다.

- socket: `SocketMonitor`
- discovery: runtime event로 노출하지 않는다. 운영 조회는 registry snapshot/query로 처리한다.
- registry: snapshot/query만 제공한다.
- spot: status/peer/subject snapshot만 제공한다.

따라서 framework는 socket은 raw monitor[^raw-monitor] 기반 event로, registry/spot은
snapshot diff[^snapshot-diff] 기반 event로 올린다. discovery 자체는 별도 runtime
event로 만들지 않고, registry의 topology/service/member snapshot을 조회해서 현재
provider 상태를 확인한다.

## 2. 기본 방향

이 초안은 다음 규칙을 기본으로 둔다.

- event kind는 enum으로 둔다.
- 실제 callback payload는 record struct로 둔다.
- socket은 하부 monitor를 그대로 감싼다.
- registry/spot은 polling[^polling] + snapshot diff로 event를 합성한다.
- discovery 상태는 registry snapshot/query 결과로 조회한다.
- application은 `IZLinkRuntimeEventHandler<TEvent>`를 구현해서 이벤트를 받는다.

enum 하나만으로는 충분하지 않다. 운영 코드에서는 event 종류뿐 아니라 source 이름,
endpoint, routing id, snapshot 본문도 함께 필요하기 때문이다.

## 3. 등록 모델 초안

framework 등록은 다음 모양이 자연스럽다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.UseDiscovery(discovery =>
    {
        discovery.Add("tcp://registry-1:5551");
    });

    options.AddClientServerChannel("profile", channel =>
    {
        channel.EnableServer(server =>
        {
            server.Bind("tcp://0.0.0.0:7101");
        });
        channel.EnableClient();
    });

    options.AddSpotMesh("game.stage", mesh =>
    {
        mesh.UseDiscovery(discovery =>
        {
            discovery.Add("tcp://registry-1:5551");
        });
        mesh.AddNode("stage-node", spot =>
        {
            spot.Bind("tcp://0.0.0.0:9000");
            spot.EnablePubSub();
        });
    });
});

builder.Services.AddZLinkMonitoring(monitor =>
{
    monitor.AddSocketEvents(
        "profile.server",
        ZLinkSocketEventKind.ConnectionReady,
        ZLinkSocketEventKind.Disconnected);

    monitor.AddRegistryEvents(
        "registry",
        TimeSpan.FromSeconds(1));

    monitor.AddSpotEvents(
        "stage-node",
        TimeSpan.FromSeconds(1));
});
```

`AddZLinkMonitoring(...)`은 source 등록만 맡는다. 실제 socket, registry, spot
source는 같은 애플리케이션에 `AddZLinkFramework(...)` 또는
`AddZLinkRegistry(...)`로 이미 올라와 있어야 한다.

여기서 짚어 둘 점은 일반 channel capability[^capability]와 SPOT mesh가 각자 자신의
discovery source를 가진다는 사실이다. 일반 channel은 framework 등록 루트의
`UseDiscovery(...)`가, SPOT mesh는 `AddSpotMesh(...)` 안의
`mesh.UseDiscovery(...)`가 registry endpoint 집합을 공급한다.

source 이름은 다음 규칙으로 잡는 편이 자연스럽다.

- socket
  - `channel + capability` 형태
  - 예: `profile.server`, `profile.client`
- discovery
  - framework는 별도의 monitoring source 이름을 두지 않는다.
  - 현재 provider 상태는 `IZLinkRegistryQuery.TopologySnapshotAsync(...)`,
    `ServiceSummarySnapshotAsync(...)`, `MemberPeersAsync(...)`로 조회한다.
  - application logging 쪽에서 discovery 활동을 별도 식별자로 남기고 싶다면,
    `profile.client.discovery`, `game.stage.discovery` 같은 이름을 application
    logging convention으로 둘 수는 있다. 이 이름은 framework monitoring source의
    등록 이름이 아니다.
- registry
  - infrastructure source 이름
  - 예: `registry`
- spot
  - spot node 등록 이름
  - 예: `stage-node`

## 4. 인터페이스 초안

기준이 되는 인터페이스는 [handler-interfaces.ko.md](./handler-interfaces.ko.md)의
section 10.3을 참고한다. 핵심 모양은 다음 정도다.

```csharp
public interface IZLinkMonitoringOptions
{
    void AddSocketEvents(
        string sourceName,
        params ZLinkSocketEventKind[] events);

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

socket/registry/spot은 각각 framework가 소유한 event kind enum과 record payload를
가진다. backend의 raw monitor enum이나 status 값이 필요하면, event 안의 optional
diagnostic detail로만 노출한다.

이 "optional diagnostic"도 framework가 소유한 타입으로 다시 감싼다. 즉 backend
`.NET` binding의 `MonitorEventType`, `ServiceEventType`, `SubjectKind`,
`RegistryStatus`, `SpotNodeStatus` 같은 타입은 framework의 public surface에 직접
노출하지 않는다.

`AddSocketEvents(...)`에 event kind를 따로 넘기지 않으면, 그 source에서 지원하는
모든 logical event를 받는다는 뜻으로 해석한다.

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

### 5.2 registry 이벤트

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

registry는 하부에 raw monitor가 없으므로, framework가 주기적으로 snapshot을 읽어
직전 값과 비교하는 방식으로 event를 합성한다.

### 5.3 spot 이벤트

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

spot도 registry와 같은 이유로, raw monitor보다 snapshot diff 표면이 더 잘 맞는다.
즉 `StatusSnapshot()`, `PeersSnapshot()`, `SubjectsSnapshot()`를 주기적으로 읽고,
변화가 있을 때 typed event로 올리는 방향을 기본으로 본다.

`ZLinkSpotEvent` payload에 노출되는 `ZLinkSpotNodeStatus`와
`ZLinkSpotNodePeerEntry`는 첫 필드가 `ChannelName`이다. spot node에서 채널 이름은
`AddSpotMesh(...)`에 넘긴 mesh 이름(예: `"game.stage"`)이 그대로 들어간다.
framework 내부에서 위 두 record를 묶는 `ZLinkSpotMonitoringSnapshot`은 internal
타입이므로, application 코드에서 직접 다루지 않는다.

## 6. 왜 raw monitor를 그대로 노출하지 않는가

하나의 API로 네 source를 전부 덮으려면 결국 가장 낮은 수준의 모양으로 내려가야
한다. 그러면 registry와 spot는 실제 하부 표면이 가진 능력보다 더 많은 것을
약속하게 된다.

따라서 현재 초안은 다음과 같이 source를 나누는 편을 기본으로 본다.

- socket
  - raw monitor 기반
- registry/spot
  - snapshot diff 기반
- discovery
  - registry snapshot/query 기반 조회
- application
  - typed runtime event handler 기반

이렇게 구분해 두어야 framework가 source별 구현 차이를 숨기면서도, 없는 기능을
있는 것처럼 보이지 않게 할 수 있다.

## 7. 결정된 기준

- registry/spot polling 주기는 monitoring 등록 시점에 항상 명시한다. 숨은 기본
  주기를 두지 않고, 운영 코드가 polling cost를 설정에서 바로 읽을 수 있게 하는
  편을 기본으로 본다.
- registry event 종류는 `StatusChanged`, `TopologyChanged`,
  `ServiceSummaryChanged` 세 가지로 고정한다.
- spot event 종류는 `StatusChanged`, `PeersChanged`, `SubjectsChanged` 세 가지로
  고정한다.
- socket event payload는 raw native enum과 상태 코드를 함께 노출한다. 반면
  registry/spot event는 snapshot diff 기반의 합성 event이므로 별도의 native enum
  필드를 두지 않는다. discovery는 runtime event 자체가 아니므로 별도 event
  payload를 두지 않는다.

## 8. 회귀 테스트

Monitoring 문서의 항목은 등록한 source 이름이 실제 runtime capability와 맞는지,
Registry와 SPOT 상태 변화가 typed event와 snapshot으로 관찰되는지를 확인한다. raw
monitor event를 그대로 외부로 새어 보내지 않는다는 정책도 public surface 테스트와
함께 점검한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegistrationValidationTests.AddZLinkMonitoring_Throws_WhenSocketSourceDoesNotMatchRegisteredCapability` | 존재하지 않는 monitoring source 이름은 startup validation 예외로 이어진다. |
| `MonitoringIntegrationTests.RegistryMonitoring_Emits_StatusChanged_For_EmbeddedRegistry` | embedded Registry의 상태 변경 event가 발생한다. |
| `MonitoringIntegrationTests.RegistryMonitoring_Emits_Topology_And_ServiceSummary_When_FrameworkHostRegisters` | framework host 등록 후 topology와 service summary event가 발생한다. |
| `MonitoringIntegrationTests.SpotMonitoring_Emits_SubjectsChanged_When_SpotIsCreated` | spot 생성 후 subject 변화 event가 발생한다. |
| `MonitoringIntegrationTests.SpotMonitoring_Emits_PeersChanged_When_RemoteNodeAppears` | remote spot node가 나타나면 peer 변화 event가 발생한다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^handshake]: handshake는 연결 초기에 양쪽이 프로토콜 버전이나 인증 정보를 주고받아 통신 조건을 맞추는 절차다.
[^discovery]: discovery는 분산 환경에서 어떤 서비스가 어느 endpoint에 있는지를 자동으로 알아내는 메커니즘이다. ZLink에서는 registry가 그 역할을 한다.
[^topology]: topology는 어떤 노드(channel, spot, registry 등)가 어디에 있고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^spot-node]: spot node는 여러 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^raw-monitor]: raw monitor는 하부 socket 계층에서 직접 발생하는 저수준 이벤트(연결 성공, 끊김 등)를 그대로 수신하는 메커니즘이다.
[^snapshot-diff]: snapshot diff는 일정 주기로 상태 스냅샷을 읽고, 이전 스냅샷과 비교해서 차이가 있을 때만 event를 합성하는 방식이다.
[^polling]: polling은 주기적으로 상태를 직접 조회해서 변화를 감지하는 방식이다. push 기반 event가 없을 때 사용한다.
[^capability]: capability는 어떤 노드(channel, spot 등)가 외부에 노출하는 역할이나 기능 단위(예: server, client, subscriber, publisher)를 가리킨다.
