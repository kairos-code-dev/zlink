using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;

namespace Zlink.Framework.Contracts.Configuration;

internal enum ZLinkMeshNodeState
{
    Starting = 0,
    Serving = 1,
    Draining = 2,
    Drained = 3,
    ForceStopping = 4,
    Stopped = 5,
    Faulted = 6
}

internal sealed record ZLinkMeshPeerSnapshot(
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    string AdmissionState,
    bool Ready,
    string DrainState,
    IReadOnlyList<string> ChannelNames,
    string? LastFailure);

internal sealed record ZLinkMeshChannelSnapshot(
    string ChannelName,
    int LocalWeight,
    int ReadyMemberCount,
    bool Selectable);

internal sealed record ZLinkMeshClaimSnapshot(
    bool ApplicationActive,
    ulong PendingApplicationWork,
    bool InfrastructureActive,
    ulong PendingInfrastructureWork);

internal sealed record ZLinkLocationRuntimeSnapshot(
    string State,
    DateTimeOffset? LastSuccessAt,
    DateTimeOffset? LastFailureAt);

internal sealed record ZLinkInstanceSpotTypeSnapshot(
    string InstanceSpotType,
    ulong ActiveCount,
    ulong ActivatingCount,
    ulong ClosingCount,
    ulong PendingMessageCount,
    ulong PendingByteCount,
    string? LastActivationOutcome);

internal sealed record ZLinkMeshNodeSnapshot(
    string MeshName,
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    ZLinkMeshNodeState State,
    ulong Sequence,
    DateTimeOffset ObservedAt,
    IReadOnlyList<string> DescriptorSources,
    IReadOnlyList<ZLinkMeshPeerSnapshot> Peers,
    IReadOnlyList<ZLinkMeshChannelSnapshot> Channels,
    ZLinkMeshClaimSnapshot Claims,
    ZLinkLocationRuntimeSnapshot Location)
{
    internal long ApplicationVersion { get; init; }
    internal ZLinkMeshNodeObjectRole ObjectRole { get; init; }
    internal int PlacementWeight { get; init; } = 100;
    internal ZLinkPlacementCapacity PopulationCapacity { get; init; }
        = new(
            new ZLinkPopulationCapacity(0, 0, 0),
            new ZLinkPopulationCapacity(0, 0, 0),
            Array.Empty<ZLinkSpotTypeCapacity>());
    internal ZLinkActivationConcurrency ActivationConcurrency { get; init; }
        = new(0, 128);
    internal ulong PlacementReservationFailureCount { get; init; }
    internal string? LastPlacementReservationFailure { get; init; }
    internal IReadOnlyList<ZLinkObjectCapability> ObjectCapabilities { get; init; }
        = Array.Empty<ZLinkObjectCapability>();
    internal IReadOnlyList<ZLinkInstanceSpotTypeSnapshot> InstanceSpots { get; init; }
        = Array.Empty<ZLinkInstanceSpotTypeSnapshot>();
}

internal sealed record ZLinkMeshRuntimeEvent(
    string Identifier,
    ulong Sequence,
    DateTimeOffset Timestamp,
    string MeshName,
    RoutingId SourceRid,
    RoutingId? PeerRid,
    ulong? LifecycleGeneration,
    ulong? DescriptorRevision,
    string? ChannelName,
    string? ClaimDomain,
    string? MessageKind,
    string? PlacementOutcome,
    ZLinkCapacityVector? Capacity,
    ZLinkPlacementCapacity? PopulationCapacity,
    ZLinkActivationConcurrency? ActivationConcurrency,
    string? Reason,
    ZLinkMeshNodeState? State);

internal enum ZLinkClientServerServerState
{
    Configured = 0,
    Connecting = 1,
    Ready = 2,
    Draining = 3,
    Disconnected = 4,
    Rejected = 5
}

internal sealed record ZLinkClientServerServerSnapshot(
    RoutingId ServerRid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    int Weight,
    bool Ready,
    ZLinkClientServerServerState State,
    string DescriptorSource,
    string? LastFailure);

internal sealed record ZLinkClientServerChannelSnapshot(
    string ChannelName,
    ZLinkClientServerRole LocalRole,
    bool Selectable,
    int ReadyServerCount,
    int ConnectionIntentCount,
    int PendingRequestCount,
    ulong Sequence,
    DateTimeOffset ObservedAt,
    IReadOnlyList<ZLinkClientServerServerSnapshot> Servers,
    ZLinkLocationRuntimeSnapshot Location);

internal sealed record ZLinkClientServerRuntimeEvent(
    string Identifier,
    ulong Sequence,
    DateTimeOffset Timestamp,
    string ChannelName,
    RoutingId? ServerRid,
    ulong? LifecycleGeneration,
    ulong? DescriptorRevision,
    int? Weight,
    bool? Ready,
    ZLinkClientServerServerState? State,
    string? Reason);

internal enum ZLinkFanoutPublisherConnectionState
{
    Connecting = 0,
    Ready = 1,
    Disconnected = 2,
    Reconnecting = 3,
    ExcludedDraining = 4,
    ExcludedStale = 5
}

internal sealed record ZLinkFanoutPublisherConnectionSnapshot(
    RoutingId PublisherRid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    bool ConnectionIntent,
    bool Ready,
    ZLinkFanoutPublisherConnectionState State,
    string? LastFailure);

internal sealed record ZLinkFanoutChannelSnapshot(
    string ChannelName,
    int ConnectionIntentCount,
    int ReadyConnectionCount,
    ulong Sequence,
    DateTimeOffset ObservedAt,
    IReadOnlyList<ZLinkFanoutPublisherConnectionSnapshot> Publishers,
    ZLinkLocationRuntimeSnapshot Location);

internal abstract record ZLinkFanoutRuntimeEvent
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

    internal string Identifier { get; }
    internal ulong Sequence { get; }
    internal DateTimeOffset Timestamp { get; }
    internal string ChannelName { get; }

    internal sealed record PublisherChanged(
        ulong Sequence,
        DateTimeOffset Timestamp,
        string ChannelName,
        ZLinkFanoutPublisherConnectionSnapshot Entry)
        : ZLinkFanoutRuntimeEvent(
            "zlink.runtime.fanout.publisher_changed",
            Sequence,
            Timestamp,
            ChannelName);

    internal sealed record LocationChanged(
        ulong Sequence,
        DateTimeOffset Timestamp,
        string ChannelName,
        ZLinkLocationRuntimeSnapshot Location)
        : ZLinkFanoutRuntimeEvent(
            "zlink.runtime.location.store_changed",
            Sequence,
            Timestamp,
            ChannelName);

    internal sealed record RuntimeChanged(
        ulong Sequence,
        DateTimeOffset Timestamp,
        string ChannelName)
        : ZLinkFanoutRuntimeEvent(
            "zlink.runtime.framework.state_changed",
            Sequence,
            Timestamp,
            ChannelName);
}
