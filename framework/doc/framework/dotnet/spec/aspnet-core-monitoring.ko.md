<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework .NET Session Actor Dispatch](session-actor-dispatch.ko.md) | [다음: ASP.NET Core Location Integration](aspnet-core-location.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[.NET 묶음](../README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [channel](aspnet-core-channel-messaging.ko.md) | [SPOT](aspnet-core-spot.ko.md) | [STREAM](aspnet-core-stream.ko.md) | [Location](aspnet-core-location.ko.md)

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
- registry / spot 은 일정 주기로 상태를 읽고 직전 상태와 비교해서 바뀐 때만 event 로 올린다.
- timer handler failure 는 주기적 조회를 기다리지 않고 발생 시점에 바로 event 로 올린다.
- discovery 자체는 별도 runtime event 로 만들지 않는다. registry 의 topology /
  service / member snapshot 을 조회해서 현재 provider 상태를 확인한다.

## 2. 기본 방향

이 절은 monitoring 표면이 따르는 규칙을 정리한다.

이 문서는 다음 규칙을 기본으로 둔다.

- event kind 는 enum 으로 둔다.
- 실제 callback payload 는 record struct 로 둔다.
- socket 은 하부 monitor 를 그대로 감싼다.
- registry / spot 은 polling[^polling] 으로 상태를 읽고 직전 상태와 비교해서 event 를 합성한다.
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

    {
        var channel =     options.AddClientServerChannel("profile");
        channel.EnableServer("tcp://0.0.0.0:7101");
        channel.EnableClient();

    }

    {
        var mesh =     options.AddSpotMesh("game.stage");
        {
            var spot = mesh;
            spot.EnablePubSub("tcp://0.0.0.0:9000");

        }

    }
});

builder.Services.AddZLinkMonitoring(monitor =>
{
    monitor.AddSocketEvents(
        "profile.server",
        ZLinkSocketEventKind.ConnectionReady,
        ZLinkSocketEventKind.Disconnected);

    monitor.AddSpotEvents(
        "stage-node",
        TimeSpan.FromSeconds(1));
});
```

`AddZLinkMonitoring(...)` 은 source 등록만 맡는다. 즉 실제 socket, spot source 는
같은 애플리케이션에 `AddZLinkFramework(...)` 또는

여기서 한 가지 짚어 둘 점이 있다. 일반 channel 역할[^capability] 과 SPOT mesh 는
각자 monitoring source 이름을 가진다.

source 이름은 다음 규칙으로 잡는 편이 자연스럽다.

- socket
  - `channel + capability` 형태
  - 예: `profile.server`, `profile.client`
- spot
  - spot node 등록 이름
  - 예: `stage-node`

## 4. 인터페이스 초안

이 절은 monitoring 표면에서 사용자가 직접 마주하는 타입을 정리한다.

기준이 되는 인터페이스는 [handler-interfaces.ko.md](handler-interfaces.ko.md)
의 section 10.3 을 참고한다. 핵심 모양은 다음 정도다.

```csharp
public interface IZLinkMonitoringOptions
{
    void AddSocketEvents(
        string sourceName,
        params ZLinkSocketEventKind[] events);

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
`RegistryStatus`, `SpotNodeStatus` 같은 타입은 framework 의 public API 표면에
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
{
    private readonly ILogger<RegistryMonitor> _logger;

    public RegistryMonitor(ILogger<RegistryMonitor> logger)
    {
        _logger = logger;
    }

    public ValueTask HandleAsync(
        CancellationToken cancellationToken)
    {
        switch (@event.Event)
        {
                _logger.LogInformation(
                    "registry status changed: {State}",
                    @event.Status?.State);
                break;

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

spot 도 registry 와 같은 이유로, raw monitor 보다 주기적으로 상태를 읽고 직전 상태와 비교하는 표면이
더 잘 맞는다. 즉 `Status()`, `Peers()`, `Subjects()` 를
주기적으로 읽고, 변화가 있을 때 typed event 로 올리는 방향을 기본으로 본다.

timer handler failure 는 주기적 상태 비교 결과가 아니다. `TimerHandlerFailed` 와
`TimerStoppedAfterUnhandledException` 은 timer callback 에서 처리되지 않은 예외가
발생한 시점에 즉시 발행된다. `AddSpotEvents(sourceName, interval)` 의 `interval`
은 status / peer / subject 상태 비교에만 적용하고, timer failure event 를
지연시키지 않는다.

`ZLinkSpotEvent` payload 에 노출되는 `ZLinkSpotNodeStatus` 와
`ZLinkSpotNodePeerEntry` 는 첫 필드가 `ChannelName` 이다. spot node 에서 채널
이름은 `AddSpotMesh` 에 넘긴 mesh 이름(예: `"game.stage"`) 이 그대로
들어간다.

timer failure event 의 세부 정보는 `ZLinkSpotTimerDiagnostic` 에 담는다. 이 payload
에는 `SpotRid`, Entry Spot 여부, timer 이름, handler 타입, callback
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
  - 주기적으로 상태를 읽고 직전 상태와 비교해서 event 합성
- spot timer failure
  - timer loop 에서 실패가 발생한 시점에 즉시 발행하는 event
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
  registry event 와 spot 상태 event 는 주기적 상태 비교로 만든 합성 event 다. timer
  failure event 는 framework timer loop 에서 즉시 만든다. discovery 는 runtime
  event 자체가 아니므로 별도 event payload 를 두지 않는다.

## 8. 회귀 테스트

이 절은 monitoring 표면이 어떤 테스트로 회귀를 막는지를 정리한다.

Monitoring 문서의 항목은 다음을 확인한다.

- 등록한 source 이름이 실제 runtime 역할과 맞는지
- SPOT 상태 변화와 socket 상태 변화가 typed event 로 관찰되는지
- timer handler failure 가 polling interval 을 기다리지 않고 typed event 로 관찰되는지
- raw monitor event 를 그대로 외부로 새어 보내지 않는다는 정책이 public API 표면
  테스트에서도 유지되는지

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `CoverageCriticalRuntimeTests.MonitoringEventMapper_MapsAndFiltersSocketEvents` | socket runtime event 를 public monitoring event 로 매핑하고 내부 event 는 밖으로 내보내지 않는다. |
| `CoverageCriticalRuntimeTests.SpotTimerFailureEventFactory_MapsStoppedAndContinuingFailures` | timer handler 예외가 계속 실행되는 실패와 timer 중단 실패를 구분해 typed event 로 만들어진다. |

## 9. 메시지 흐름 추적 (dispatch 관측)

monitoring 이 socket/registry/spot **runtime 변화**를 다룬다면, 메시지 흐름 추적은 한 메시지의
생애주기(왔나/처리됐나/응답됐나/보냈나/응답받았나)를 dispatch 길목에서 관측한다. 공통 의미
(로그 모드·phase·event·observer·off 제로코스트 성능 계약·출력 라우팅·길목·스트림
correlation_id 와이어)는 [공통 스펙 — 메시지 흐름 추적](../../common/spec/message-flow-tracing.ko.md)이
소유한다. 이 절은 그 의미의 `.NET` 표면만 적는다. dispatch **제어**가 아니라 **관측**이며,
observer 실패가 메시지 처리나 응답 전송을 깨지 않는다.

### 9.1 표면

| 공통 개념 | `.NET` 타입 / 멤버 |
|-----------|---------------------|
| 로그 모드 | `ZLinkMessageFlowLogMode` { `Off`, `ErrorsOnly`(기본), `KeyTransitions`, `Verbose`, `Diagnostic` } |
| outcome | `ZLinkMessageFlowOutcome` { `Received`, `Dispatched`, `Replied`, `Dropped`, `Sent`, `ReplyReceived`, `Error` } |
| event | `ZLinkMessageFlowEvent`(record): `Outcome`, `Surface`, `MessageKind`, `PacketName`, `ChannelName`, `Topic`, `CorrelationId`, `SourceRid`, `LocalRid`, `PeerRid`, `SocketRole`, `SpotRid`, `ActorId`, `MessageSize`, 오류 필드 |
| observer | `IZLinkMessageFlowObserver.OnMessageFlowAsync(ZLinkMessageFlowEvent, CancellationToken)` |
| 진단 옵션(read-only) | `IZLinkDispatchOptions.Diagnostics` → `IZLinkDiagnosticsOptions` { `MessageFlow`, `EffectiveMessageFlow`, `SampleRate`, `IncludeMessageSizes`, `LogFile`, `Label` } |
| 런타임 토글 | `IZLinkMessageFlowControl.SetMessageFlowMode(...)` / `MessageFlowMode` (DI singleton) |

게이팅(공통 규칙): `Dropped`·에러는 `ErrorsOnly` 이상, 성공 전이(`Received`/`Dispatched`/
`Replied`/`Sent`/`ReplyReceived`)는 `KeyTransitions` 이상에서 발화한다. `SampleRate<1`은 성공
전이만 thinning하고 `Dropped`·에러는 항상 통과한다.

### 9.2 설정 (builder 전용)

진단 필드는 read-only이며 `ConfigureDispatch()` fluent 체인으로만 설정한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.ConfigureDispatch()
        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
        .TraceLogFile("logs/flow-api.log")   // 지정=전용 파일(앱 로그와 분리)
        .TraceLabel("api")                  // 구조화 필드 label= 식별자
        .IncludeMessageSizes(true)           // Verbose에서 size= 출력
        .SetMessageFlowObserver<ApiFlowObserver>();   // 선택: 콜렉터/OTel 어댑터(앱 레이어)
});
```

- `TraceLogFile` 지정 시 트레이싱/에러는 전용 파일로만 가고 앱 `ILogger`와 섞이지 않는다.
  미지정 + 앱 로거 sink 있으면 통합, 둘 다 없으면 표준 에러스트림 폴백.
- 출력은 카테고리 `zlink.framework.dispatch` + 구조화 필드(phase/surface/kind/packet/channel/
  topic/corr/src/spot/actor/size/node)로 나가 콜렉터가 정규식 파싱 없이 ingest할 수 있다.
- `Off`일 때는 이벤트를 생성조차 하지 않아(호출부 가드 + lazy) 운영 성능에 영향이 없다.

### 9.3 런타임 토글

`IZLinkMessageFlowControl`을 DI에서 받아 재시작 없이 모드를 바꾼다. 공유 live cell을 모든
surface가 읽으므로 즉시 반영된다. `MessageFlow(...)`는 seed(기본값)다.

```csharp
var control = app.Services.GetRequiredService<IZLinkMessageFlowControl>();
control.SetMessageFlowMode(ZLinkMessageFlowLogMode.KeyTransitions);  // off→on, 즉시 반영
```

### 9.4 관측 백엔드 경계

framework 가 제공하는 것은 `CorrelationId` + 구조화 필드 + observer 훅까지다. OpenTelemetry /
span / 외부 콜렉터(Loki/ELK 등) 어댑터는 앱이 `IZLinkMessageFlowObserver` 콜백에서 받아 끼운다.
framework 는 OTel에 의존하지 않는다(공통 스펙 §6 경계 원칙).

### 9.5 샘플

Bingo 3노드(Api/Play/Session)는 각자 `MessageFlow(KeyTransitions)` +
`TraceLogFile(SampleFlowLog.Path(role))` + `TraceLabel(role)`로 분리 파일 로깅을 시연한다
(`BINGO_LOG_DIR`로 로그 디렉토리 override). 한 요청을 `corr=`로 grep하면 노드 간
`outcome=sent`→`outcome=received`→`outcome=replied`→`outcome=reply-received`가 시간순으로 이어진다.

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^handshake]: handshake는 연결 초기에 양쪽이 프로토콜 버전이나 인증 정보를 주고받아 통신 조건을 맞추는 절차다.
[^discovery]: discovery는 분산 환경에서 어떤 서비스가 어느 endpoint에 있는지를 자동으로 알아내는 메커니즘이다. ZLink에서는 registry가 그 역할을 한다.
[^topology]: topology는 어떤 노드(channel, spot, registry 등)가 어디에 있고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^spot-node]: spot node는 여러 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^raw-monitor]: raw monitor는 하부 socket 계층에서 직접 발생하는 저수준 이벤트(연결 성공, 끊김 등)를 그대로 수신하는 메커니즘이다.
[^snapshot-diff]: snapshot diff는 일정 주기로 상태 스냅샷을 읽고, 이전 스냅샷과 비교해서 차이가 있을 때만 event를 합성하는 방식이다. 본문에서는 가능한 한 "주기적으로 상태를 읽고 직전 상태와 비교한다"처럼 풀어 쓴다.
[^polling]: polling은 주기적으로 상태를 직접 조회해서 변화를 감지하는 방식이다. push 기반 event가 없을 때 사용한다.
[^capability]: **역할**은 어떤 노드(channel, spot 등)가 외부에 노출하는 기능 단위(예: server, client, subscriber, publisher)를 가리킨다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework .NET Session Actor Dispatch](session-actor-dispatch.ko.md) | [다음: ASP.NET Core Location Integration](aspnet-core-location.ko.md)
<!-- framework-adapter-nav:bottom:end -->
