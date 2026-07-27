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

    /// <summary>Polls the location runtime query surface and publishes
    /// <see cref="ZLinkLocationRuntimeEvent"/> diffs. Requires location
    /// stores to be registered.</summary>
    void AddLocationRuntimeEvents(
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

public enum ZLinkSocketEventKind
{
    Connected = 0,
    ConnectionReady = 1,
    Disconnected = 2,
    HandshakeFailed = 3,
    PeerAdmissionChanged = 4,
    Closed = 5
}

public readonly record struct ZLinkSocketEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkSocketEventKind Event,
    RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr) : IZLinkRuntimeEvent;

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
