namespace Zlink.Framework;

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
    ZLinkAdmissionState AdmissionState);

internal static class ZLinkRegistryMappings
{
    public static ZLinkRegistryStatus ToFramework(this global::Zlink.RegistryStatus status)
    {
        return new ZLinkRegistryStatus(
            status.RegistryId,
            status.BindEndpoint,
            (ZLinkRegistryState)status.State,
            status.TopologyEntryCount,
            status.PeerRegistryCount,
            status.ConnectedPeerRegistryCount,
            status.ListSeq,
            status.LastError,
            status.LastChangedMs);
    }

    public static ZLinkRegistryServiceSummaryEntry ToFramework(this global::Zlink.RegistryServiceSummaryEntry entry)
    {
        return new ZLinkRegistryServiceSummaryEntry(
            (ZLinkServiceKind)entry.ServiceKind,
            (ZLinkServiceRole)entry.ServiceRole,
            entry.ServiceName,
            entry.TotalCount,
            entry.ConnectingCount,
            entry.ReadyCount,
            entry.ErrorCount,
            entry.StoppedCount,
            entry.LastReportedMs);
    }

    public static ZLinkRegistryTopologyEntry ToFramework(this global::Zlink.RegistryTopologyEntry entry)
    {
        return new ZLinkRegistryTopologyEntry(
            entry.RoutingId,
            (ZLinkServiceKind)entry.ServiceKind,
            (ZLinkServiceRole)entry.ServiceRole,
            entry.ServiceName,
            entry.Endpoint,
            (ZLinkTopologySource)entry.Source,
            (ZLinkTopologyState)entry.State,
            entry.DesiredCount,
            entry.ReadyCount,
            entry.ErrorCode,
            entry.LastReportedMs);
    }

    public static ZLinkMemberPeerEntry ToFramework(this global::Zlink.MemberPeerEntry entry)
    {
        return new ZLinkMemberPeerEntry(
            (ZLinkServiceType)entry.ServiceType,
            (ZLinkServiceRole)entry.ServiceRole,
            entry.ServiceName,
            entry.Endpoint,
            entry.RoutingId,
            entry.Value,
            (ZLinkAdmissionState)entry.AdmissionState);
    }

    public static global::Zlink.RegistryServiceSummaryFilter? ToBackend(this ZLinkRegistryServiceSummaryFilter? filter)
    {
        if (filter is null)
        {
            return null;
        }

        return new global::Zlink.RegistryServiceSummaryFilter(
            filter.ServiceKind is null ? null : (global::Zlink.ServiceKind?)filter.ServiceKind,
            filter.ServiceRole is null ? null : (global::Zlink.ServiceRole?)filter.ServiceRole,
            filter.ServiceName);
    }

    public static global::Zlink.RegistryTopologyFilter? ToBackend(this ZLinkRegistryTopologyFilter? filter)
    {
        if (filter is null)
        {
            return null;
        }

        return new global::Zlink.RegistryTopologyFilter(
            filter.ServiceKind is null ? null : (global::Zlink.ServiceKind?)filter.ServiceKind,
            filter.ServiceRole is null ? null : (global::Zlink.ServiceRole?)filter.ServiceRole,
            filter.ServiceName,
            filter.RoutingId,
            filter.State is null ? null : (global::Zlink.TopologyState?)filter.State,
            filter.Source is null ? null : (global::Zlink.TopologySource?)filter.Source);
    }
}
