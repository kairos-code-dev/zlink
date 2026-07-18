// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     The lifecycle state of a MeshNode. Maps to <c>zlink_mesh_node_state_t</c>.
/// </summary>
public enum MeshNodeState
{
    /// <summary>Created but not started.</summary>
    Created = 1,

    /// <summary>Started.</summary>
    Started = 2,

    /// <summary>Some but not all peers are admitted.</summary>
    PartialReady = 3,

    /// <summary>All expected peers are admitted.</summary>
    Ready = 4,

    /// <summary>Draining before stop.</summary>
    Draining = 5,

    /// <summary>Stopped.</summary>
    Stopped = 6,

    /// <summary>In an error state.</summary>
    Error = 7
}

/// <summary>
///     How a mesh peer became known. Maps to <c>zlink_mesh_peer_source_t</c>.
/// </summary>
public enum MeshPeerSource
{
    /// <summary>Added manually.</summary>
    Manual = 1,

    /// <summary>Learned from discovery.</summary>
    Discovery = 2,

    /// <summary>Both manual and discovered.</summary>
    Mixed = 3
}

/// <summary>
///     The connection state of a mesh peer. Maps to
///     <c>zlink_mesh_peer_state_t</c>.
/// </summary>
public enum MeshPeerState
{
    /// <summary>Configured but not connecting.</summary>
    Configured = 1,

    /// <summary>Connecting.</summary>
    Connecting = 2,

    /// <summary>Admitted into the mesh.</summary>
    Admitted = 3,

    /// <summary>Draining.</summary>
    Draining = 4,

    /// <summary>Closed.</summary>
    Closed = 5,

    /// <summary>In an error state.</summary>
    Error = 6
}

/// <summary>
///     Options used when creating a MeshNode.
/// </summary>
public sealed class MeshNodeOptions
{
    /// <summary>Gets or sets the mesh membership name.</summary>
    public string? MeshName { get; set; }

    /// <summary>Gets or sets the trust profile name applied to peers.</summary>
    public string? TrustProfile { get; set; }
}

/// <summary>
///     A snapshot of a MeshNode's status. Maps to
///     <c>zlink_mesh_node_status_t</c>.
/// </summary>
/// <param name="State">The node's lifecycle state.</param>
/// <param name="RoutingId">The node's routing id.</param>
/// <param name="MeshName">The mesh membership name.</param>
/// <param name="LocalEndpoint">The node's local transport endpoint.</param>
/// <param name="LifecycleGeneration">The node's lifecycle generation.</param>
/// <param name="DescriptorRevision">The node's descriptor revision.</param>
/// <param name="ChannelCount">The number of channels.</param>
/// <param name="ConfiguredPeerCount">The number of configured peers.</param>
/// <param name="AdmittedPeerCount">The number of admitted peers.</param>
/// <param name="DrainingPeerCount">The number of draining peers.</param>
/// <param name="PendingApplicationMessages">Queued application messages.</param>
/// <param name="PendingInfrastructureMessages">Queued infrastructure messages.</param>
/// <param name="PendingBytes">Queued bytes.</param>
/// <param name="MulticastSubmitted">Multicast messages submitted.</param>
/// <param name="MulticastDroppedTargets">Multicast targets dropped.</param>
/// <param name="LastError">The last native error code, or 0.</param>
/// <param name="LastChangedMs">When the status last changed, in milliseconds.</param>
public sealed record MeshNodeStatus(
    MeshNodeState State,
    RoutingId RoutingId,
    string MeshName,
    string LocalEndpoint,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    uint ChannelCount,
    uint ConfiguredPeerCount,
    uint AdmittedPeerCount,
    uint DrainingPeerCount,
    ulong PendingApplicationMessages,
    ulong PendingInfrastructureMessages,
    ulong PendingBytes,
    ulong MulticastSubmitted,
    ulong MulticastDroppedTargets,
    int LastError,
    ulong LastChangedMs);

/// <summary>
///     One peer of a MeshNode. Maps to <c>zlink_mesh_peer_entry_t</c>.
/// </summary>
/// <param name="ConnectionIntentId">The connection intent id.</param>
/// <param name="Source">How the peer became known.</param>
/// <param name="State">The peer's connection state.</param>
/// <param name="RoutingId">The peer's routing id.</param>
/// <param name="LifecycleGeneration">The peer's lifecycle generation.</param>
/// <param name="DescriptorRevision">The peer's descriptor revision.</param>
/// <param name="Endpoint">The peer's transport endpoint.</param>
/// <param name="ChannelCount">The number of channels shared with the peer.</param>
/// <param name="LastError">The last native error code, or 0.</param>
/// <param name="LastChangedMs">When the peer last changed, in milliseconds.</param>
public sealed record MeshNodePeer(
    ulong ConnectionIntentId,
    MeshPeerSource Source,
    MeshPeerState State,
    RoutingId RoutingId,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    uint ChannelCount,
    int LastError,
    ulong LastChangedMs);
