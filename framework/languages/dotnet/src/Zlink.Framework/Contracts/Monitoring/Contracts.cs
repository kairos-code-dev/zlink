namespace Zlink.Framework.Contracts.Monitoring;

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

public interface IZLinkRuntimeEventPublisher
{
    ValueTask PublishAsync<TEvent>(
        TEvent @event,
        CancellationToken cancellationToken)
        where TEvent : IZLinkRuntimeEvent;
}

public enum ZLinkSocketEventKind
{
    Connected = 0,
    ConnectionReady = 1,
    Disconnected = 2,
    HandshakeFailed = 3,
    PeerAdmissionChanged = 4,
    Closed = 5,
    Internal = 6,
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
    PeerAdmissionChanged = 0x8000,
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

public enum ZLinkRegistryEventKind
{
    StatusChanged = 0,
    TopologyChanged = 1,
    ServiceSummaryChanged = 2,
}

public readonly record struct ZLinkRegistryEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkRegistryEventKind Event,
    ZLinkRegistryStatus? Status,
    IReadOnlyList<ZLinkRegistryTopologyEntry>? Topology,
    IReadOnlyList<ZLinkRegistryServiceSummaryEntry>? ServiceSummary) : IZLinkRuntimeEvent;

public enum ZLinkSpotEventKind
{
    StatusChanged = 0,
    PeersChanged = 1,
    SubjectsChanged = 2,
    TimerHandlerFailed = 3,
    TimerStoppedAfterUnhandledException = 4,
}

public readonly record struct ZLinkSpotTimerDiagnostic(
    RoutingId SpotRid,
    bool IsEntrySpot,
    string TimerName,
    string HandlerType,
    ulong DeliveryIndex,
    ulong ScheduledIndex,
    string ExceptionType,
    string ExceptionMessage);

public readonly record struct ZLinkSpotEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkSpotEventKind Event,
    ZLinkSpotNodeStatus? Status,
    IReadOnlyList<ZLinkSpotNodePeerEntry>? Peers,
    IReadOnlyList<ZLinkSpotNodeSubjectEntry>? Subjects,
    ZLinkSpotTimerDiagnostic? TimerDiagnostic = null) : IZLinkRuntimeEvent;
