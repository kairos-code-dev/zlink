# .NET monitoring과 Framework 오류 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

## 1. Monitoring과 Framework 오류

Monitoring source는 framework root와 같은 DI container에 등록한다. 공개 source 종류와 등록 표면은
다음과 같다. `ZLinkSocketEventKind`는 다음 여섯 값으로 닫혀 있다.

```csharp
public interface IZLinkMonitoringOptions
{
    void AddSocketEvents(
        string sourceName,
        params ZLinkSocketEventKind[] events);
    void AddMeshNodeEvents(string meshName);
    void AddLocationRuntimeEvents(string sourceName, TimeSpan interval);
}

public enum ZLinkSocketEventKind
{
    Connected = 0,
    ConnectionReady = 1,
    Disconnected = 2,
    HandshakeFailed = 3,
    PeerAdmissionChanged = 4,
    Closed = 5
}
```

Location monitoring은 provider-neutral runtime status, topology와 service summary만 공개한다. Store row update,
resolve miss와 auto-connect reconcile diff를 별도 source로 등록하는 API는 제공하지 않는다. 해당 값은 provider
DTO와 owner fence를 포함하는 내부 reducer 입력이며 application은 `ZLinkLocationRuntimeEvent`,
`ZLinkMeshRuntimeEvent`, message-flow event와 runtime snapshot으로 운영 상태를 확인한다.

Request, lifecycle과 one-way operation의 framework 실패는 다음 exception으로 전달한다. One-way operation은
정상 완료 값을 반환하지 않는다. 잘못된 public 인자는 .NET 표준 `ArgumentException` 계열로 거부한다.

```csharp
public enum ZLinkFrameworkErrorKind
{
    ActorRouteNotFound = 0,
    ActorCreateFailed = 1,
    ActorAlreadyExists = 2,
    ActorTypeMismatch = 3,
    SpotCreateFailed = 4,
    SpotRouteNotFound = 5,
    SpotTypeMismatch = 6,
    ActorSessionNotBound = 7,
    HandlerNotFound = 8,
    RouteHandlerNotFound = 9,
    ActorDispatchHandlerNotFound = 10,
    PayloadDecodeFailed = 11,
    RouteNotConnected = 12,
    RequestTargetNotFound = 13,
    RequestRejected = 14,
    RequestProtocolError = 15,
    RequestFailed = 16,
    WorkerQueueFull = 17,
    WorkerTimedOut = 18,
    WorkerFailed = 19,
    ActorLocationStale = 20,
    ActorCreateRejected = 21,
    ObjectClientNotConfigured = 22,
    MeshSelectionRequired = 23,
    MeshNotFound = 24,
    InvalidConfiguration = 25,
    AlreadySubmitted = 26,
    ActorGenerationStale = 27,
    ActorMoving = 28,
    DeadlineExceeded = 29,
    PlacementCapacityExhausted = 30,
    RoutingIdConflict = 31,
    SpotGenerationStale = 32,
    SpotMoving = 33,
    RelocationDataLost = 34,
    SpotIdConflict = 35,
    RuntimeShutdown = 36,
    RelocationDisabled = 37,
    RelocationTargetUnavailable = 38,
    RelocationFailed = 39
}

public sealed class ZLinkFrameworkException : Exception
{
    public ZLinkFrameworkException(
        ZLinkFrameworkErrorKind kind,
        string message,
        bool? isRetriable = null,
        Exception? innerException = null);
    public ZLinkFrameworkErrorKind Kind { get; }
    public bool IsRetriable { get; }
}

public sealed class ZLinkConfigurationException : InvalidOperationException
{
    public ZLinkConfigurationException(string message);
}
```

`Kind`의 숫자 값과 기본 재시도 의미는 [공통 Framework API](../../../../05-framework-api.ko.md#13-오류-kind)와
같다. `RelocationDataLost`는 Location authority가 공개한 Relocation reference의 payload를 영구적으로 찾을 수
없거나 checksum·inventory digest가 일치하지 않을 때 반환하는 non-retriable 오류다. Runtime은 이 오류에서
이전 owner로 rollback하지 않는다. Remote framework error는 `ZLinkFrameworkException`으로 전달한다.
`RuntimeShutdown`은 runtime이 신규 admission을 받지 않는 terminal state에서 사용한다.
`RelocationDisabled`는 object policy가 cross-node 이동을 허용하지 않을 때,
`RelocationTargetUnavailable`은 호환 target을 확보하지 못했을 때,
`RelocationFailed`는 admission callback exception이나 capture·factory·restore·staging
실패가 발생했을 때 사용한다.

`RoutingIdConflict`는 transport RID claim 충돌이고 `SpotIdConflict`는 global Spot ID claim 충돌이다.
## 2. Eventing과 metric identity

```csharp
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

public readonly record struct ZLinkSocketEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkSocketEventKind Event,
    RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr) : IZLinkRuntimeEvent;

public static class ZLinkMeters
{
    public const string Framework = "zlink.framework";
}

public enum ZLinkFlowOrigin : byte
{
    Inbound = 1,
    Timer = 2,
    Application = 3,
    Lifecycle = 4
}

public abstract record ZLinkLocationRuntimeEvent : IZLinkRuntimeEvent
{
    private protected ZLinkLocationRuntimeEvent(string sourceName, DateTimeOffset timestamp)
    {
        SourceName = sourceName;
        Timestamp = timestamp;
    }
    public string SourceName { get; init; }
    public DateTimeOffset Timestamp { get; init; }
    public void Deconstruct(out string SourceName, out DateTimeOffset Timestamp) =>
        (SourceName, Timestamp) = (this.SourceName, this.Timestamp);
    public sealed record StatusChanged(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkLocationRuntimeStatus Status) : ZLinkLocationRuntimeEvent(SourceName, Timestamp);
    public sealed record TopologyChanged(
        string SourceName,
        DateTimeOffset Timestamp,
        IReadOnlyList<ZLinkLocationTopologyEntry> Topology) : ZLinkLocationRuntimeEvent(SourceName, Timestamp);
    public sealed record ServiceSummaryChanged(
        string SourceName,
        DateTimeOffset Timestamp,
        IReadOnlyList<ZLinkLocationServiceSummary> ServiceSummary) : ZLinkLocationRuntimeEvent(SourceName, Timestamp);
    public sealed record StoreFailure(
        string SourceName,
        DateTimeOffset Timestamp) : ZLinkLocationRuntimeEvent(SourceName, Timestamp);
    public sealed record StoreRecovered(
        string SourceName,
        DateTimeOffset Timestamp) : ZLinkLocationRuntimeEvent(SourceName, Timestamp);
}

public readonly record struct ZLinkSpotTimerDiagnostic(
    string SpotId,
    bool IsEntrySpot,
    string TimerName,
    string HandlerType,
    ulong DeliveryIndex,
    ulong ScheduledIndex,
    string ExceptionType,
    string ExceptionMessage);

public abstract record ZLinkSpotEvent : IZLinkRuntimeEvent
{
    private protected ZLinkSpotEvent(string sourceName, DateTimeOffset timestamp)
    {
        SourceName = sourceName;
        Timestamp = timestamp;
    }
    public string SourceName { get; init; }
    public DateTimeOffset Timestamp { get; init; }
    public void Deconstruct(out string SourceName, out DateTimeOffset Timestamp) =>
        (SourceName, Timestamp) = (this.SourceName, this.Timestamp);
    public sealed record TimerHandlerFailed(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkSpotTimerDiagnostic Diagnostic) : ZLinkSpotEvent(SourceName, Timestamp);
    public sealed record TimerStoppedAfterUnhandledException(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkSpotTimerDiagnostic Diagnostic) : ZLinkSpotEvent(SourceName, Timestamp);
}
```
