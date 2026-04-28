// SPDX-License-Identifier: MPL-2.0

using Zlink.Native;

namespace Zlink;

public enum ServiceType
{
    Spot = 0x3002,
    Socket = 0x3003
}

public enum ServiceKind
{
    Discovery = 1,
    SpotSub = 3,
    SpotPub = 4,
    Socket = 5
}

public enum ServiceRole : ushort
{
    Invalid = 0,
    Spot = 2,
    Router = 3,
    Dealer = 4,
    Pub = 5,
    Sub = 6
}

public enum DiscoveryDealerPeerMode
{
    Router = 1,
    Dealer = 2
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

public enum SpotPeerState
{
    Configured = 1,
    Connecting = 2,
    Connected = 3
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

public enum SubjectKind : uint
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
    ServiceKind? ServiceKind = null,
    ServiceRole? ServiceRole = null,
    string? ServiceName = null);

public sealed record RegistryTopologyFilter(
    ServiceKind? ServiceKind = null,
    ServiceRole? ServiceRole = null,
    string? ServiceName = null,
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
    ulong LastChangedMs)
{
    internal static unsafe RegistryStatus FromNative(ref ZlinkRegistryStatus native)
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
}

public sealed record RegistryServiceSummaryEntry(
    ServiceKind ServiceKind,
    ServiceRole ServiceRole,
    string ServiceName,
    uint TotalCount,
    uint ConnectingCount,
    uint ReadyCount,
    uint ErrorCount,
    uint StoppedCount,
    ulong LastReportedMs)
{
    internal static unsafe RegistryServiceSummaryEntry FromNative(
        ref ZlinkRegistryServiceSummaryEntry native)
    {
        fixed (byte* serviceName = native.ServiceName)
        {
            return new RegistryServiceSummaryEntry(
                (ServiceKind)native.ServiceKind, (ServiceRole)native.ServiceRole,
                NativeHelpers.ReadFixedString(serviceName, 256),
                native.TotalCount, native.ConnectingCount, native.ReadyCount,
                native.ErrorCount, native.StoppedCount, native.LastReportedMs);
        }
    }
}

public sealed record RegistryTopologyEntry(
    RoutingId? RoutingId,
    ServiceKind ServiceKind,
    ServiceRole ServiceRole,
    string ServiceName,
    string Endpoint,
    TopologySource Source,
    TopologyState State,
    uint DesiredCount,
    uint ReadyCount,
    uint ErrorCode,
    ulong LastReportedMs)
{
    internal static unsafe RegistryTopologyEntry FromNative(
        ref ZlinkRegistryTopologyEntry native)
    {
        fixed (byte* serviceName = native.ServiceName)
        fixed (byte* endpoint = native.Endpoint)
        {
            return new RegistryTopologyEntry(
                RoutingIdCodec.ToRoutingId(
                    NativeHelpers.ReadRoutingId(ref native.RoutingId)),
                (ServiceKind)native.ServiceKind,
                (ServiceRole)native.ServiceRole,
                NativeHelpers.ReadFixedString(serviceName, 256),
                NativeHelpers.ReadFixedString(endpoint, 256),
                (TopologySource)native.Source, (TopologyState)native.State,
                native.DesiredCount, native.ReadyCount, native.ErrorCode,
                native.LastReportedMs);
        }
    }
}

public sealed record MemberPeerEntry(
    ServiceType ServiceType,
    ServiceRole ServiceRole,
    string ServiceName,
    string Endpoint,
    RoutingId? RoutingId,
    long Value,
    uint Weight)
{
    internal static unsafe MemberPeerEntry FromNative(ref ZlinkMemberPeerEntry native)
    {
        fixed (byte* serviceName = native.ServiceName)
        fixed (byte* endpoint = native.Endpoint)
        {
            return new MemberPeerEntry((ServiceType)native.ServiceType,
                (ServiceRole)native.ServiceRole,
                NativeHelpers.ReadFixedString(serviceName, 256),
                NativeHelpers.ReadFixedString(endpoint, 256),
                RoutingIdCodec.ToRoutingId(
                    NativeHelpers.ReadRoutingId(ref native.RoutingId)),
                native.Value,
                native.Weight);
        }
    }
}

public sealed record SpotNodeStatus(
    string ServiceName,
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
    ulong LastChangedMs)
{
    internal static unsafe SpotNodeStatus FromNative(ref ZlinkSpotNodeStatus native)
    {
        fixed (byte* serviceName = native.ServiceName)
        fixed (byte* endpoint = native.LocalEndpoint)
        {
            return new SpotNodeStatus(
                NativeHelpers.ReadFixedString(serviceName, 256),
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
}

public sealed record SpotNodePeerEntry(
    string ServiceName,
    string LocalEndpoint,
    string PeerEndpoint,
    SpotPeerSource Source,
    SpotPeerState State,
    uint Weight,
    ulong ConnectedSinceMs,
    ulong LastChangedMs)
{
    internal static unsafe SpotNodePeerEntry FromNative(
        ref ZlinkSpotNodePeerEntry native)
    {
        fixed (byte* serviceName = native.ServiceName)
        fixed (byte* local = native.LocalEndpoint)
        fixed (byte* peer = native.PeerEndpoint)
        {
            return new SpotNodePeerEntry(
                NativeHelpers.ReadFixedString(serviceName, 256),
                NativeHelpers.ReadFixedString(local, 256),
                NativeHelpers.ReadFixedString(peer, 256),
                (SpotPeerSource)native.Source, (SpotPeerState)native.State,
                native.Weight,
                native.ConnectedSinceMs, native.LastChangedMs);
        }
    }
}

internal sealed record SpotServiceAttachmentStats(
    string ServiceName,
    uint RouterCount,
    uint PubCount,
    uint SubCount,
    uint AutoRouterCount,
    uint AutoPubCount,
    uint AutoSubCount)
{
    internal static unsafe SpotServiceAttachmentStats FromNative(
        ref ZlinkSpotServiceAttachmentStats native)
    {
        fixed (byte* serviceName = native.ServiceName)
        {
            return new SpotServiceAttachmentStats(
                NativeHelpers.ReadFixedString(serviceName, 256),
                native.RouterCount, native.PubCount, native.SubCount,
                native.AutoRouterCount, native.AutoPubCount,
                native.AutoSubCount);
        }
    }
}

public sealed record SpotNodeSubjectEntry(
    SpotRole Role,
    string Subject,
    SubjectKind SubjectKind,
    uint ReadyPeerCount,
    uint ActivePeerCount,
    ulong LastChangedMs)
{
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
}
