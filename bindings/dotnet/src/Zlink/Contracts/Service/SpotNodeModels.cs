// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// Defines spot node state values.
/// </summary>
public enum SpotNodeState
{
    /// <summary>
    /// Indicates the idle spot node state.
    /// </summary>
    Idle = 1,
    /// <summary>
    /// Indicates the connecting spot node state.
    /// </summary>
    Connecting = 2,
    /// <summary>
    /// Indicates the partial ready spot node state.
    /// </summary>
    PartialReady = 3,
    /// <summary>
    /// Indicates the ready spot node state.
    /// </summary>
    Ready = 4,
    /// <summary>
    /// Indicates the error spot node state.
    /// </summary>
    Error = 5
}

/// <summary>
/// Defines spot peer source values.
/// </summary>
public enum SpotPeerSource
{
    /// <summary>
    /// Indicates the manual spot peer source.
    /// </summary>
    Manual = 1,
    /// <summary>
    /// Indicates the discovery spot peer source.
    /// </summary>
    Discovery = 2,
    /// <summary>
    /// Indicates the mixed spot peer source.
    /// </summary>
    Mixed = 3
}

/// <summary>
/// Defines spot peer kind values.
/// </summary>
public enum SpotPeerKind
{
    /// <summary>
    /// Indicates the spot mesh spot peer kind.
    /// </summary>
    SpotMesh = 1,
    /// <summary>
    /// Indicates the router channel spot peer kind.
    /// </summary>
    RouterChannel = 2
}

/// <summary>
/// Defines spot peer state values.
/// </summary>
public enum SpotPeerState
{
    /// <summary>
    /// Indicates the configured spot peer state.
    /// </summary>
    Configured = 1,
    /// <summary>
    /// Indicates the connecting spot peer state.
    /// </summary>
    Connecting = 2,
    /// <summary>
    /// Indicates the connected spot peer state.
    /// </summary>
    Connected = 3
}

/// <summary>
/// Defines spot kind values.
/// </summary>
public enum SpotKind
{
    /// <summary>
    /// Indicates the invalid spot kind.
    /// </summary>
    Invalid = 0,
    /// <summary>
    /// Indicates the entry spot kind.
    /// </summary>
    Entry = 1,
    /// <summary>
    /// Indicates the user spot kind.
    /// </summary>
    User = 2
}

/// <summary>
/// Defines subject kind values.
/// </summary>
public enum SubjectKind
{
    /// <summary>
    /// Indicates the none subject kind.
    /// </summary>
    None = 0,
    /// <summary>
    /// Indicates the topic subject kind.
    /// </summary>
    Topic = 1,
    /// <summary>
    /// Indicates the pattern subject kind.
    /// </summary>
    Pattern = 2
}

/// <summary>
/// Defines spot role values.
/// </summary>
public enum SpotRole
{
    /// <summary>
    /// Indicates the pub spot role.
    /// </summary>
    Pub = 1,
    /// <summary>
    /// Indicates the sub spot role.
    /// </summary>
    Sub = 2
}

/// <summary>
/// Describes spot node peer filter data.
/// </summary>
/// <param name="PeerEndpoint">The peer endpoint value.</param>
/// <param name="Source">The source value.</param>
/// <param name="State">The state value.</param>
public sealed record SpotNodePeerFilter(
    string? PeerEndpoint = null,
    SpotPeerSource? Source = null,
    SpotPeerState? State = null);

/// <summary>
/// Describes spot node subject filter data.
/// </summary>
/// <param name="Role">The role value.</param>
/// <param name="Subject">The subject value.</param>
/// <param name="SubjectKind">The subject kind value.</param>
public sealed record SpotNodeSubjectFilter(
    SpotRole? Role = null,
    string? Subject = null,
    SubjectKind? SubjectKind = null);

/// <summary>
/// Describes spot node status data.
/// </summary>
/// <param name="ChannelName">The channel name value.</param>
/// <param name="LocalEndpoint">The local endpoint value.</param>
/// <param name="NodeRoutingId">The node routing id value.</param>
/// <param name="State">The state value.</param>
/// <param name="ConfiguredPeerCount">The configured peer count value.</param>
/// <param name="ActivePeerCount">The active peer count value.</param>
/// <param name="ConnectedPeerCount">The connected peer count value.</param>
/// <param name="SubjectCount">The subject count value.</param>
/// <param name="ReadySubjectCount">The ready subject count value.</param>
/// <param name="DisconnectedSubTargetCount">The disconnected sub target count value.</param>
/// <param name="DisconnectedRoutedTargetCount">The disconnected routed target count value.</param>
/// <param name="LastError">The last error value.</param>
/// <param name="LastChangedMs">The last changed milliseconds value.</param>
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
/// Describes spot node peer entry data.
/// </summary>
/// <param name="ChannelName">The channel name value.</param>
/// <param name="LocalEndpoint">The local endpoint value.</param>
/// <param name="PeerEndpoint">The peer endpoint value.</param>
/// <param name="Source">The source value.</param>
/// <param name="Kind">The kind value.</param>
/// <param name="State">The state value.</param>
/// <param name="Weight">The weight value.</param>
/// <param name="ConnectedSinceMs">The connected since milliseconds value.</param>
/// <param name="LastChangedMs">The last changed milliseconds value.</param>
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
/// Describes spot node subject entry data.
/// </summary>
/// <param name="Role">The role value.</param>
/// <param name="Subject">The subject value.</param>
/// <param name="SubjectKind">The subject kind value.</param>
/// <param name="ReadyPeerCount">The ready peer count value.</param>
/// <param name="ActivePeerCount">The active peer count value.</param>
/// <param name="LastChangedMs">The last changed milliseconds value.</param>
public sealed record SpotNodeSubjectEntry(
    SpotRole Role,
    string Subject,
    SubjectKind SubjectKind,
    uint ReadyPeerCount,
    uint ActivePeerCount,
    ulong LastChangedMs);

/// <summary>
/// Defines spot node mode values.
/// </summary>
public enum SpotNodeMode
{
    /// <summary>
    /// Indicates the pub/sub spot node mode.
    /// </summary>
    PubSub = 1,
    /// <summary>
    /// Indicates the routed spot node mode.
    /// </summary>
    Routed = 2,
    /// <summary>
    /// Indicates the all spot node mode.
    /// </summary>
    All = 3
}

/// <summary>
/// Defines spot node socket owner values.
/// </summary>
public enum SpotNodeSocketOwner
{
    /// <summary>
    /// Indicates the any spot node socket owner.
    /// </summary>
    Any = 0,
    /// <summary>
    /// Indicates the node spot node socket owner.
    /// </summary>
    Node = 1,
    /// <summary>
    /// Indicates the spot spot node socket owner.
    /// </summary>
    Spot = 2
}

/// <summary>
/// Defines spot node socket type values.
/// </summary>
public enum SpotNodeSocketType
{
    /// <summary>
    /// Indicates the any spot node socket type.
    /// </summary>
    Any = 0,
    /// <summary>
    /// Indicates the pair spot node socket type.
    /// </summary>
    Pair = 0x1001,
    /// <summary>
    /// Indicates the pub spot node socket type.
    /// </summary>
    Pub = 0x1002,
    /// <summary>
    /// Indicates the sub spot node socket type.
    /// </summary>
    Sub = 0x1003,
    /// <summary>
    /// Indicates the dealer spot node socket type.
    /// </summary>
    Dealer = 0x1004,
    /// <summary>
    /// Indicates the router spot node socket type.
    /// </summary>
    Router = 0x1005,
    /// <summary>
    /// Indicates the xpub spot node socket type.
    /// </summary>
    XPub = 0x1006,
    /// <summary>
    /// Indicates the xsub spot node socket type.
    /// </summary>
    XSub = 0x1007,
    /// <summary>
    /// Indicates the stream spot node socket type.
    /// </summary>
    Stream = 0x1008
}

/// <summary>
/// Describes spot node socket filter data.
/// </summary>
/// <param name="Owner">The owner value.</param>
/// <param name="SocketType">The socket type value.</param>
/// <param name="SocketName">The socket name value.</param>
public sealed record SpotNodeSocketFilter(
    SpotNodeSocketOwner? Owner = null,
    SpotNodeSocketType? SocketType = null,
    string? SocketName = null);

/// <summary>
/// Describes spot node socket entry data.
/// </summary>
/// <param name="Owner">The owner value.</param>
/// <param name="OwnerId">The owner id value.</param>
/// <param name="OwnerName">The owner name value.</param>
/// <param name="SocketName">The socket name value.</param>
/// <param name="SocketType">The socket type value.</param>
/// <param name="AutoHwmVisible">The automatic high water mark visible value.</param>
/// <param name="MonitorStatus">The monitor status value.</param>
public sealed record SpotNodeSocketEntry(
    SpotNodeSocketOwner Owner,
    ulong OwnerId,
    string OwnerName,
    string SocketName,
    SpotNodeSocketType SocketType,
    bool AutoHwmVisible,
    MonitorStatus MonitorStatus);

/// <summary>
/// Describes spot node spot entry data.
/// </summary>
/// <param name="SpotRid">The spot routing id value.</param>
/// <param name="SpotKind">The spot kind value.</param>
/// <param name="DispatchHandlerAttached">The dispatch handler attached value.</param>
/// <param name="JoinedActorCount">The joined actor count value.</param>
/// <param name="PendingActorJoinCount">The pending actor join count value.</param>
/// <param name="RouteSynced">The route synced value.</param>
/// <param name="LastChangedMs">The last changed milliseconds value.</param>
public sealed record SpotNodeSpotEntry(RoutingId? SpotRid, SpotKind SpotKind,
    bool DispatchHandlerAttached, uint JoinedActorCount,
    uint PendingActorJoinCount, bool RouteSynced, ulong LastChangedMs);

/// <summary>
/// Describes spot node actor entry data.
/// </summary>
/// <param name="Actor">The actor value.</param>
/// <param name="CurrentSpotRid">The current spot routing id value.</param>
/// <param name="CurrentSpotKind">The current spot kind value.</param>
/// <param name="RouteSynced">The route synced value.</param>
/// <param name="PendingMessageCount">The pending message count value.</param>
/// <param name="LastChangedMs">The last changed milliseconds value.</param>
public sealed record SpotNodeActorEntry(ActorRef Actor, RoutingId CurrentSpotRid,
    SpotKind CurrentSpotKind, bool RouteSynced, uint PendingMessageCount,
    ulong LastChangedMs);
