namespace Zlink.Framework.Registry;

public enum ZLinkAutoConnectType
{
    Invalid = 0,
    RouteMesh = 1,
    ClientServer = 2,
    DealerMesh = 3,
    Fanout = 4,
    SpotMesh = 5,
}

public enum ZLinkServiceKind
{
    Discovery = 1,
    SpotSub = 3,
    SpotPub = 4,
    Socket = 5,
}

public enum ZLinkServiceRole : ushort
{
    Invalid = 0,
    Spot = 2,
    Router = 3,
    Dealer = 4,
    Pub = 5,
    Sub = 6,
}

public enum ZLinkRegistryState
{
    Idle = 1,
    Active = 2,
    Degraded = 3,
    Error = 4,
}

public enum ZLinkTopologySource
{
    Manual = 1,
    Discovery = 2,
    Registry = 3,
}

public enum ZLinkTopologyState
{
    Discovered = 1,
    Connecting = 2,
    Ready = 3,
    Lost = 4,
    Error = 5,
    Stopped = 6,
}

public enum ZLinkAdmissionState
{
    Serving = 1,
    Draining = 2,
}

public sealed record ZLinkRegistryServiceSummaryFilter(
    ZLinkAutoConnectType? AutoConnectType = null,
    ZLinkServiceRole? ServiceRole = null,
    string? ChannelName = null);

public sealed record ZLinkRegistryTopologyFilter(
    ZLinkAutoConnectType? AutoConnectType = null,
    ZLinkServiceKind? ServiceKind = null,
    ZLinkServiceRole? ServiceRole = null,
    string? ChannelName = null,
    RoutingId? RoutingId = null,
    ZLinkTopologyState? State = null,
    ZLinkTopologySource? Source = null);

public sealed record ZLinkRegistryStatus(
    uint RegistryId,
    string BindEndpoint,
    ZLinkRegistryState State,
    uint TopologyEntryCount,
    uint PeerRegistryCount,
    uint ConnectedPeerRegistryCount,
    ulong ListSeq,
    int LastError,
    ulong LastChangedMs);

public sealed record ZLinkRegistryServiceSummaryEntry(
    ZLinkAutoConnectType AutoConnectType,
    ZLinkServiceRole ServiceRole,
    string ChannelName,
    uint TotalCount,
    uint ConnectingCount,
    uint ReadyCount,
    uint ErrorCount,
    uint StoppedCount,
    ulong LastReportedMs);

public sealed record ZLinkRegistryTopologyEntry(
    ZLinkAutoConnectType AutoConnectType,
    RoutingId? RoutingId,
    ZLinkServiceKind ServiceKind,
    ZLinkServiceRole ServiceRole,
    string ChannelName,
    string Endpoint,
    ZLinkTopologySource Source,
    ZLinkTopologyState State,
    uint DesiredCount,
    uint ReadyCount,
    uint ErrorCode,
    ulong LastReportedMs);

public sealed record ZLinkMemberPeerEntry(
    ZLinkAutoConnectType AutoConnectType,
    ZLinkServiceRole ServiceRole,
    string ChannelName,
    string Endpoint,
    RoutingId? RoutingId,
    long Value,
    uint Weight);
