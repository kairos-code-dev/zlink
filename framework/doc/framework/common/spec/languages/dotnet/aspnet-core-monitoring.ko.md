<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework .NET Session Actor Dispatch](session-actor-dispatch.ko.md) | [다음: ASP.NET Core Location Integration](aspnet-core-location.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](../../../../dotnet/README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [channel](aspnet-core-channel-messaging.ko.md) | [SPOT](aspnet-core-spot.ko.md) | [STREAM](aspnet-core-stream.ko.md) | [Location](aspnet-core-location.ko.md)

# ZLink Framework ASP.NET Core Monitoring

## 1. 목표

이 절은 monitoring 표면이 어떤 사건을 담아야 하는지, 그리고 왜 그렇게 정했는지를
정리한다.

운영 관점에서는 handler 호출만 관측해서는 부족하다. 다음과 같은 runtime 변화도
framework 표면에서 함께 받을 수 있어야 한다.

- socket connect / disconnect / handshake[^handshake] 실패
- location store[^location-store] 상태와 topology[^topology] 변화
- spot node[^spot-node] peer / subject 변화

문제는 하부 `.NET zlink` 표면이 source 마다 모양이 다르다는 점이다.

- socket: `SocketMonitor`
- location runtime: store 상태와 위치 row 를 주기적으로 조회해 변화 event 를 만든다.
- spot: status/peer/subject snapshot만 제공한다.
- timer handler failure: 하부 snapshot 이 아니라 framework timer loop 안에서
  직접 관찰한다.

그래서 framework 는 source 마다 표면을 달리 둔다.

- socket 은 raw monitor[^raw-monitor] 기반 event 로 올린다.
- location / spot 은 일정 주기로 상태를 읽고 직전 상태와 비교해서 바뀐 때만 event 로 올린다.
- timer handler failure 는 주기적 조회를 기다리지 않고 발생 시점에 바로 event 로 올린다.
- peer/spot/actor/route row 변화는 location 계열 source 로 받거나
  `IZLinkLocationRuntimeQuery` 로 직접 조회한다.

## 2. 기본 방향

이 절은 monitoring 표면이 따르는 규칙을 정리한다.

이 문서는 다음 규칙을 기본으로 둔다.

- event kind 는 enum 으로 둔다.
- 실제 callback payload 는 record struct 로 둔다.
- socket 은 하부 monitor 를 그대로 감싼다.
- location / spot 은 polling[^polling] 으로 상태를 읽고 직전 상태와 비교해서 event 를 합성한다.
- timer handler failure 는 polling interval 을 기다리지 않고 즉시 발행한다.
- location 상태는 `IZLinkLocationRuntimeQuery` 결과와 location runtime event 로 조회한다.
- application 은 `IZLinkRuntimeEventHandler<TEvent>` 를 구현해서 이벤트를 받는다.

enum 하나만으로는 충분하지 않다. 운영 코드에서는 event 종류뿐 아니라 source 이름,
endpoint, routing id, snapshot 본문도 함께 필요하기 때문이다.

## 3. 등록 모델

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

## 4. 인터페이스 계약

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

socket, location, spot 은 각각 framework 가 소유한 event kind enum 과 record
payload 를 가진다. backend 의 raw monitor enum 이나 status 값이 필요하면,
event 안의 optional diagnostic detail 로만 노출한다.

이 "optional diagnostic" 도 framework 가 소유한 타입으로 다시 감싼다. 즉
backend `.NET` binding 의 `MonitorEventType`, `ServiceEventType`, `SubjectKind`,
`SpotNodeStatus` 같은 타입은 framework 의 public API 표면에
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

### 5.2 location runtime 이벤트

```csharp
public sealed class LocationRuntimeMonitor
    : IZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent>
{
    private readonly ILogger<LocationRuntimeMonitor> _logger;

    public LocationRuntimeMonitor(ILogger<LocationRuntimeMonitor> logger)
    {
        _logger = logger;
    }

    public ValueTask HandleAsync(
        ZLinkLocationRuntimeEvent @event,
        CancellationToken cancellationToken)
    {
        switch (@event.Event)
        {
            case ZLinkLocationRuntimeEventKind.StatusChanged:
                _logger.LogInformation(
                    "location store status changed: {Healthy}",
                    @event.Status?.StoreHealthy);
                break;

            case ZLinkLocationRuntimeEventKind.TopologyChanged:
                _logger.LogInformation(
                    "location topology changed: {Count}",
                    @event.Topology?.Count ?? 0);
                break;
        }

        return ValueTask.CompletedTask;
    }
}
```

location runtime 은 하부에 raw monitor 가 없다. 그래서 framework 가 주기적으로
store 상태와 topology snapshot 을 읽고, 직전 값과 비교하는 방식으로 event 를 합성한다.

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

spot 도 같은 이유로, raw monitor 보다 주기적으로 상태를 읽고 직전 상태와 비교하는 표면이
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
내려가야 한다. 그러면 location 과 spot 은 실제 하부 표면이 가진 능력보다 더
많은 것을 약속하게 된다.

따라서 현재 스펙은 다음과 같이 source 를 나누는 편을 기본으로 본다.

- socket
  - raw monitor 기반
- location/spot 상태
  - 주기적으로 상태를 읽고 직전 상태와 비교해서 event 합성
- spot timer failure
  - timer loop 에서 실패가 발생한 시점에 즉시 발행하는 event
- location row
  - `location-peer`, `location-spot`, `location-actor`, `location-route` source 로 관찰
- application
  - typed runtime event handler 기반

이렇게 구분해 두어야 framework 가 source 별 구현 차이를 숨기면서도, 없는 기능을
있는 것처럼 보이지 않게 할 수 있다.

## 7. 결정된 기준

이 절은 monitoring 표면이 따르는 고정된 결정 사항을 모아둔 것이다.

- location / spot polling 주기는 monitoring 등록 시점에 항상 명시한다. 숨은
  기본 주기를 두지 않는다. 운영 코드가 polling cost 를 설정에서 바로 읽을 수
  있게 하는 편을 기본으로 본다.
- location runtime event 종류는 `StatusChanged`, `TopologyChanged`,
  `ServiceSummaryChanged`, `StoreFailure`, `StoreRecovered` 다.
- spot event 종류는 `StatusChanged`, `PeersChanged`, `SubjectsChanged`,
  `TimerHandlerFailed`, `TimerStoppedAfterUnhandledException` 다.
- socket event payload 는 raw native enum 과 상태 코드를 함께 노출한다. 반면
  location event 와 spot 상태 event 는 주기적 상태 비교로 만든 합성 event 다. timer
  failure event 는 framework timer loop 에서 즉시 만든다. 제거된 자동 발견 runtime
  event payload 는 다시 두지 않는다.

### 7.1 startup validation

- socket source는 `<channel>.<capability>` 형식을 사용하며, 해당 channel 역할이 실제로
  등록되어 있어야 한다.
- Spot source는 등록된 SpotNode 이름을 가리켜야 한다.
- location source를 사용하려면 location runtime이 등록되어 있어야 한다. location
  source 이름은 event를 구분하는 사용자 지정 이름이며, store row나 역할 이름과
  대조하지 않는다.
- polling source의 interval은 0보다 커야 한다. 0 이하의 interval은 시작 전에 거부한다.
- 위 조건을 만족하지 않으면 `ZLinkConfigurationException`으로 host 시작 전에
  실패한다.
- 임의 source 자동 발견은 지원하지 않는다. 자동 연결 상태는 `location-runtime`
  source와 runtime query로 관찰한다.

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

monitoring 이 socket/location/spot **runtime 변화**를 다룬다면, 메시지 흐름 추적은 한 메시지의
생애주기(왔나/처리됐나/응답됐나/보냈나/응답받았나)를 dispatch 길목에서 관측한다. 공통 의미
(로그 모드·phase·event·observer·off 제로코스트 성능 계약·출력 라우팅·길목·스트림
correlation_id 와이어)는 [공통 스펙 — 메시지 흐름 추적](../../message-flow-tracing.ko.md)이
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

## 10. 런타임 메트릭 (runtime metrics)

공통 의미(계기 카탈로그·종류·라벨·성능 계약)는 [공통 스펙 — 런타임 메트릭](../../runtime-metrics.ko.md)이
소유한다. 이 절은 `.NET` 표면만 적는다.

> **설계 원칙(깊은 모듈): 공통 케이스는 무설정.** framework는 안정된 이름의 `Meter` 하나로 카탈로그
> 계기를 방출하고, 앱은 자기 OpenTelemetry 파이프라인에 그 meter만 포함한다. per-계기 API는
> 노출하지 않는다 — 계기 갱신 지점·라벨은 framework 내부에 있고, 앱이 배우는 것은 meter 이름
> 하나뿐이다.

### 10.1 표면

| 공통 개념 | `.NET` |
|-----------|--------|
| meter 이름(상수) | `ZLinkMeters.Framework` = `"Zlink.Framework"` |
| 계기 방출 | `System.Diagnostics.Metrics.Meter("Zlink.Framework")` — `Counter`/`UpDownCounter`/`ObservableGauge`/`Histogram` |
| 앱 연결(공통 케이스) | OTel `MeterProviderBuilder.AddMeter(ZLinkMeters.Framework)` — 이게 전부다 |
| 비-OTel/커스텀 수집 | `options.UseMetrics(m => m.UseListener(...))` (선택, `MeterListener` 기반 pull) |

```csharp
// 앱은 이 한 줄로 zlink 계기를 자기 OTel 파이프라인에 넣는다. 별도 zlink 설정 없음.
builder.Services.AddOpenTelemetry().WithMetrics(m => m
    .AddMeter(ZLinkMeters.Framework)
    .AddPrometheusExporter());
```

- 공통 §3의 `updown`=`UpDownCounter<long>`, `observable`=`ObservableGauge`(scrape 시 콜백),
  histogram=`Histogram<double>`(단위 ms, 공통 §4.0).
- meter가 어떤 listener에도 안 붙으면 계기 갱신은 사실상 no-op이다(off 제로코스트, 공통 §7.2).
- 대시보드·exporter는 앱 몫. framework는 내장 scrape 서버를 제공하지 않는다(공통 §6).

### 10.2 왜 새 인터페이스가 없나

`.NET`의 `Meter`/`MeterListener`가 이미 벤더 중립 계기 파사드다. framework가 별도 `IZLinkMetrics*`
표면을 두면 그 위에 pass-through 층이 생겨 깊이가 없다(얕은 모듈). 그래서 공개 표면을 **안정된 meter
이름 하나 + 선택적 `UseMetrics`**로 최소화한다.

## 11. 메시지 흐름 상관관계 (flow correlation)

공통 의미는 [공통 스펙 — 메시지 흐름 상관관계](../../flow-correlation.ko.md)가 소유한다. 이 절은
§9(메시지 흐름 추적)의 **additive 확장**이며 **새 최상위 표면을 만들지 않는다** — 기존
`ConfigureDispatch()` 체인에 모드 하나, 기존 `ZLinkMessageFlowEvent`에 필드 하나를 더한다.

### 11.1 표면

| 공통 개념 | `.NET` |
|-----------|--------|
| flow id 모드 | `ZLinkFlowIdMode` { `None`, `Monotonic`(기본), `GlobalUnique` } |
| 설정 | `ConfigureDispatch().FlowId(ZLinkFlowIdMode.GlobalUnique)` |
| event 필드(추가) | `ZLinkMessageFlowEvent.FlowId` (§9.1 record에 추가), dispatch 오류 이벤트에도 동일 필드 |

```csharp
options.ConfigureDispatch()
    .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
    .FlowId(ZLinkFlowIdMode.GlobalUnique);   // 대규모 fleet 전역 조인; 기본은 Monotonic
```

- 생성은 트레이싱 모드 게이트, **전파(echo)는 무조건**이라 off 노드를 지나도 흐름이 끊기지 않는다
  (공통 §2.2).
- stream/actor gateway 로거는 부트스트랩에서 **자동 배선**된다(공통 §7). 명시 주입이 우선하되, 없을
  때 침묵 대신 기본 sink로 폴백 — "조용한 무로그"를 기본에서 제거한다. 게이팅은 불변(배선 ≠ 출력,
  `Off`면 완전 침묵).
- 로그 토큰 `flow=`는 언어 간 바이트 동일(공통 §8).

## 12. Graceful Drain & Handoff

공통 의미는 [공통 스펙 — Graceful Drain & Handoff](../../graceful-drain-handoff.ko.md)가 소유한다.
이 절은 **lifecycle 제어 표면**(관측이 아님)의 `.NET` 투영이다.

> **설계 원칙(복잡도 하향): 공통 케이스는 무설정.** framework가 host shutdown에 자동 참여해 drain하므로
> 앱은 아무 코드도 쓰지 않는다. draining 마커·owner lease 유지·`Takeover` 순서 같은 분산 정합은
> 공통 스펙 §3이 소유하며 앱 표면에 노출하지 않는다.

### 12.1 표면

| 공통 개념 | `.NET` |
|-----------|--------|
| 자동 drain(기본) | framework hosted service가 `IHostApplicationLifetime` 종료에 참여, `StopAsync`에서 drain — 앱 코드 0 |
| SPOT drain 정책 | spot mesh 등록의 `UseDrainPolicy(ZLinkSpotDrainPolicy.{DrainNatural(기본)/Deadline/ReleaseAndRecreate})` |
| 명시 제어(선택) | `IZLinkDrainControl` { `DrainAsync(TimeSpan deadline, CancellationToken)`, `AwaitDrainedAsync(CancellationToken)`, `bool IsReady { get; }` } (DI singleton) |
| readiness probe | `IZLinkDrainControl.IsReady` 또는 편의 `AddZLinkDrainHealthCheck()` |
| 상태 관측 | 기존 `IZLinkRuntimeEventHandler<ZLinkDrainEvent>` 재사용. `ZLinkDrainEvent.State` { `Serving`/`Draining`/`Drained`/`ForceStopping` } |

```csharp
// SPOT별 정책만 선언 — 나머지는 전부 기본 동작
options.AddSpotMesh("orders")
    .UseDrainPolicy(ZLinkSpotDrainPolicy.ReleaseAndRecreate);  // event-sourcing owner spot

// 배포 자동화가 세밀 제어를 원할 때만 (대개 불필요)
var drain = app.Services.GetRequiredService<IZLinkDrainControl>();
await drain.DrainAsync(TimeSpan.FromSeconds(25), ct);
await drain.AwaitDrainedAsync(ct);
```

- **왜 새 이벤트 구독을 안 만드나:** drain 상태 관측은 monitoring의 `IZLinkRuntimeEventHandler<T>`를
  그대로 쓴다(같은 개념 → 같은 메커니즘). 새 관측 표면을 만들지 않는다.
- **왜 앱이 `Drain`을 안 불러도 되나:** host 종료 신호 처리를 framework가 흡수한다. `IZLinkDrainControl`
  은 배포 자동화가 세밀 제어할 때만 쓰는 탈출구다.
- `IsReady`는 술어 프로퍼티다(`Draining`이면 false). readiness probe가 이 값을 그대로 읽는다.

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^handshake]: handshake는 연결 초기에 양쪽이 프로토콜 버전이나 인증 정보를 주고받아 통신 조건을 맞추는 절차다.
[^location-store]: location store 는 분산 환경에서 어떤 서비스가 어느 endpoint에 있는지를 row 로 저장하고 조회하는 공유 저장소다.
[^topology]: topology는 어떤 노드(channel, spot, location row 등)가 어디에 있고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^spot-node]: spot node는 여러 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^raw-monitor]: raw monitor는 하부 socket 계층에서 직접 발생하는 저수준 이벤트(연결 성공, 끊김 등)를 그대로 수신하는 메커니즘이다.
[^snapshot-diff]: snapshot diff는 일정 주기로 상태 스냅샷을 읽고, 이전 스냅샷과 비교해서 차이가 있을 때만 event를 합성하는 방식이다. 본문에서는 가능한 한 "주기적으로 상태를 읽고 직전 상태와 비교한다"처럼 풀어 쓴다.
[^polling]: polling은 주기적으로 상태를 직접 조회해서 변화를 감지하는 방식이다. push 기반 event가 없을 때 사용한다.
[^capability]: **역할**은 어떤 노드(channel, spot 등)가 외부에 노출하는 기능 단위(예: server, client, subscriber, publisher)를 가리킨다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework .NET Session Actor Dispatch](session-actor-dispatch.ko.md) | [다음: ASP.NET Core Location Integration](aspnet-core-location.ko.md)
<!-- framework-adapter-nav:bottom:end -->
