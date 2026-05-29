// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// Defines registry state values.
/// </summary>
public enum RegistryState
{
    /// <summary>
    /// Represents the Idle value.
    /// </summary>
    Idle = 1,
    /// <summary>
    /// Represents the Active value.
    /// </summary>
    Active = 2,
    /// <summary>
    /// Represents the Degraded value.
    /// </summary>
    Degraded = 3,
    /// <summary>
    /// Represents the Error value.
    /// </summary>
    Error = 4
}

/// <summary>
/// Describes registry service summary filter data.
/// </summary>
/// <param name="AutoConnectType">The auto connect type value.</param>
/// <param name="ServiceRole">The service role value.</param>
/// <param name="ChannelName">The channel name value.</param>
public sealed record RegistryServiceSummaryFilter(
    AutoConnectType? AutoConnectType = null,
    ServiceRole? ServiceRole = null,
    string? ChannelName = null);

/// <summary>
/// Describes registry topology filter data.
/// </summary>
/// <param name="AutoConnectType">The auto connect type value.</param>
/// <param name="ServiceKind">The service kind value.</param>
/// <param name="ServiceRole">The service role value.</param>
/// <param name="ChannelName">The channel name value.</param>
/// <param name="RoutingId">The routing id value.</param>
/// <param name="State">The state value.</param>
/// <param name="Source">The source value.</param>
public sealed record RegistryTopologyFilter(
    AutoConnectType? AutoConnectType = null,
    ServiceKind? ServiceKind = null,
    ServiceRole? ServiceRole = null,
    string? ChannelName = null,
    RoutingId? RoutingId = null,
    TopologyState? State = null,
    TopologySource? Source = null);

/// <summary>
/// Describes registry status data.
/// </summary>
/// <param name="RegistryId">The registry id value.</param>
/// <param name="BindEndpoint">The bind endpoint value.</param>
/// <param name="State">The state value.</param>
/// <param name="TopologyEntryCount">The topology entry count value.</param>
/// <param name="PeerRegistryCount">The peer registry count value.</param>
/// <param name="ConnectedPeerRegistryCount">The connected peer registry count value.</param>
/// <param name="ListSeq">The list sequence value.</param>
/// <param name="LastError">The last error value.</param>
/// <param name="LastChangedMs">The last changed milliseconds value.</param>
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
/// Describes registry service summary entry data.
/// </summary>
/// <param name="AutoConnectType">The auto connect type value.</param>
/// <param name="ServiceRole">The service role value.</param>
/// <param name="ChannelName">The channel name value.</param>
/// <param name="TotalCount">The total count value.</param>
/// <param name="ConnectingCount">The connecting count value.</param>
/// <param name="ReadyCount">The ready count value.</param>
/// <param name="ErrorCount">The error count value.</param>
/// <param name="StoppedCount">The stopped count value.</param>
/// <param name="LastReportedMs">The last reported milliseconds value.</param>
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
/// Describes registry topology entry data.
/// </summary>
/// <param name="AutoConnectType">The auto connect type value.</param>
/// <param name="RoutingId">The routing id value.</param>
/// <param name="ServiceKind">The service kind value.</param>
/// <param name="ServiceRole">The service role value.</param>
/// <param name="ChannelName">The channel name value.</param>
/// <param name="Endpoint">The endpoint value.</param>
/// <param name="Source">The source value.</param>
/// <param name="State">The state value.</param>
/// <param name="DesiredCount">The desired count value.</param>
/// <param name="ReadyCount">The ready count value.</param>
/// <param name="ErrorCode">The error code value.</param>
/// <param name="LastReportedMs">The last reported milliseconds value.</param>
/// <param name="SpotKind">The spot kind value.</param>
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
/// Describes member peer entry data.
/// </summary>
/// <param name="AutoConnectType">The auto connect type value.</param>
/// <param name="ServiceRole">The service role value.</param>
/// <param name="ChannelName">The channel name value.</param>
/// <param name="Endpoint">The endpoint value.</param>
/// <param name="RoutingId">The routing id value.</param>
/// <param name="Value">The value value.</param>
/// <param name="Weight">The weight value.</param>
public sealed record MemberPeerEntry(
    AutoConnectType AutoConnectType,
    ServiceRole ServiceRole,
    string ChannelName,
    string Endpoint,
    RoutingId? RoutingId,
    long Value,
    uint Weight);
