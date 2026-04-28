namespace Zlink.Framework.Backend.DotNet.Mappings;


internal static class ZLinkDotNetBackendMappings
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
            entry.Weight);
    }

    public static ZLinkSpotNodeStatus ToFramework(this global::Zlink.SpotNodeStatus status)
    {
        return new ZLinkSpotNodeStatus(
            status.ServiceName,
            status.LocalEndpoint,
            status.NodeRoutingId,
            (ZLinkSpotNodeState)status.State,
            status.ConfiguredPeerCount,
            status.ActivePeerCount,
            status.ConnectedPeerCount,
            status.SubjectCount,
            status.ReadySubjectCount,
            status.LastError,
            status.LastChangedMs);
    }

    public static ZLinkSpotNodePeerEntry ToFramework(this global::Zlink.SpotNodePeerEntry entry)
    {
        return new ZLinkSpotNodePeerEntry(
            entry.ServiceName,
            entry.LocalEndpoint,
            entry.PeerEndpoint,
            (ZLinkSpotPeerSource)entry.Source,
            (ZLinkSpotPeerState)entry.State,
            entry.Weight,
            entry.ConnectedSinceMs,
            entry.LastChangedMs);
    }

    public static ZLinkSpotNodeSubjectEntry ToFramework(this global::Zlink.SpotNodeSubjectEntry entry)
    {
        return new ZLinkSpotNodeSubjectEntry(
            (ZLinkSpotRole)entry.Role,
            entry.Subject,
            (ZLinkSubjectKind)entry.SubjectKind,
            entry.ReadyPeerCount,
            entry.ActivePeerCount,
            entry.LastChangedMs);
    }

    public static global::Zlink.RegistryServiceSummaryFilter? ToNative(this ZLinkRegistryServiceSummaryFilter? filter)
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

    public static global::Zlink.RegistryTopologyFilter? ToNative(this ZLinkRegistryTopologyFilter? filter)
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

    public static ZLinkBackendSocketMonitorEvent ToFramework(this global::Zlink.MonitorEvent monitorEvent)
    {
        return new ZLinkBackendSocketMonitorEvent(
            (ZLinkSocketNativeEventType)monitorEvent.Event,
            monitorEvent.RoutingId,
            monitorEvent.LocalAddr,
            monitorEvent.RemoteAddr,
            monitorEvent.Value);
    }

    public static ZLinkBackendSpotDispatchInfo ToFramework(this global::Zlink.SpotDispatchInfo info)
    {
        return new ZLinkBackendSpotDispatchInfo(
            info.Event switch
            {
                global::Zlink.SpotDispatchEvent.RoutedReadable => ZLinkBackendSpotDispatchEvent.RoutedReadable,
                global::Zlink.SpotDispatchEvent.ChannelReplyReadable => ZLinkBackendSpotDispatchEvent.ChannelReplyReadable,
                _ => ZLinkBackendSpotDispatchEvent.Internal,
            },
            info.Subject);
    }
}
