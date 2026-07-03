namespace Zlink.Framework.Contracts.Eventing;

public interface IZLinkMonitoringOptions
{
    void AddSocketEvents(
        string sourceName,
        params ZLinkSocketEventKind[] events);

    void AddSpotEvents(
        string sourceName,
        TimeSpan interval);

    /// <summary>Polls the location runtime query surface and publishes
    /// <see cref="ZLinkLocationRuntimeEvent"/> diffs. Requires location
    /// stores to be registered (draft 20.5).</summary>
    void AddLocationRuntimeEvents(
        string sourceName,
        TimeSpan interval);

    /// <summary>Publishes <see cref="ZLinkLocationPeerEvent"/>s when this
    /// runtime writes or removes a peer row and when an auto-connect
    /// desired target set changes. Requires location stores (draft 20.5).</summary>
    void AddLocationPeerEvents(string sourceName);

    /// <summary>Publishes <see cref="ZLinkLocationSpotEvent"/>s when this
    /// runtime writes or removes a spot row and when a spot resolve
    /// misses. Requires location stores (draft 20.5).</summary>
    void AddLocationSpotEvents(string sourceName);

    /// <summary>Publishes <see cref="ZLinkLocationActorEvent"/>s when this
    /// runtime writes or removes an actor row and when an actor resolve
    /// misses. Requires location stores (draft 20.5).</summary>
    void AddLocationActorEvents(string sourceName);

    /// <summary>Publishes <see cref="ZLinkLocationRouteEvent"/>s when this
    /// runtime writes or removes a route row and when a route resolve
    /// misses. Requires location stores (draft 20.5).</summary>
    void AddLocationRouteEvents(string sourceName);
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
    Internal = 6
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

public enum ZLinkLocationRuntimeEventKind
{
    StatusChanged = 0,
    TopologyChanged = 1,
    ServiceSummaryChanged = 2,
    StoreUnavailable = 3,
    StoreRecovered = 4
}

public readonly record struct ZLinkLocationRuntimeEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkLocationRuntimeEventKind Event,
    ZLinkLocationRuntimeStatus? Status,
    IReadOnlyList<ZLinkLocationTopologyEntry>? Topology,
    IReadOnlyList<ZLinkLocationServiceSummary>? ServiceSummary) : IZLinkRuntimeEvent;

public enum ZLinkLocationPeerEventKind
{
    RowUpdated = 0,
    RowRemoved = 1,
    DesiredSetChanged = 2
}

/// <summary>Connect/disconnect diff one reconcile tick applied to an
/// auto-connect desired target set.</summary>
public readonly record struct ZLinkAutoConnectDesiredSetChange(
    ZLinkLocationAutoConnectType AutoConnectType,
    string MeshName,
    IReadOnlyList<string> ConnectedEndpoints,
    IReadOnlyList<string> DisconnectedEndpoints);

public readonly record struct ZLinkLocationPeerEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkLocationPeerEventKind Event,
    ZLinkPeerLocationKey? Key,
    ZLinkPeerLocation? Peer,
    ZLinkAutoConnectDesiredSetChange? DesiredSetChange) : IZLinkRuntimeEvent;

public enum ZLinkLocationSpotEventKind
{
    RowUpdated = 0,
    RowRemoved = 1,
    ResolveMiss = 2
}

public readonly record struct ZLinkLocationSpotEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkLocationSpotEventKind Event,
    ZLinkSpotLocationKey Key,
    ZLinkSpotLocation? Spot) : IZLinkRuntimeEvent;

public enum ZLinkLocationActorEventKind
{
    RowUpdated = 0,
    RowRemoved = 1,
    ResolveMiss = 2
}

public readonly record struct ZLinkLocationActorEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkLocationActorEventKind Event,
    ZLinkActorLocationKey Key,
    ZLinkActorLocation? Actor) : IZLinkRuntimeEvent;

public enum ZLinkLocationRouteEventKind
{
    RowUpdated = 0,
    RowRemoved = 1,
    ResolveMiss = 2
}

public readonly record struct ZLinkLocationRouteEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkLocationRouteEventKind Event,
    ZLinkRouteLocationKey Key,
    ZLinkRouteLocation? Route) : IZLinkRuntimeEvent;

public enum ZLinkSpotEventKind
{
    StatusChanged = 0,
    PeersChanged = 1,
    SubjectsChanged = 2,
    TimerHandlerFailed = 3,
    TimerStoppedAfterUnhandledException = 4
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
