// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// Defines spot node state values.
/// </summary>
public enum SpotNodeState
{
    /// <summary>
    /// Represents the Idle value.
    /// </summary>
    Idle = 1,
    /// <summary>
    /// Represents the Connecting value.
    /// </summary>
    Connecting = 2,
    /// <summary>
    /// Represents the PartialReady value.
    /// </summary>
    PartialReady = 3,
    /// <summary>
    /// Represents the Ready value.
    /// </summary>
    Ready = 4,
    /// <summary>
    /// Represents the Error value.
    /// </summary>
    Error = 5
}

/// <summary>
/// Defines spot peer source values.
/// </summary>
public enum SpotPeerSource
{
    /// <summary>
    /// Represents the Manual value.
    /// </summary>
    Manual = 1,
    /// <summary>
    /// Represents the Discovery value.
    /// </summary>
    Discovery = 2,
    /// <summary>
    /// Represents the Mixed value.
    /// </summary>
    Mixed = 3
}

/// <summary>
/// Defines spot peer kind values.
/// </summary>
public enum SpotPeerKind
{
    /// <summary>
    /// Represents the SpotMesh value.
    /// </summary>
    SpotMesh = 1,
    /// <summary>
    /// Represents the RouterChannel value.
    /// </summary>
    RouterChannel = 2
}

/// <summary>
/// Defines spot peer state values.
/// </summary>
public enum SpotPeerState
{
    /// <summary>
    /// Represents the Configured value.
    /// </summary>
    Configured = 1,
    /// <summary>
    /// Represents the Connecting value.
    /// </summary>
    Connecting = 2,
    /// <summary>
    /// Represents the Connected value.
    /// </summary>
    Connected = 3
}

/// <summary>
/// Defines spot kind values.
/// </summary>
public enum SpotKind
{
    /// <summary>
    /// Represents the Invalid value.
    /// </summary>
    Invalid = 0,
    /// <summary>
    /// Represents the Entry value.
    /// </summary>
    Entry = 1,
    /// <summary>
    /// Represents the User value.
    /// </summary>
    User = 2
}

/// <summary>
/// Defines subject kind values.
/// </summary>
public enum SubjectKind
{
    /// <summary>
    /// Represents the None value.
    /// </summary>
    None = 0,
    /// <summary>
    /// Represents the Topic value.
    /// </summary>
    Topic = 1,
    /// <summary>
    /// Represents the Pattern value.
    /// </summary>
    Pattern = 2
}

/// <summary>
/// Defines spot role values.
/// </summary>
public enum SpotRole
{
    /// <summary>
    /// Represents the Pub value.
    /// </summary>
    Pub = 1,
    /// <summary>
    /// Represents the Sub value.
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
    /// Represents the PubSub value.
    /// </summary>
    PubSub = 1,
    /// <summary>
    /// Represents the Routed value.
    /// </summary>
    Routed = 2,
    /// <summary>
    /// Represents the All value.
    /// </summary>
    All = 3
}

/// <summary>
/// Defines spot node socket owner values.
/// </summary>
public enum SpotNodeSocketOwner
{
    /// <summary>
    /// Represents the Any value.
    /// </summary>
    Any = 0,
    /// <summary>
    /// Represents the Node value.
    /// </summary>
    Node = 1,
    /// <summary>
    /// Represents the Spot value.
    /// </summary>
    Spot = 2
}

/// <summary>
/// Defines spot node socket type values.
/// </summary>
public enum SpotNodeSocketType
{
    /// <summary>
    /// Represents the Any value.
    /// </summary>
    Any = 0,
    /// <summary>
    /// Represents the Pair value.
    /// </summary>
    Pair = 0x1001,
    /// <summary>
    /// Represents the Pub value.
    /// </summary>
    Pub = 0x1002,
    /// <summary>
    /// Represents the Sub value.
    /// </summary>
    Sub = 0x1003,
    /// <summary>
    /// Represents the Dealer value.
    /// </summary>
    Dealer = 0x1004,
    /// <summary>
    /// Represents the Router value.
    /// </summary>
    Router = 0x1005,
    /// <summary>
    /// Represents the XPub value.
    /// </summary>
    XPub = 0x1006,
    /// <summary>
    /// Represents the XSub value.
    /// </summary>
    XSub = 0x1007,
    /// <summary>
    /// Represents the Stream value.
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
