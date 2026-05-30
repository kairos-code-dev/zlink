// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// The operational state of a registry.
/// </summary>
public enum RegistryState
{
    /// <summary>
    /// Not yet serving.
    /// </summary>
    Idle = 1,
    /// <summary>
    /// Serving normally.
    /// </summary>
    Active = 2,
    /// <summary>
    /// Serving with reduced capability.
    /// </summary>
    Degraded = 3,
    /// <summary>
    /// In an error state.
    /// </summary>
    Error = 4
}

/// <summary>
/// Filter for <see cref="IRegistry.ServiceSummary"/>; null fields match
/// anything.
/// </summary>
/// <param name="AutoConnectType">Restrict to this auto-connect topology.</param>
/// <param name="ServiceRole">Restrict to this service role.</param>
/// <param name="ChannelName">Restrict to this logical channel name.</param>
public sealed record RegistryServiceSummaryFilter(
    AutoConnectType? AutoConnectType = null,
    ServiceRole? ServiceRole = null,
    string? ChannelName = null);

/// <summary>
/// Filter for a registry topology query; null fields match anything.
/// </summary>
/// <param name="AutoConnectType">Restrict to this auto-connect topology.</param>
/// <param name="ServiceKind">Restrict to this kind of service.</param>
/// <param name="ServiceRole">Restrict to this service role.</param>
/// <param name="ChannelName">Restrict to this logical channel name.</param>
/// <param name="RoutingId">Restrict to this routing id.</param>
/// <param name="State">Restrict to this topology state.</param>
/// <param name="Source">Restrict to entries learned from this source.</param>
public sealed record RegistryTopologyFilter(
    AutoConnectType? AutoConnectType = null,
    ServiceKind? ServiceKind = null,
    ServiceRole? ServiceRole = null,
    string? ChannelName = null,
    RoutingId? RoutingId = null,
    TopologyState? State = null,
    TopologySource? Source = null);

/// <summary>
/// A snapshot of a registry's status.
/// </summary>
/// <param name="RegistryId">The registry's identifier.</param>
/// <param name="BindEndpoint">The endpoint the registry is bound to.</param>
/// <param name="State">The registry's operational state.</param>
/// <param name="TopologyEntryCount">The number of topology entries held.</param>
/// <param name="PeerRegistryCount">The number of peer registries configured.</param>
/// <param name="ConnectedPeerRegistryCount">The number of peer registries currently connected.</param>
/// <param name="ListSeq">A monotonically increasing sequence number for the topology list.</param>
/// <param name="LastError">The last native error code, or 0 for none.</param>
/// <param name="LastChangedMs">When the status last changed, in milliseconds.</param>
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

/// <summary>
/// A per-service rollup of registered endpoints grouped by connection state.
/// </summary>
/// <param name="AutoConnectType">The auto-connect topology.</param>
/// <param name="ServiceRole">The service's messaging role.</param>
/// <param name="ChannelName">The logical channel name.</param>
/// <param name="TotalCount">The total number of endpoints.</param>
/// <param name="ConnectingCount">The number of endpoints currently connecting.</param>
/// <param name="ReadyCount">The number of ready endpoints.</param>
/// <param name="ErrorCount">The number of endpoints in error.</param>
/// <param name="StoppedCount">The number of stopped endpoints.</param>
/// <param name="LastReportedMs">When this summary was last reported, in milliseconds.</param>
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

/// <summary>
/// One entry in a registry's topology: a service endpoint and its state.
/// </summary>
/// <param name="AutoConnectType">The auto-connect topology.</param>
/// <param name="RoutingId">The endpoint's routing id, when known.</param>
/// <param name="ServiceKind">The kind of service.</param>
/// <param name="ServiceRole">The service's messaging role.</param>
/// <param name="ChannelName">The logical channel name.</param>
/// <param name="Endpoint">The transport endpoint.</param>
/// <param name="Source">Where this entry was learned from.</param>
/// <param name="State">The connection state of the entry.</param>
/// <param name="DesiredCount">The desired number of connections.</param>
/// <param name="ReadyCount">The number of ready connections.</param>
/// <param name="ErrorCode">The last native error code, or 0 for none.</param>
/// <param name="LastReportedMs">When this entry was last reported, in milliseconds.</param>
/// <param name="SpotKind">The kind of spot, when the entry is a spot.</param>
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

/// <summary>
/// One member peer registered on a channel.
/// </summary>
/// <param name="AutoConnectType">The auto-connect topology.</param>
/// <param name="ServiceRole">The peer's messaging role.</param>
/// <param name="ChannelName">The logical channel name.</param>
/// <param name="Endpoint">The peer's transport endpoint.</param>
/// <param name="RoutingId">The peer's routing id, when known.</param>
/// <param name="Value">The application-defined value advertised by the peer.</param>
/// <param name="Weight">The peer's load-balancing weight.</param>
public sealed record MemberPeerEntry(
    AutoConnectType AutoConnectType,
    ServiceRole ServiceRole,
    string ChannelName,
    string Endpoint,
    RoutingId? RoutingId,
    long Value,
    uint Weight);
