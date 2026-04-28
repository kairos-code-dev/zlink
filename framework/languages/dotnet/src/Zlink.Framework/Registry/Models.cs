namespace Zlink.Framework.Registry;

public enum ZLinkServiceType
{
    Spot = 0x3002,
    Socket = 0x3003,
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
    ZLinkServiceKind? ServiceKind = null,
    ZLinkServiceRole? ServiceRole = null,
    string? ServiceName = null);

public sealed record ZLinkRegistryTopologyFilter(
    ZLinkServiceKind? ServiceKind = null,
    ZLinkServiceRole? ServiceRole = null,
    string? ServiceName = null,
    global::Zlink.RoutingId? RoutingId = null,
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
    ZLinkServiceKind ServiceKind,
    ZLinkServiceRole ServiceRole,
    string ServiceName,
    uint TotalCount,
    uint ConnectingCount,
    uint ReadyCount,
    uint ErrorCount,
    uint StoppedCount,
    ulong LastReportedMs);

public sealed record ZLinkRegistryTopologyEntry(
    global::Zlink.RoutingId? RoutingId,
    ZLinkServiceKind ServiceKind,
    ZLinkServiceRole ServiceRole,
    string ServiceName,
    string Endpoint,
    ZLinkTopologySource Source,
    ZLinkTopologyState State,
    uint DesiredCount,
    uint ReadyCount,
    uint ErrorCode,
    ulong LastReportedMs);

public sealed record ZLinkMemberPeerEntry(
    ZLinkServiceType ServiceType,
    ZLinkServiceRole ServiceRole,
    string ServiceName,
    string Endpoint,
    global::Zlink.RoutingId? RoutingId,
    long Value,
    uint Weight);
