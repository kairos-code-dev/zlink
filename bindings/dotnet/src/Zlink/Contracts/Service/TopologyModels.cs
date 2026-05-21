// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public enum AutoConnectType
{
    Invalid = 0,
    RouteMesh = 1,
    ClientServer = 2,
    DealerMesh = 3,
    Fanout = 4,
    SpotMesh = 5
}

public enum ServiceKind
{
    Discovery = 1,
    SpotSub = 3,
    SpotPub = 4,
    Socket = 5
}

public enum ServiceRole
{
    Invalid = 0,
    Spot = 2,
    Router = 3,
    Dealer = 4,
    Pub = 5,
    Sub = 6
}

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

public enum RegistryState
{
    Idle = 1,
    Active = 2,
    Degraded = 3,
    Error = 4
}

public enum TopologySource
{
    Manual = 1,
    Discovery = 2,
    Registry = 3
}

public enum TopologyState
{
    Discovered = 1,
    Connecting = 2,
    Ready = 3,
    Lost = 4,
    Error = 5,
    Stopped = 6
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

public sealed record RegistryServiceSummaryFilter(
    AutoConnectType? AutoConnectType = null,
    ServiceRole? ServiceRole = null,
    string? ChannelName = null);

public sealed record RegistryTopologyFilter(
    AutoConnectType? AutoConnectType = null,
    ServiceKind? ServiceKind = null,
    ServiceRole? ServiceRole = null,
    string? ChannelName = null,
    RoutingId? RoutingId = null,
    TopologyState? State = null,
    TopologySource? Source = null);

public sealed record RegistryStatus(
    uint RegistryId,
    string BindEndpoint,
    RegistryState State,
    uint TopologyEntryCount,
    uint PeerRegistryCount,
    uint ConnectedPeerRegistryCount,
    ulong ListSeq,
    int LastError,
    ulong LastChangedMs);

public sealed record RegistryServiceSummaryEntry(
    AutoConnectType AutoConnectType,
    ServiceRole ServiceRole,
    string ChannelName,
    uint TotalCount,
    uint ConnectingCount,
    uint ReadyCount,
    uint ErrorCount,
    uint StoppedCount,
    ulong LastReportedMs);

public sealed record RegistryTopologyEntry(
    AutoConnectType AutoConnectType,
    RoutingId? RoutingId,
    ServiceKind ServiceKind,
    ServiceRole ServiceRole,
    string ChannelName,
    string Endpoint,
    TopologySource Source,
    TopologyState State,
    uint DesiredCount,
    uint ReadyCount,
    uint ErrorCode,
    ulong LastReportedMs,
    SpotKind SpotKind);

public sealed record MemberPeerEntry(
    AutoConnectType AutoConnectType,
    ServiceRole ServiceRole,
    string ChannelName,
    string Endpoint,
    RoutingId? RoutingId,
    long Value,
    uint Weight);

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
