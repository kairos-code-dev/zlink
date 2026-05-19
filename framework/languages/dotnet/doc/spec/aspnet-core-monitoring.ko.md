<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework .NET Session Actor Dispatch](./session-actor-dispatch.ko.md) | [다음: ZLink Framework ASP.NET Core Registry Integration](./aspnet-core-registry.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[.NET 묶음](../README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [Registry](./aspnet-core-registry.ko.md)

# ZLink Framework ASP.NET Core Monitoring

## 1. 목표

이 절은 monitoring 표면이 어떤 사건을 담아야 하는지, 그리고 왜 그렇게 정했는지를
정리한다.

운영 관점에서는 handler 호출만 관측해서는 부족하다. 다음과 같은 runtime 변화도
framework 표면에서 함께 받을 수 있어야 한다.

- socket connect / disconnect / handshake[^handshake] 실패
- discovery[^discovery] provider up / down / changed
- registry status / topology[^topology] 변화
- spot node[^spot-node] peer / subject 변화

문제는 하부 `.NET zlink` 표면이 source 마다 모양이 다르다는 점이다.

- socket: `SocketMonitor`
- discovery: runtime event로 노출하지 않는다. 운영 조회는 registry snapshot/query로 처리한다.
- registry: snapshot/query만 제공한다.
- spot: status/peer/subject snapshot만 제공한다.
- timer handler failure: 하부 snapshot 이 아니라 framework timer loop 안에서
  직접 관찰한다.

그래서 framework 는 source 마다 표면을 달리 둔다.

- socket 은 raw monitor[^raw-monitor] 기반 event 로 올린다.
- registry / spot 은 snapshot diff[^snapshot-diff] 기반 event 로 올린다.
- timer handler failure 는 발생 시점에 point-in-time event 로 올린다.
- discovery 자체는 별도 runtime event 로 만들지 않는다. registry 의 topology /
  service / member snapshot 을 조회해서 현재 provider 상태를 확인한다.

## 2. 기본 방향

이 절은 monitoring 표면이 따르는 규칙을 정리한다.

이 문서는 다음 규칙을 기본으로 둔다.

- event kind 는 enum 으로 둔다.
- 실제 callback payload 는 record struct 로 둔다.
- socket 은 하부 monitor 를 그대로 감싼다.
- registry / spot 은 polling[^polling] 과 snapshot diff 로 event 를 합성한다.
- timer handler failure 는 polling interval 을 기다리지 않고 즉시 발행한다.
- discovery 상태는 registry snapshot / query 결과로 조회한다.
- application 은 `IZLinkRuntimeEventHandler<TEvent>` 를 구현해서 이벤트를 받는다.

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

`AddZLinkMonitoring(...)` 은 source 등록만 맡는다. 즉 실제 socket, registry,
spot source 는 같은 애플리케이션에 `AddZLinkFramework(...)` 또는
`AddZLinkRegistry(...)` 로 이미 올라와 있어야 한다.

여기서 한 가지 짚어 둘 점이 있다. 일반 channel capability[^capability] 와 SPOT
mesh 는 각자 자신의 discovery source 를 가진다. 즉 registry endpoint 집합을
공급하는 곳이 둘로 나뉜다.

- 일반 channel: framework 등록 루트의 `UseDiscovery(...)` 가 공급한다.
- SPOT mesh: `AddSpotMesh(...)` 안의 `mesh.UseDiscovery(...)` 가 공급한다.

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

이 절은 monitoring 표면에서 사용자가 직접 마주하는 타입을 정리한다.

기준이 되는 인터페이스는 [handler-interfaces.ko.md](./handler-interfaces.ko.md)
의 section 10.3 을 참고한다. 핵심 모양은 다음 정도다.

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

socket, registry, spot 은 각각 framework 가 소유한 event kind enum 과 record
payload 를 가진다. backend 의 raw monitor enum 이나 status 값이 필요하면,
event 안의 optional diagnostic detail 로만 노출한다.

이 "optional diagnostic" 도 framework 가 소유한 타입으로 다시 감싼다. 즉
backend `.NET` binding 의 `MonitorEventType`, `ServiceEventType`, `SubjectKind`,
`RegistryStatus`, `SpotNodeStatus` 같은 타입은 framework 의 public surface 에
직접 노출하지 않는다.

`AddSocketEvents(...)` 에 event kind 를 따로 넘기지 않으면, 그 source 에서
지원하는 모든 logical event 를 받는다는 뜻으로 해석한다.

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

registry 는 하부에 raw monitor 가 없다. 그래서 framework 가 주기적으로 snapshot
을 읽고, 직전 값과 비교하는 방식으로 event 를 합성한다.

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

            case ZLinkSpotEventKind.TimerHandlerFailed:
            case ZLinkSpotEventKind.TimerStoppedAfterUnhandledException:
                _logger.LogError(
                    "spot timer failed: {Source} {Timer} {Handler} {Exception}",
                    @event.SourceName,
                    @event.TimerDiagnostic?.TimerName,
                    @event.TimerDiagnostic?.HandlerType,
                    @event.TimerDiagnostic?.ExceptionType);
                break;
        }

        return ValueTask.CompletedTask;
    }
}
```

spot 도 registry 와 같은 이유로, raw monitor 보다 snapshot diff 표면이 더 잘
맞는다. 즉 `StatusSnapshot()`, `PeersSnapshot()`, `SubjectsSnapshot()` 를
주기적으로 읽고, 변화가 있을 때 typed event 로 올리는 방향을 기본으로 본다.

timer handler failure 는 snapshot diff 가 아니다. `TimerHandlerFailed` 와
`TimerStoppedAfterUnhandledException` 은 timer callback 에서 처리되지 않은 예외가
발생한 시점에 즉시 발행된다. `AddSpotEvents(sourceName, interval)` 의 `interval`
은 status / peer / subject snapshot diff 에만 적용하고, timer failure event 를
지연시키지 않는다.

`ZLinkSpotEvent` payload 에 노출되는 `ZLinkSpotNodeStatus` 와
`ZLinkSpotNodePeerEntry` 는 첫 필드가 `ChannelName` 이다. spot node 에서 채널
이름은 `AddSpotMesh(...)` 에 넘긴 mesh 이름(예: `"game.stage"`) 이 그대로
들어간다.

timer failure event 의 세부 정보는 `ZLinkSpotTimerDiagnostic` 에 담는다. 이 payload
에는 `SpotRid`, `SpotName`, Entry Spot 여부, timer 이름, handler 타입, callback
번호, fixed-rate 시간표의 tick 번호, exception 타입과 메시지가 들어간다. exception
객체 자체는 public event payload 로 노출하지 않는다.

framework 내부에서 위 두 record 를 묶는 `ZLinkSpotMonitoringSnapshot` 은
internal 타입이다. 따라서 application 코드에서 직접 다루지 않는다.

## 6. 왜 raw monitor 를 그대로 노출하지 않는가

이 절은 source 별로 표면을 따로 둔 이유를 정리한다.

하나의 API 로 네 source 를 전부 덮으려면, 결국 가장 낮은 수준의 모양으로
내려가야 한다. 그러면 registry 와 spot 은 실제 하부 표면이 가진 능력보다 더
많은 것을 약속하게 된다.

따라서 현재 스펙은 다음과 같이 source 를 나누는 편을 기본으로 본다.

- socket
  - raw monitor 기반
- registry/spot 상태
  - snapshot diff 기반
- spot timer failure
  - timer loop 에서 즉시 발행하는 point-in-time event
- discovery
  - registry snapshot/query 기반 조회
- application
  - typed runtime event handler 기반

이렇게 구분해 두어야 framework 가 source 별 구현 차이를 숨기면서도, 없는 기능을
있는 것처럼 보이지 않게 할 수 있다.

## 7. 결정된 기준

이 절은 monitoring 표면이 따르는 고정된 결정 사항을 모아둔 것이다.

- registry / spot polling 주기는 monitoring 등록 시점에 항상 명시한다. 숨은
  기본 주기를 두지 않는다. 운영 코드가 polling cost 를 설정에서 바로 읽을 수
  있게 하는 편을 기본으로 본다.
- registry event 종류는 `StatusChanged`, `TopologyChanged`,
  `ServiceSummaryChanged` 세 가지로 고정한다.
- spot event 종류는 `StatusChanged`, `PeersChanged`, `SubjectsChanged`,
  `TimerHandlerFailed`, `TimerStoppedAfterUnhandledException` 다.
- socket event payload 는 raw native enum 과 상태 코드를 함께 노출한다. 반면
  registry event 와 spot 상태 event 는 snapshot diff 기반의 합성 event 다. timer
  failure event 는 framework timer loop 에서 즉시 만든다. discovery 는 runtime
  event 자체가 아니므로 별도 event payload 를 두지 않는다.

## 8. 회귀 테스트

이 절은 monitoring 표면이 어떤 테스트로 회귀를 막는지를 정리한다.

Monitoring 문서의 항목은 다음을 확인한다.

- 등록한 source 이름이 실제 runtime capability 와 맞는지
- Registry 와 SPOT 상태 변화가 typed event 와 snapshot 으로 관찰되는지
- timer handler failure 가 polling interval 을 기다리지 않고 typed event 로 관찰되는지
- raw monitor event 를 그대로 외부로 새어 보내지 않는다는 정책이 public surface
  테스트에서도 유지되는지

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegistrationValidationTests.AddZLinkMonitoring_Throws_WhenSocketSourceDoesNotMatchRegisteredCapability` | 존재하지 않는 monitoring source 이름은 startup validation 예외로 이어진다. |
| `MonitoringIntegrationTests.RegistryMonitoring_Emits_StatusChanged_For_EmbeddedRegistry` | embedded Registry의 상태 변경 event가 발생한다. |
| `MonitoringIntegrationTests.RegistryMonitoring_Emits_Topology_And_ServiceSummary_When_FrameworkHostRegisters` | framework host 등록 후 topology와 service summary event가 발생한다. |
| `MonitoringIntegrationTests.SpotMonitoring_Emits_SubjectsChanged_When_SpotIsCreated` | spot 생성 후 subject 변화 event가 발생한다. |
| `MonitoringIntegrationTests.SpotMonitoring_Emits_PeersChanged_When_RemoteNodeAppears` | remote spot node가 나타나면 peer 변화 event가 발생한다. |
| `SpotIntegrationTests.SpotTimer_Reports_Handler_Exception_To_Monitoring` | timer handler 예외가 `TimerHandlerFailed` event와 `ZLinkSpotTimerDiagnostic` payload로 발생한다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^handshake]: handshake는 연결 초기에 양쪽이 프로토콜 버전이나 인증 정보를 주고받아 통신 조건을 맞추는 절차다.
[^discovery]: discovery는 분산 환경에서 어떤 서비스가 어느 endpoint에 있는지를 자동으로 알아내는 메커니즘이다. ZLink에서는 registry가 그 역할을 한다.
[^topology]: topology는 어떤 노드(channel, spot, registry 등)가 어디에 있고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^spot-node]: spot node는 여러 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^raw-monitor]: raw monitor는 하부 socket 계층에서 직접 발생하는 저수준 이벤트(연결 성공, 끊김 등)를 그대로 수신하는 메커니즘이다.
[^snapshot-diff]: snapshot diff는 일정 주기로 상태 스냅샷을 읽고, 이전 스냅샷과 비교해서 차이가 있을 때만 event를 합성하는 방식이다.
[^polling]: polling은 주기적으로 상태를 직접 조회해서 변화를 감지하는 방식이다. push 기반 event가 없을 때 사용한다.
[^capability]: capability는 어떤 노드(channel, spot 등)가 외부에 노출하는 역할이나 기능 단위(예: server, client, subscriber, publisher)를 가리킨다.
