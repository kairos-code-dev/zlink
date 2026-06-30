// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     The overall readiness state of a spot node.
/// </summary>
public enum SpotNodeState
{
    /// <summary>
    ///     Not yet connecting to any peer.
    /// </summary>
    Idle = 1,

    /// <summary>
    ///     Establishing connections to peers.
    /// </summary>
    Connecting = 2,

    /// <summary>
    ///     Some but not all peers are connected.
    /// </summary>
    PartialReady = 3,

    /// <summary>
    ///     All expected peers are connected.
    /// </summary>
    Ready = 4,

    /// <summary>
    ///     The node is in an error state.
    /// </summary>
    Error = 5
}

/// <summary>
///     How a spot peer became known to the node.
/// </summary>
public enum SpotPeerSource
{
    /// <summary>
    ///     Added manually by the application.
    /// </summary>
    Manual = 1,

    /// <summary>
    ///     Learned from a discovery service.
    /// </summary>
    Discovery = 2,

    /// <summary>
    ///     Both manually added and discovered.
    /// </summary>
    Mixed = 3
}

/// <summary>
///     The connection style of a spot peer.
/// </summary>
public enum SpotPeerKind
{
    /// <summary>
    ///     A peer in the spot mesh.
    /// </summary>
    SpotMesh = 1,

    /// <summary>
    ///     A peer reached over a router channel.
    /// </summary>
    RouterChannel = 2
}

/// <summary>
///     The connection state of a spot peer.
/// </summary>
public enum SpotPeerState
{
    /// <summary>
    ///     Configured but not yet connecting.
    /// </summary>
    Configured = 1,

    /// <summary>
    ///     A connection is being established.
    /// </summary>
    Connecting = 2,

    /// <summary>
    ///     The peer is connected.
    /// </summary>
    Connected = 3
}

/// <summary>
///     The kind of a spot.
/// </summary>
public enum SpotKind
{
    /// <summary>
    ///     No spot kind (unset).
    /// </summary>
    Invalid = 0,

    /// <summary>
    ///     An entry spot (a node's well-known entry point).
    /// </summary>
    Entry = 1,

    /// <summary>
    ///     A user-created spot.
    /// </summary>
    User = 2
}

/// <summary>
///     How a subscription subject is matched.
/// </summary>
public enum SubjectKind
{
    /// <summary>
    ///     No subject.
    /// </summary>
    None = 0,

    /// <summary>
    ///     An exact topic match.
    /// </summary>
    Topic = 1,

    /// <summary>
    ///     A pattern match.
    /// </summary>
    Pattern = 2
}

/// <summary>
///     The pub/sub role of a spot subject.
/// </summary>
public enum SpotRole
{
    /// <summary>
    ///     A publisher subject.
    /// </summary>
    Pub = 1,

    /// <summary>
    ///     A subscriber subject.
    /// </summary>
    Sub = 2
}

/// <summary>
///     Filter for a spot node peer query; null fields match anything.
/// </summary>
/// <param name="PeerEndpoint">Restrict to this peer endpoint.</param>
/// <param name="Source">Restrict to peers from this source.</param>
/// <param name="State">Restrict to peers in this state.</param>
public sealed record SpotNodePeerFilter(
    string? PeerEndpoint = null,
    SpotPeerSource? Source = null,
    SpotPeerState? State = null);

/// <summary>
///     Filter for a spot node subject query; null fields match anything.
/// </summary>
/// <param name="Role">Restrict to subjects with this pub/sub role.</param>
/// <param name="Subject">Restrict to this subject topic or pattern.</param>
/// <param name="SubjectKind">Restrict to this subject match kind.</param>
public sealed record SpotNodeSubjectFilter(
    SpotRole? Role = null,
    string? Subject = null,
    SubjectKind? SubjectKind = null);

/// <summary>
///     A snapshot of a spot node's status and peer/subject counts.
/// </summary>
/// <param name="ChannelName">The logical channel name.</param>
/// <param name="LocalEndpoint">The node's local transport endpoint.</param>
/// <param name="NodeRoutingId">The node's routing id, when assigned.</param>
/// <param name="State">The node's overall readiness state.</param>
/// <param name="ConfiguredPeerCount">The number of configured peers.</param>
/// <param name="ActivePeerCount">The number of active peers.</param>
/// <param name="ConnectedPeerCount">The number of connected peers.</param>
/// <param name="SubjectCount">The total number of subjects.</param>
/// <param name="ReadySubjectCount">The number of ready subjects.</param>
/// <param name="DisconnectedSubTargetCount">The number of disconnected subscription targets.</param>
/// <param name="DisconnectedRoutedTargetCount">The number of disconnected routed targets.</param>
/// <param name="LastError">The last native error code, or 0 for none.</param>
/// <param name="LastChangedMs">When the status last changed, in milliseconds.</param>
public sealed record SpotNodeStatus(
    string ChannelName,
    string LocalEndpoint,
    RoutingId? NodeRoutingId,
    SpotNodeState State,
    uint ConfiguredPeerCount,
    uint ActivePeerCount,
    uint ConnectedPeerCount,
    uint SubjectCount,
    uint ReadySubjectCount,
    uint DisconnectedSubTargetCount,
    uint DisconnectedRoutedTargetCount,
    int LastError,
    ulong LastChangedMs);

/// <summary>
///     One peer of a spot node and its connection details.
/// </summary>
/// <param name="ChannelName">The logical channel name.</param>
/// <param name="LocalEndpoint">The node's local transport endpoint.</param>
/// <param name="PeerEndpoint">The peer's transport endpoint.</param>
/// <param name="Source">How the peer became known.</param>
/// <param name="Kind">The peer's connection style.</param>
/// <param name="State">The peer's connection state.</param>
/// <param name="Weight">The peer's load-balancing weight.</param>
/// <param name="ConnectedSinceMs">When the peer connected, in milliseconds.</param>
/// <param name="LastChangedMs">When the peer last changed state, in milliseconds.</param>
public sealed record SpotNodePeerEntry(
    string ChannelName,
    string LocalEndpoint,
    string PeerEndpoint,
    SpotPeerSource Source,
    SpotPeerKind Kind,
    SpotPeerState State,
    uint Weight,
    ulong ConnectedSinceMs,
    ulong LastChangedMs);

/// <summary>
///     One subject (topic or pattern) served by a spot node.
/// </summary>
/// <param name="Role">The subject's pub/sub role.</param>
/// <param name="Subject">The subject topic or pattern.</param>
/// <param name="SubjectKind">How the subject is matched.</param>
/// <param name="ReadyPeerCount">The number of peers ready on this subject.</param>
/// <param name="ActivePeerCount">The number of peers active on this subject.</param>
/// <param name="LastChangedMs">When the subject last changed, in milliseconds.</param>
public sealed record SpotNodeSubjectEntry(
    SpotRole Role,
    string Subject,
    SubjectKind SubjectKind,
    uint ReadyPeerCount,
    uint ActivePeerCount,
    ulong LastChangedMs);

/// <summary>
///     Which messaging patterns a spot node enables.
/// </summary>
public enum SpotNodeMode
{
    /// <summary>
    ///     Publish/subscribe only.
    /// </summary>
    PubSub = 1,

    /// <summary>
    ///     Routed request/reply only.
    /// </summary>
    Routed = 2,

    /// <summary>
    ///     Both pub/sub and routed.
    /// </summary>
    All = 3
}

/// <summary>
///     Which component owns a spot node socket.
/// </summary>
public enum SpotNodeSocketOwner
{
    /// <summary>
    ///     Any owner (no filter).
    /// </summary>
    Any = 0,

    /// <summary>
    ///     Owned by the node itself.
    /// </summary>
    Node = 1,

    /// <summary>
    ///     Owned by a spot.
    /// </summary>
    Spot = 2
}

/// <summary>
///     The socket type of a spot node socket; mirrors <see cref="SocketType" />.
/// </summary>
public enum SpotNodeSocketType
{
    /// <summary>
    ///     Any socket type (no filter).
    /// </summary>
    Any = 0,

    /// <summary>
    ///     A PAIR socket.
    /// </summary>
    Pair = 0x1001,

    /// <summary>
    ///     A PUB socket.
    /// </summary>
    Pub = 0x1002,

    /// <summary>
    ///     A SUB socket.
    /// </summary>
    Sub = 0x1003,

    /// <summary>
    ///     A DEALER socket.
    /// </summary>
    Dealer = 0x1004,

    /// <summary>
    ///     A ROUTER socket.
    /// </summary>
    Router = 0x1005,

    /// <summary>
    ///     An XPUB socket.
    /// </summary>
    XPub = 0x1006,

    /// <summary>
    ///     An XSUB socket.
    /// </summary>
    XSub = 0x1007,

    /// <summary>
    ///     A STREAM socket.
    /// </summary>
    Stream = 0x1008
}

/// <summary>
///     Filter for a spot node socket query; null fields match anything.
/// </summary>
/// <param name="Owner">Restrict to sockets owned by this component.</param>
/// <param name="SocketType">Restrict to this socket type.</param>
/// <param name="SocketName">Restrict to this socket name.</param>
public sealed record SpotNodeSocketFilter(
    SpotNodeSocketOwner? Owner = null,
    SpotNodeSocketType? SocketType = null,
    string? SocketName = null);

/// <summary>
///     One socket owned within a spot node and its monitored status.
/// </summary>
/// <param name="Owner">Which component owns the socket.</param>
/// <param name="OwnerId">The identifier of the owning component.</param>
/// <param name="OwnerName">The name of the owning component.</param>
/// <param name="SocketName">The socket's name.</param>
/// <param name="SocketType">The socket's type.</param>
/// <param name="AutoHwmVisible">Whether the socket participates in automatic high-water-mark sizing.</param>
/// <param name="MonitorStatus">The socket's monitored status.</param>
public sealed record SpotNodeSocketEntry(
    SpotNodeSocketOwner Owner,
    ulong OwnerId,
    string OwnerName,
    string SocketName,
    SpotNodeSocketType SocketType,
    bool AutoHwmVisible,
    MonitorStatus MonitorStatus);

/// <summary>
///     One spot hosted on a spot node and its actor counts.
/// </summary>
/// <param name="SpotRid">The spot's routing id, when assigned.</param>
/// <param name="SpotKind">The kind of spot.</param>
/// <param name="DispatchHandlerAttached">Whether a dispatch handler is attached to the spot.</param>
/// <param name="JoinedActorCount">The number of actors currently joined.</param>
/// <param name="PendingActorJoinCount">The number of pending actor joins.</param>
/// <param name="RouteSynced">Whether the spot's route is synced across peers.</param>
/// <param name="LastChangedMs">When the spot last changed, in milliseconds.</param>
public sealed record SpotNodeSpotEntry(
    RoutingId? SpotRid,
    SpotKind SpotKind,
    bool DispatchHandlerAttached,
    uint JoinedActorCount,
    uint PendingActorJoinCount,
    bool RouteSynced,
    ulong LastChangedMs);

/// <summary>
///     One actor hosted on a spot node and its current placement.
/// </summary>
/// <param name="Actor">The actor.</param>
/// <param name="CurrentSpotRid">The routing id of the spot the actor is currently on.</param>
/// <param name="CurrentSpotKind">The kind of the current spot.</param>
/// <param name="RouteSynced">Whether the actor's route is synced across peers.</param>
/// <param name="PendingMessageCount">The number of messages queued for the actor.</param>
/// <param name="LastChangedMs">When the actor last changed, in milliseconds.</param>
public sealed record SpotNodeActorEntry(
    ActorRef Actor,
    RoutingId CurrentSpotRid,
    SpotKind CurrentSpotKind,
    bool RouteSynced,
    uint PendingMessageCount,
    ulong LastChangedMs);