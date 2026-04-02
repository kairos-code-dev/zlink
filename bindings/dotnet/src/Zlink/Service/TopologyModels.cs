// SPDX-License-Identifier: MPL-2.0

using System;
using Zlink.Native;

namespace Zlink.Service;

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

public enum ServiceEventSubjectKind : uint
{
    None = 0,
    Topic = 1,
    Pattern = 2
}

public readonly struct SpotNodePeerFilter
{
    public SpotNodePeerFilter(string? peerEndpoint = null,
        SpotPeerSource? source = null, SpotPeerState? state = null)
    {
        PeerEndpoint = peerEndpoint;
        Source = source;
        State = state;
    }

    public string? PeerEndpoint { get; }
    public SpotPeerSource? Source { get; }
    public SpotPeerState? State { get; }
}

public readonly struct SpotNodeSubjectFilter
{
    public SpotNodeSubjectFilter(SpotSocketRole? role = null,
        string? subject = null,
        ServiceEventSubjectKind? subjectKind = null)
    {
        Role = role;
        Subject = subject;
        SubjectKind = subjectKind;
    }

    public SpotSocketRole? Role { get; }
    public string? Subject { get; }
    public ServiceEventSubjectKind? SubjectKind { get; }
}

public readonly struct RegistryServiceSummaryFilter
{
    public RegistryServiceSummaryFilter(ServiceKind? serviceKind = null,
        ServiceRole? serviceRole = null, string? serviceName = null)
    {
        ServiceKind = serviceKind;
        ServiceRole = serviceRole;
        ServiceName = serviceName;
    }

    public ServiceKind? ServiceKind { get; }
    public ServiceRole? ServiceRole { get; }
    public string? ServiceName { get; }
}

public readonly struct RegistryTopologyFilter
{
    public RegistryTopologyFilter(ServiceKind? serviceKind = null,
        ServiceRole? serviceRole = null, string? serviceName = null,
        RoutingId? routingId = null, TopologyState? state = null,
        TopologySource? source = null)
    {
        ServiceKind = serviceKind;
        ServiceRole = serviceRole;
        ServiceName = serviceName;
        RoutingId = routingId;
        State = state;
        Source = source;
    }

    public ServiceKind? ServiceKind { get; }
    public ServiceRole? ServiceRole { get; }
    public string? ServiceName { get; }
    public RoutingId? RoutingId { get; }
    public TopologyState? State { get; }
    public TopologySource? Source { get; }
}

public readonly struct RegistryStatus
{
    public RegistryStatus(uint registryId, string bindEndpoint, RegistryState state,
        uint topologyEntryCount, uint peerRegistryCount,
        uint connectedPeerRegistryCount, ulong listSeq, int lastError,
        ulong lastChangedMs)
    {
        RegistryId = registryId;
        BindEndpoint = bindEndpoint;
        State = state;
        TopologyEntryCount = topologyEntryCount;
        PeerRegistryCount = peerRegistryCount;
        ConnectedPeerRegistryCount = connectedPeerRegistryCount;
        ListSeq = listSeq;
        LastError = lastError;
        LastChangedMs = lastChangedMs;
    }

    public uint RegistryId { get; }
    public string BindEndpoint { get; }
    public RegistryState State { get; }
    public uint TopologyEntryCount { get; }
    public uint PeerRegistryCount { get; }
    public uint ConnectedPeerRegistryCount { get; }
    public ulong ListSeq { get; }
    public int LastError { get; }
    public ulong LastChangedMs { get; }

    internal static unsafe RegistryStatus FromNative(
        ref ZlinkRegistryStatus native)
    {
        fixed (byte* endpoint = native.BindEndpoint)
        {
            return new RegistryStatus(native.RegistryId,
                NativeHelpers.ReadFixedString(endpoint, 256),
                (RegistryState)native.State,
                native.TopologyEntryCount, native.PeerRegistryCount,
                native.ConnectedPeerRegistryCount, native.ListSeq,
                native.LastError, native.LastChangedMs);
        }
    }
}

public readonly struct RegistryServiceSummaryEntry
{
    public RegistryServiceSummaryEntry(ServiceKind serviceKind,
        ServiceRole serviceRole, string serviceName, uint totalCount,
        uint connectingCount, uint readyCount, uint errorCount,
        uint stoppedCount, ulong lastReportedMs)
    {
        ServiceKind = serviceKind;
        ServiceRole = serviceRole;
        ServiceName = serviceName;
        TotalCount = totalCount;
        ConnectingCount = connectingCount;
        ReadyCount = readyCount;
        ErrorCount = errorCount;
        StoppedCount = stoppedCount;
        LastReportedMs = lastReportedMs;
    }

    public ServiceKind ServiceKind { get; }
    public ServiceRole ServiceRole { get; }
    public string ServiceName { get; }
    public uint TotalCount { get; }
    public uint ConnectingCount { get; }
    public uint ReadyCount { get; }
    public uint ErrorCount { get; }
    public uint StoppedCount { get; }
    public ulong LastReportedMs { get; }

    internal static unsafe RegistryServiceSummaryEntry FromNative(
        ref ZlinkRegistryServiceSummaryEntry native)
    {
        fixed (byte* serviceName = native.ServiceName)
        {
            return new RegistryServiceSummaryEntry(
                (ServiceKind)native.ServiceKind,
                (ServiceRole)native.ServiceRole,
                NativeHelpers.ReadFixedString(serviceName, 256),
                native.TotalCount, native.ConnectingCount, native.ReadyCount,
                native.ErrorCount, native.StoppedCount, native.LastReportedMs);
        }
    }
}

public readonly struct RegistryTopologyEntry
{
    public RegistryTopologyEntry(string routingId, ServiceKind serviceKind,
        ServiceRole serviceRole, string serviceName, string endpoint,
        TopologySource source, TopologyState state, uint desiredCount,
        uint readyCount, uint errorCode,
        ulong lastReportedMs)
    {
        RoutingId = routingId;
        ServiceKind = serviceKind;
        ServiceRole = serviceRole;
        ServiceName = serviceName;
        Endpoint = endpoint;
        Source = source;
        State = state;
        DesiredCount = desiredCount;
        ReadyCount = readyCount;
        ErrorCode = errorCode;
        LastReportedMs = lastReportedMs;
    }

    public string RoutingId { get; }
    public ServiceKind ServiceKind { get; }
    public ServiceRole ServiceRole { get; }
    public string ServiceName { get; }
    public string Endpoint { get; }
    public TopologySource Source { get; }
    public TopologyState State { get; }
    public uint DesiredCount { get; }
    public uint ReadyCount { get; }
    public uint ErrorCode { get; }
    public ulong LastReportedMs { get; }

    internal static unsafe RegistryTopologyEntry FromNative(
        ref ZlinkRegistryTopologyEntry native)
    {
        fixed (byte* serviceName = native.ServiceName)
        fixed (byte* endpoint = native.Endpoint)
        {
            return new RegistryTopologyEntry(
                RoutingIdCodec.ToPublicString(
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

public readonly struct MemberPeerEntry
{
    public MemberPeerEntry(ServiceType serviceType, ServiceRole serviceRole,
        string serviceName, string endpoint, string routingId, long value)
    {
        ServiceType = serviceType;
        ServiceRole = serviceRole;
        ServiceName = serviceName;
        Endpoint = endpoint;
        RoutingId = routingId;
        Value = value;
    }

    public ServiceType ServiceType { get; }
    public ServiceRole ServiceRole { get; }
    public string ServiceName { get; }
    public string Endpoint { get; }
    public string RoutingId { get; }
    public long Value { get; }

    internal static unsafe MemberPeerEntry FromNative(ref ZlinkMemberPeerEntry native)
    {
        fixed (byte* serviceName = native.ServiceName)
        fixed (byte* endpoint = native.Endpoint)
        {
            return new MemberPeerEntry((ServiceType)native.ServiceType,
                (ServiceRole)native.ServiceRole,
                NativeHelpers.ReadFixedString(serviceName, 256),
                NativeHelpers.ReadFixedString(endpoint, 256),
                RoutingIdCodec.ToPublicString(
                    NativeHelpers.ReadRoutingId(ref native.RoutingId)),
                native.Value);
        }
    }
}

public readonly struct SpotNodeStatus
{
    public SpotNodeStatus(string serviceName, string localEndpoint,
        string nodeRoutingId, SpotNodeState state, uint configuredPeerCount,
        uint activePeerCount, uint connectedPeerCount, uint subjectCount,
        uint readySubjectCount, int lastError, ulong lastChangedMs)
    {
        ServiceName = serviceName;
        LocalEndpoint = localEndpoint;
        NodeRoutingId = nodeRoutingId;
        State = state;
        ConfiguredPeerCount = configuredPeerCount;
        ActivePeerCount = activePeerCount;
        ConnectedPeerCount = connectedPeerCount;
        SubjectCount = subjectCount;
        ReadySubjectCount = readySubjectCount;
        LastError = lastError;
        LastChangedMs = lastChangedMs;
    }

    public string ServiceName { get; }
    public string LocalEndpoint { get; }
    public string NodeRoutingId { get; }
    public SpotNodeState State { get; }
    public uint ConfiguredPeerCount { get; }
    public uint ActivePeerCount { get; }
    public uint ConnectedPeerCount { get; }
    public uint SubjectCount { get; }
    public uint ReadySubjectCount { get; }
    public int LastError { get; }
    public ulong LastChangedMs { get; }

    internal static unsafe SpotNodeStatus FromNative(ref ZlinkSpotNodeStatus native)
    {
        fixed (byte* serviceName = native.ServiceName)
        fixed (byte* endpoint = native.LocalEndpoint)
        {
            return new SpotNodeStatus(
                NativeHelpers.ReadFixedString(serviceName, 256),
                NativeHelpers.ReadFixedString(endpoint, 256),
                RoutingIdCodec.ToPublicString(
                    NativeHelpers.ReadRoutingId(ref native.NodeRoutingId)),
                (SpotNodeState)native.State, native.ConfiguredPeerCount,
                native.ActivePeerCount, native.ConnectedPeerCount,
                native.SubjectCount, native.ReadySubjectCount,
                native.LastError, native.LastChangedMs);
        }
    }
}

public readonly struct SpotNodePeerEntry
{
    public SpotNodePeerEntry(string serviceName, string localEndpoint,
        string peerEndpoint, SpotPeerSource source, SpotPeerState state,
        ulong connectedSinceMs,
        ulong lastChangedMs)
    {
        ServiceName = serviceName;
        LocalEndpoint = localEndpoint;
        PeerEndpoint = peerEndpoint;
        Source = source;
        State = state;
        ConnectedSinceMs = connectedSinceMs;
        LastChangedMs = lastChangedMs;
    }

    public string ServiceName { get; }
    public string LocalEndpoint { get; }
    public string PeerEndpoint { get; }
    public SpotPeerSource Source { get; }
    public SpotPeerState State { get; }
    public ulong ConnectedSinceMs { get; }
    public ulong LastChangedMs { get; }

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
                native.ConnectedSinceMs, native.LastChangedMs);
        }
    }
}

public readonly struct SpotNodeSubjectEntry
{
    public SpotNodeSubjectEntry(SpotSocketRole role, string subject,
        ServiceEventSubjectKind subjectKind,
        uint readyPeerCount, uint activePeerCount, ulong lastChangedMs)
    {
        Role = role;
        Subject = subject;
        SubjectKind = subjectKind;
        ReadyPeerCount = readyPeerCount;
        ActivePeerCount = activePeerCount;
        LastChangedMs = lastChangedMs;
    }

    public SpotSocketRole Role { get; }
    public string Subject { get; }
    public ServiceEventSubjectKind SubjectKind { get; }
    public uint ReadyPeerCount { get; }
    public uint ActivePeerCount { get; }
    public ulong LastChangedMs { get; }

    internal static unsafe SpotNodeSubjectEntry FromNative(
        ref ZlinkSpotNodeSubjectEntry native)
    {
        fixed (byte* subject = native.Subject)
        {
            return new SpotNodeSubjectEntry((SpotSocketRole)native.Role,
                NativeHelpers.ReadFixedString(subject, 256),
                (ServiceEventSubjectKind)native.SubjectKind,
                native.ReadyPeerCount, native.ActivePeerCount,
                native.LastChangedMs);
        }
    }
}

public readonly struct ServiceMonitorEvent
{
    public ServiceMonitorEvent(ServiceKind serviceKind,
        ServiceMonitorEvents eventType,
        int status, int errorCode, uint value,
        ServiceMonitorDetailFlags detailFlags,
        string serviceName, string endpoint, string routingId, string subject,
        ServiceEventSubjectKind subjectKind)
    {
        ServiceKind = serviceKind;
        EventType = eventType;
        Status = status;
        ErrorCode = errorCode;
        Value = value;
        DetailFlags = detailFlags;
        ServiceName = serviceName;
        Endpoint = endpoint;
        RoutingId = routingId;
        Subject = subject;
        SubjectKind = subjectKind;
    }

    public ServiceKind ServiceKind { get; }
    public ServiceMonitorEvents EventType { get; }
    public int Status { get; }
    public int ErrorCode { get; }
    public uint Value { get; }
    public ServiceMonitorDetailFlags DetailFlags { get; }
    public string ServiceName { get; }
    public string Endpoint { get; }
    public string RoutingId { get; }
    public string Subject { get; }
    public ServiceEventSubjectKind SubjectKind { get; }

    internal static unsafe ServiceMonitorEvent FromNative(
        ref ZlinkServiceEvent native)
    {
        fixed (byte* serviceName = native.ServiceName)
        fixed (byte* endpoint = native.Endpoint)
        fixed (byte* subject = native.Subject)
        {
            return new ServiceMonitorEvent((ServiceKind)native.ServiceKind,
                (ServiceMonitorEvents)native.EventType, native.Status,
                native.ErrorCode, native.Value,
                (ServiceMonitorDetailFlags)native.DetailFlags,
                NativeHelpers.ReadFixedString(serviceName, 256),
                NativeHelpers.ReadFixedString(endpoint, 256),
                RoutingIdCodec.ToPublicString(
                    NativeHelpers.ReadRoutingId(ref native.RoutingId)),
                NativeHelpers.ReadFixedString(subject, 256),
                (ServiceEventSubjectKind)native.SubjectKind);
        }
    }
}
