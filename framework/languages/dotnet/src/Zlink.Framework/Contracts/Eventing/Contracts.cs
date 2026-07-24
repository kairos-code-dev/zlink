namespace Zlink.Framework.Contracts.Eventing;

public interface IZLinkMonitoringOptions
{
    void AddSocketEvents(
        string sourceName,
        params ZLinkSocketEventKind[] events);

    /// <summary>Observes the registered RouteMesh's runtime events (spec 50:
    /// state/peer transitions) and publishes them as
    /// <see cref="ZLinkMeshRuntimeEvent"/> runtime events.</summary>
    void AddMeshNodeEvents(string meshName);

    void AddSpotEvents(
        string sourceName,
        TimeSpan interval);

    /// <summary>Polls the location runtime query surface and publishes
    /// <see cref="ZLinkLocationRuntimeEvent"/> diffs. Requires location
    /// stores to be registered.</summary>
    void AddLocationRuntimeEvents(
        string sourceName,
        TimeSpan interval);

    /// <summary>Publishes <see cref="ZLinkLocationPeerEvent"/>s when this
    /// runtime writes or removes a peer row and when an auto-connect
    /// desired target set changes. Requires location stores.</summary>
    void AddLocationPeerEvents(string sourceName);

    /// <summary>Publishes <see cref="ZLinkLocationSpotEvent"/>s when this
    /// runtime writes or removes a spot row and when a spot resolve
    /// misses. Requires location stores.</summary>
    void AddLocationSpotEvents(string sourceName);

    /// <summary>Publishes <see cref="ZLinkLocationActorEvent"/>s when this
    /// runtime writes or removes an actor row and when an actor resolve
    /// misses. Requires location stores.</summary>
    void AddLocationActorEvents(string sourceName);
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

    public sealed record StoreFailure(string SourceName, DateTimeOffset Timestamp)
        : ZLinkLocationRuntimeEvent(SourceName, Timestamp);

    public sealed record StoreRecovered(string SourceName, DateTimeOffset Timestamp)
        : ZLinkLocationRuntimeEvent(SourceName, Timestamp);
}

/// <summary>Connect/disconnect diff one reconcile tick applied to an
/// auto-connect desired target set.</summary>
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
