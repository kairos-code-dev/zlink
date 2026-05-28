// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public enum SpotNodeState
{
    Idle = 1,
    Connecting = 2,
    PartialReady = 3,
    Ready = 4,
    Error = 5
}

public enum SpotPeerSource
{
    Manual = 1,
    Discovery = 2,
    Mixed = 3
}

public enum SpotPeerKind
{
    SpotMesh = 1,
    RouterChannel = 2
}

public enum SpotPeerState
{
    Configured = 1,
    Connecting = 2,
    Connected = 3
}

public enum SpotKind
{
    Invalid = 0,
    Entry = 1,
    User = 2
}

public enum SubjectKind
{
    None = 0,
    Topic = 1,
    Pattern = 2
}

public enum SpotRole
{
    Pub = 1,
    Sub = 2
}

public sealed record SpotNodePeerFilter(
    string? PeerEndpoint = null,
    SpotPeerSource? Source = null,
    SpotPeerState? State = null);

public sealed record SpotNodeSubjectFilter(
    SpotRole? Role = null,
    string? Subject = null,
    SubjectKind? SubjectKind = null);

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

public sealed record SpotNodeSubjectEntry(
    SpotRole Role,
    string Subject,
    SubjectKind SubjectKind,
    uint ReadyPeerCount,
    uint ActivePeerCount,
    ulong LastChangedMs);

public enum SpotNodeMode
{
    PubSub = 1,
    Routed = 2,
    All = 3
}

public enum SpotNodeSocketOwner
{
    Any = 0,
    Node = 1,
    Spot = 2
}

public enum SpotNodeSocketType
{
    Any = 0,
    Pair = 0x1001,
    Pub = 0x1002,
    Sub = 0x1003,
    Dealer = 0x1004,
    Router = 0x1005,
    XPub = 0x1006,
    XSub = 0x1007,
    Stream = 0x1008
}

public sealed record SpotNodeSocketFilter(
    SpotNodeSocketOwner? Owner = null,
    SpotNodeSocketType? SocketType = null,
    string? SocketName = null);

public sealed record SpotNodeSocketEntry(
    SpotNodeSocketOwner Owner,
    ulong OwnerId,
    string OwnerName,
    string SocketName,
    SpotNodeSocketType SocketType,
    bool AutoHwmVisible,
    MonitorStatus MonitorStatus);

public sealed record SpotNodeSpotEntry(RoutingId? SpotRid, SpotKind SpotKind,
    bool DispatchHandlerAttached, uint JoinedActorCount,
    uint PendingActorJoinCount, bool RouteSynced, ulong LastChangedMs);

public sealed record SpotNodeActorEntry(ActorRef Actor, RoutingId CurrentSpotRid,
    SpotKind CurrentSpotKind, bool RouteSynced, uint PendingMessageCount,
    ulong LastChangedMs);
