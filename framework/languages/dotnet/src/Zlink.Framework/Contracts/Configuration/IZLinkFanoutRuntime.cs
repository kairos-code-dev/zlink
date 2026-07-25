using Zlink.Framework.Contracts.Locations;

namespace Zlink.Framework.Contracts.Configuration;

public enum ZLinkFanoutPublisherConnectionState
{
    Connecting = 0,
    Ready = 1,
    Disconnected = 2,
    Reconnecting = 3,
    ExcludedDraining = 4,
    ExcludedStale = 5
}

public sealed record ZLinkFanoutPublisherConnectionSnapshot(
    RoutingId PublisherRid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    bool ConnectionIntent,
    bool Ready,
    ZLinkFanoutPublisherConnectionState State,
    string? LastFailure);

public sealed record ZLinkFanoutChannelSnapshot(
    string ChannelName,
    int ConnectionIntentCount,
    int ReadyConnectionCount,
    ulong Sequence,
    DateTimeOffset ObservedAt,
    IReadOnlyList<ZLinkFanoutPublisherConnectionSnapshot> Publishers,
    ZLinkLocationRuntimeSnapshot Location);

public abstract record ZLinkFanoutRuntimeEvent
{
    private protected ZLinkFanoutRuntimeEvent(
        string identifier,
        ulong sequence,
        DateTimeOffset timestamp,
        string channelName)
    {
        Identifier = identifier;
        Sequence = sequence;
        Timestamp = timestamp;
        ChannelName = channelName;
    }

    public string Identifier { get; }
    public ulong Sequence { get; }
    public DateTimeOffset Timestamp { get; }
    public string ChannelName { get; }

    public sealed record PublisherChanged(
        ulong Sequence,
        DateTimeOffset Timestamp,
        string ChannelName,
        ZLinkFanoutPublisherConnectionSnapshot Entry)
        : ZLinkFanoutRuntimeEvent(
            "zlink.runtime.fanout.publisher_changed",
            Sequence,
            Timestamp,
            ChannelName);

    public sealed record LocationChanged(
        ulong Sequence,
        DateTimeOffset Timestamp,
        string ChannelName,
        ZLinkLocationRuntimeSnapshot Location)
        : ZLinkFanoutRuntimeEvent(
            "zlink.runtime.location.store_changed",
            Sequence,
            Timestamp,
            ChannelName);
}

public interface IZLinkFanoutRuntime
{
    ZLinkFanoutChannelSnapshot Snapshot(string channelName);

    IAsyncEnumerable<ZLinkFanoutRuntimeEvent> ObserveAsync(
        string channelName,
        int capacity = 1024,
        CancellationToken cancellationToken = default);
}
