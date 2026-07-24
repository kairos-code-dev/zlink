# .NET monitoring과 Framework 오류 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

## 1. Monitoring과 Framework 오류

Monitoring source는 framework root와 같은 DI container에 등록한다. 공개 source 종류와 등록 표면은
다음과 같다. `ZLinkSocketEventKind`는 다음 일곱 값으로 닫혀 있다.

```csharp
public interface IZLinkMonitoringOptions
{
    void AddSocketEvents(
        string sourceName,
        params ZLinkSocketEventKind[] events);
    void AddMeshNodeEvents(string meshName);
    void AddSpotEvents(string sourceName, TimeSpan interval);
    void AddLocationRuntimeEvents(string sourceName, TimeSpan interval);
    void AddLocationPeerEvents(string sourceName);
    void AddLocationSpotEvents(string sourceName);
    void AddLocationActorEvents(string sourceName);
}

public enum ZLinkSocketEventKind
{
    Connected = 0,
    ConnectionReady = 1,
    Disconnected = 2,
    HandshakeFailed = 3,
    PeerAdmissionChanged = 4,
    Closed = 5,
    Internal = 6
}
```

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
    RuntimeShutdown = 36
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

`RoutingIdConflict`는 transport RID claim 충돌이고 `SpotIdConflict`는 global Spot ID claim 충돌이다.
## 7. Eventing과 metric identity

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

public interface IZLinkRuntimeEventPublisher
{
    ValueTask PublishAsync<TEvent>(
        TEvent @event,
        CancellationToken cancellationToken)
        where TEvent : IZLinkRuntimeEvent;
}

public enum ZLinkSocketNativeEventType
{
    Connected = 0x0001,
    ConnectDelayed = 0x0002,
    ConnectRetried = 0x0004,
    Listening = 0x0008,
    BindFailed = 0x0010,
    Accepted = 0x0020,
    AcceptFailed = 0x0040,
    Closed = 0x0080,
    CloseFailed = 0x0100,
    Disconnected = 0x0200,
    MonitorStopped = 0x0400,
    HandshakeFailedNoDetail = 0x0800,
    ConnectionReady = 0x1000,
    HandshakeFailedProtocol = 0x2000,
    HandshakeFailedAuth = 0x4000,
    PeerAdmissionChanged = 0x8000
}

public readonly record struct ZLinkSocketDiagnostic(
    ZLinkSocketNativeEventType NativeEvent,
    uint NativeValue);

public readonly record struct ZLinkSocketEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkSocketEventKind Event,
    RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr,
    ZLinkSocketDiagnostic? Diagnostic) : IZLinkRuntimeEvent;

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

public enum ZLinkDrainState
{
    Serving = 0,
    Draining = 1,
    Drained = 2,
    ForceStopping = 3
}

public sealed record ZLinkDrainEvent(
    DateTimeOffset Timestamp,
    ZLinkDrainState State) : IZLinkRuntimeEvent
{
    public string SourceName => "drain";
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

public readonly record struct ZLinkAutoConnectDesiredSetChange(
    ZLinkLocationAutoConnectType AutoConnectType,
    string MeshName,
    IReadOnlyList<string> ConnectedEndpoints,
    IReadOnlyList<string> DisconnectedEndpoints);

public abstract record ZLinkLocationPeerEvent : IZLinkRuntimeEvent
{
    private protected ZLinkLocationPeerEvent(string sourceName, DateTimeOffset timestamp)
    {
        SourceName = sourceName;
        Timestamp = timestamp;
    }
    public string SourceName { get; init; }
    public DateTimeOffset Timestamp { get; init; }
    public void Deconstruct(out string SourceName, out DateTimeOffset Timestamp) =>
        (SourceName, Timestamp) = (this.SourceName, this.Timestamp);
    public sealed record RowUpdated(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkMeshNodeDescriptorKey Key,
        ZLinkMeshNodeDescriptor Descriptor) : ZLinkLocationPeerEvent(SourceName, Timestamp);
    public sealed record RowRemoved(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkMeshNodeDescriptorKey Key) : ZLinkLocationPeerEvent(SourceName, Timestamp);
    public sealed record DesiredSetChanged(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkAutoConnectDesiredSetChange Change) : ZLinkLocationPeerEvent(SourceName, Timestamp);
}

public abstract record ZLinkLocationSpotEvent : IZLinkRuntimeEvent
{
    private protected ZLinkLocationSpotEvent(string sourceName, DateTimeOffset timestamp)
    {
        SourceName = sourceName;
        Timestamp = timestamp;
    }
    public string SourceName { get; init; }
    public DateTimeOffset Timestamp { get; init; }
    public void Deconstruct(out string SourceName, out DateTimeOffset Timestamp) =>
        (SourceName, Timestamp) = (this.SourceName, this.Timestamp);
    public sealed record RowUpdated(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkSpotLocationKey Key,
        ZLinkSpotLocation Spot) : ZLinkLocationSpotEvent(SourceName, Timestamp);
    public sealed record RowRemoved(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkSpotLocationKey Key) : ZLinkLocationSpotEvent(SourceName, Timestamp);
    public sealed record ResolveMiss(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkSpotLocationKey Key) : ZLinkLocationSpotEvent(SourceName, Timestamp);
}

public abstract record ZLinkLocationActorEvent : IZLinkRuntimeEvent
{
    private protected ZLinkLocationActorEvent(string sourceName, DateTimeOffset timestamp)
    {
        SourceName = sourceName;
        Timestamp = timestamp;
    }
    public string SourceName { get; init; }
    public DateTimeOffset Timestamp { get; init; }
    public void Deconstruct(out string SourceName, out DateTimeOffset Timestamp) =>
        (SourceName, Timestamp) = (this.SourceName, this.Timestamp);
    public sealed record RowUpdated(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkActorLocationKey Key,
        ZLinkActorLocation Actor) : ZLinkLocationActorEvent(SourceName, Timestamp);
    public sealed record RowRemoved(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkActorLocationKey Key) : ZLinkLocationActorEvent(SourceName, Timestamp);
    public sealed record ResolveMiss(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkActorLocationKey Key) : ZLinkLocationActorEvent(SourceName, Timestamp);
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
    public sealed record StatusChanged(
        string SourceName,
        DateTimeOffset Timestamp,
        ZLinkSpotNodeStatus Status) : ZLinkSpotEvent(SourceName, Timestamp);
    public sealed record PeersChanged(
        string SourceName,
        DateTimeOffset Timestamp,
        IReadOnlyList<ZLinkSpotNodePeerEntry> Peers) : ZLinkSpotEvent(SourceName, Timestamp);
    public sealed record SubjectsChanged(
        string SourceName,
        DateTimeOffset Timestamp,
        IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects) : ZLinkSpotEvent(SourceName, Timestamp);
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
## 8. Spot monitoring model

```csharp
public enum ZLinkSpotNodeState
{
    Idle = 1,
    Connecting = 2,
    PartialReady = 3,
    Ready = 4,
    Error = 5
}

public enum ZLinkSpotPeerSource
{
    Manual = 1,
    Discovery = 2,
    Mixed = 3
}

public enum ZLinkSpotPeerKind
{
    SpotMesh = 1,
    RouterChannel = 2
}

public enum ZLinkSpotPeerState
{
    Configured = 1,
    Connecting = 2,
    Connected = 3
}

public enum ZLinkSubjectKind : uint
{
    None = 0,
    Topic = 1,
    Pattern = 2
}

public enum ZLinkSpotRole
{
    Pub = 1,
    Sub = 2
}

public sealed record ZLinkSpotNodeStatus(
    string ChannelName,
    string LocalEndpoint,
    RoutingId? NodeRoutingId,
    ZLinkSpotNodeState State,
    uint ConfiguredPeerCount,
    uint ActivePeerCount,
    uint ConnectedPeerCount,
    uint SubjectCount,
    uint ReadySubjectCount,
    int LastError,
    ulong LastChangedMs);

public sealed record ZLinkSpotNodePeerEntry(
    string ChannelName,
    string LocalEndpoint,
    string PeerEndpoint,
    ZLinkSpotPeerSource Source,
    ZLinkSpotPeerKind Kind,
    ZLinkSpotPeerState State,
    uint Weight,
    ulong ConnectedSinceMs,
    ulong LastChangedMs);

public sealed record ZLinkSpotNodeSubjectEntry(
    ZLinkSpotRole Role,
    string Subject,
    ZLinkSubjectKind SubjectKind,
    uint ReadyPeerCount,
    uint ActivePeerCount,
    ulong LastChangedMs);
```
