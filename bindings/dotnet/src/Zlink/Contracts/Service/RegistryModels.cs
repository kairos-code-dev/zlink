// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public enum RegistryState
{
    Idle = 1,
    Active = 2,
    Degraded = 3,
    Error = 4
}

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
