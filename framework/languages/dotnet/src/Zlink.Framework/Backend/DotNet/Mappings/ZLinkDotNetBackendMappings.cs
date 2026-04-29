namespace Zlink.Framework.Backend.DotNet.Mappings;


internal static class ZLinkDotNetBackendMappings
{
    public static ZLinkRegistryStatus ToFramework(this RegistryStatus status)
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

    public static ZLinkRegistryServiceSummaryEntry ToFramework(this RegistryServiceSummaryEntry entry)
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

    public static ZLinkRegistryTopologyEntry ToFramework(this RegistryTopologyEntry entry)
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

    public static ZLinkMemberPeerEntry ToFramework(this MemberPeerEntry entry)
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

    public static ZLinkSpotNodeStatus ToFramework(this SpotNodeStatus status)
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

    public static ZLinkSpotNodePeerEntry ToFramework(this SpotNodePeerEntry entry)
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

    public static ZLinkSpotNodeSubjectEntry ToFramework(this SpotNodeSubjectEntry entry)
    {
        return new ZLinkSpotNodeSubjectEntry(
            (ZLinkSpotRole)entry.Role,
            entry.Subject,
            (ZLinkSubjectKind)entry.SubjectKind,
            entry.ReadyPeerCount,
            entry.ActivePeerCount,
            entry.LastChangedMs);
    }

    public static RegistryServiceSummaryFilter? ToNative(this ZLinkRegistryServiceSummaryFilter? filter)
    {
        if (filter is null)
        {
            return null;
        }

        return new RegistryServiceSummaryFilter(
            filter.ServiceKind is null ? null : (ServiceKind?)filter.ServiceKind,
            filter.ServiceRole is null ? null : (ServiceRole?)filter.ServiceRole,
            filter.ServiceName);
    }

    public static RegistryTopologyFilter? ToNative(this ZLinkRegistryTopologyFilter? filter)
    {
        if (filter is null)
        {
            return null;
        }

        return new RegistryTopologyFilter(
            filter.ServiceKind is null ? null : (ServiceKind?)filter.ServiceKind,
            filter.ServiceRole is null ? null : (ServiceRole?)filter.ServiceRole,
            filter.ServiceName,
            filter.RoutingId,
            filter.State is null ? null : (TopologyState?)filter.State,
            filter.Source is null ? null : (TopologySource?)filter.Source);
    }

    public static ZLinkBackendSocketMonitorEvent ToFramework(this MonitorEvent monitorEvent)
    {
        return new ZLinkBackendSocketMonitorEvent(
            (ZLinkSocketNativeEventType)monitorEvent.Event,
            monitorEvent.RoutingId,
            monitorEvent.LocalAddr,
            monitorEvent.RemoteAddr,
            monitorEvent.Value);
    }

    public static ZLinkBackendSpotDispatchInfo ToFramework(this SpotDispatchInfo info)
    {
        return new ZLinkBackendSpotDispatchInfo(
            info.Event switch
            {
                SpotDispatchEvent.RoutedReadable => ZLinkBackendSpotDispatchEvent.RoutedReadable,
                SpotDispatchEvent.ChannelReplyReadable => ZLinkBackendSpotDispatchEvent.ChannelReplyReadable,
                _ => ZLinkBackendSpotDispatchEvent.Internal,
            },
            info.Subject);
    }
}
