using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;

namespace Zlink.Framework.Contracts.Configuration;

public enum ZLinkMeshNodeState
{
    Starting = 0,
    Serving = 1,
    Draining = 2,
    Drained = 3,
    ForceStopping = 4,
    Stopped = 5,
    Faulted = 6
}

public sealed record ZLinkMeshPeerSnapshot(
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    string AdmissionState,
    bool Ready,
    string DrainState,
    IReadOnlyList<string> ChannelNames,
    string? LastFailure);

public sealed record ZLinkMeshChannelSnapshot(
    string ChannelName,
    int LocalWeight,
    int ReadyMemberCount,
    bool Selectable);

public sealed record ZLinkMeshClaimSnapshot(
    bool ApplicationActive,
    ulong PendingApplicationWork,
    bool InfrastructureActive,
    ulong PendingInfrastructureWork);

public sealed record ZLinkLocationRuntimeSnapshot(
    string State,
    DateTimeOffset? LastSuccessAt,
    DateTimeOffset? LastFailureAt);

public sealed record ZLinkInstanceSpotTypeSnapshot(
    string InstanceSpotType,
    ulong ActiveCount,
    ulong ActivatingCount,
    ulong ClosingCount,
    ulong PendingMessageCount,
    ulong PendingByteCount,
    string? LastActivationOutcome);

public sealed record ZLinkMeshNodeSnapshot(
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
    public long ApplicationVersion { get; init; }
    public ZLinkMeshNodeObjectRole ObjectRole { get; init; }
    public int PlacementWeight { get; init; } = 100;
    public ZLinkPlacementCapacity PopulationCapacity { get; init; }
        = new(
            new ZLinkPopulationCapacity(0, 0, 0),
            new ZLinkPopulationCapacity(0, 0, 0),
            Array.Empty<ZLinkSpotTypeCapacity>());
    public ZLinkActivationConcurrency ActivationConcurrency { get; init; }
        = new(0, 128);
    public ulong PlacementReservationFailureCount { get; init; }
    public string? LastPlacementReservationFailure { get; init; }
    public IReadOnlyList<ZLinkObjectCapability> ObjectCapabilities { get; init; }
        = Array.Empty<ZLinkObjectCapability>();
    public IReadOnlyList<ZLinkInstanceSpotTypeSnapshot> InstanceSpots { get; init; }
        = Array.Empty<ZLinkInstanceSpotTypeSnapshot>();
}

public sealed record ZLinkMeshRuntimeEvent(
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
    ZLinkMeshNodeState? State) : Zlink.Framework.Contracts.Eventing.IZLinkRuntimeEvent
{
    /// <summary>The runtime event source is the observed mesh.</summary>
    public string SourceName => MeshName;
}

/// <summary>
/// Runtime monitoring service for registered RouteMesh nodes (spec 50):
/// one consistent MeshNode snapshot per MeshName and an ordered event stream.
/// Host termination is owned exclusively by <see cref="IZLinkFrameworkRuntime"/>.
/// </summary>
public interface IZLinkRouteMeshRuntime
{
    ZLinkMeshNodeSnapshot Snapshot(string meshName);

    IAsyncEnumerable<ZLinkMeshRuntimeEvent> ObserveAsync(
        string meshName,
        int capacity = 1024,
        CancellationToken cancellationToken = default);

    bool IsReady(string meshName);
}

public enum ZLinkClientServerRole
{
    Client = 1,
    Server = 2,
    ClientAndServer = 3
}

public enum ZLinkClientServerServerState
{
    Configured = 0,
    Connecting = 1,
    Ready = 2,
    Draining = 3,
    Disconnected = 4,
    Rejected = 5
}

public sealed record ZLinkClientServerServerSnapshot(
    RoutingId ServerRid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    int Weight,
    bool Ready,
    ZLinkClientServerServerState State,
    string DescriptorSource,
    string? LastFailure);

public sealed record ZLinkClientServerChannelSnapshot(
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

public sealed record ZLinkClientServerRuntimeEvent(
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

public interface IZLinkClientServerRuntime
{
    ZLinkClientServerChannelSnapshot Snapshot(string channelName);

    IAsyncEnumerable<ZLinkClientServerRuntimeEvent> ObserveAsync(
        string channelName,
        int capacity = 1024,
        CancellationToken cancellationToken = default);

    bool IsReady(string channelName);
}
