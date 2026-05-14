// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Native;

namespace Systems.Zlink;

internal static class TopologyModelConverters
{
    internal static unsafe RegistryStatus FromNative(
        ref ZlinkRegistryStatus native)
    {
        fixed (byte* endpoint = native.BindEndpoint)
        {
            return new RegistryStatus(native.RegistryId,
                NativeHelpers.ReadFixedString(endpoint, 256),
                (RegistryState)native.State, native.TopologyEntryCount,
                native.PeerRegistryCount, native.ConnectedPeerRegistryCount,
                native.ListSeq, native.LastError, native.LastChangedMs);
        }
    }

    internal static unsafe RegistryServiceSummaryEntry FromNative(
        ref ZlinkRegistryServiceSummaryEntry native)
    {
        fixed (byte* channelName = native.ChannelName)
        {
            return new RegistryServiceSummaryEntry(
                (AutoConnectType)native.AutoConnectType,
                (ServiceRole)native.ServiceRole,
                NativeHelpers.ReadFixedString(channelName, 256),
                native.TotalCount, native.ConnectingCount, native.ReadyCount,
                native.ErrorCount, native.StoppedCount, native.LastReportedMs);
        }
    }

    internal static unsafe RegistryTopologyEntry FromNative(
        ref ZlinkRegistryTopologyEntry native)
    {
        fixed (byte* channelName = native.ChannelName)
        fixed (byte* endpoint = native.Endpoint)
        {
            return new RegistryTopologyEntry(
                (AutoConnectType)native.AutoConnectType,
                RoutingIdCodec.ToRoutingId(
                    NativeHelpers.ReadRoutingId(ref native.RoutingId)),
                (ServiceKind)native.ServiceKind,
                (ServiceRole)native.ServiceRole,
                NativeHelpers.ReadFixedString(channelName, 256),
                NativeHelpers.ReadFixedString(endpoint, 256),
                (TopologySource)native.Source, (TopologyState)native.State,
                native.DesiredCount, native.ReadyCount, native.ErrorCode,
                native.LastReportedMs);
        }
    }

    internal static unsafe MemberPeerEntry FromNative(
        ref ZlinkMemberPeerEntry native)
    {
        fixed (byte* channelName = native.ChannelName)
        fixed (byte* endpoint = native.Endpoint)
        {
            return new MemberPeerEntry((AutoConnectType)native.AutoConnectType,
                (ServiceRole)native.ServiceRole,
                NativeHelpers.ReadFixedString(channelName, 256),
                NativeHelpers.ReadFixedString(endpoint, 256),
                RoutingIdCodec.ToRoutingId(
                    NativeHelpers.ReadRoutingId(ref native.RoutingId)),
                native.Value,
                native.Weight);
        }
    }

    internal static unsafe SpotNodeStatus FromNative(
        ref ZlinkSpotNodeStatus native)
    {
        fixed (byte* channelName = native.ChannelName)
        fixed (byte* endpoint = native.LocalEndpoint)
        {
            return new SpotNodeStatus(
                NativeHelpers.ReadFixedString(channelName, 256),
                NativeHelpers.ReadFixedString(endpoint, 256),
                RoutingIdCodec.ToRoutingId(
                    NativeHelpers.ReadRoutingId(ref native.NodeRoutingId)),
                (SpotNodeState)native.State, native.ConfiguredPeerCount,
                native.ActivePeerCount, native.ConnectedPeerCount,
                native.SubjectCount, native.ReadySubjectCount,
                native.DisconnectedSubTargetCount,
                native.DisconnectedRoutedTargetCount,
                native.LastError, native.LastChangedMs);
        }
    }

    internal static unsafe SpotNodePeerEntry FromNative(
        ref ZlinkSpotNodePeerEntry native)
    {
        fixed (byte* channelName = native.ChannelName)
        fixed (byte* local = native.LocalEndpoint)
        fixed (byte* peer = native.PeerEndpoint)
        {
            return new SpotNodePeerEntry(
                NativeHelpers.ReadFixedString(channelName, 256),
                NativeHelpers.ReadFixedString(local, 256),
                NativeHelpers.ReadFixedString(peer, 256),
                (SpotPeerSource)native.Source, (SpotPeerState)native.State,
                native.Weight,
                native.ConnectedSinceMs, native.LastChangedMs);
        }
    }

    internal static unsafe SpotNodeSubjectEntry FromNative(
        ref ZlinkSpotNodeSubjectEntry native)
    {
        fixed (byte* subject = native.Subject)
        {
            return new SpotNodeSubjectEntry((SpotRole)native.Role,
                NativeHelpers.ReadFixedString(subject, 256),
                (SubjectKind)native.SubjectKind,
                native.ReadyPeerCount, native.ActivePeerCount,
                native.LastChangedMs);
        }
    }

    internal static unsafe SpotNodeSocketSnapshotEntry FromNative(
        ref ZlinkSpotNodeSocketSnapshotEntry native)
    {
        fixed (byte* ownerName = native.OwnerName)
        fixed (byte* socketName = native.SocketName)
        {
            ZlinkMonitorSnapshot snapshot = native.Snapshot;
            return new SpotNodeSocketSnapshotEntry(
                native.Owner,
                native.OwnerId,
                NativeHelpers.ReadFixedString(ownerName, 64),
                NativeHelpers.ReadFixedString(socketName, 64),
                native.SocketType,
                native.AutoHwmVisible != 0,
                MonitorConverters.FromNative(ref snapshot));
        }
    }
}
