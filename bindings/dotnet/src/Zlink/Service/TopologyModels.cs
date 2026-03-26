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

public readonly struct RegistryStatus
{
    public RegistryStatus(uint registryId, string bindEndpoint, int state,
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
    public int State { get; }
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
                NativeHelpers.ReadFixedString(endpoint, 256), native.State,
                native.TopologyEntryCount, native.PeerRegistryCount,
                native.ConnectedPeerRegistryCount, native.ListSeq,
                native.LastError, native.LastChangedMs);
        }
    }
}

public readonly struct RegistryServiceSummaryEntry
{
    public RegistryServiceSummaryEntry(ServiceKind serviceKind,
        ushort serviceRole, string serviceName, uint totalCount,
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
    public ushort ServiceRole { get; }
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
                (ServiceKind)native.ServiceKind, (ushort)native.ServiceRole,
                NativeHelpers.ReadFixedString(serviceName, 256),
                native.TotalCount, native.ConnectingCount, native.ReadyCount,
                native.ErrorCount, native.StoppedCount, native.LastReportedMs);
        }
    }
}

public readonly struct RegistryTopologyEntry
{
    public RegistryTopologyEntry(string routingId, ServiceKind serviceKind,
        ushort serviceRole, string serviceName, string endpoint, int source,
        int state, uint desiredCount, uint readyCount, uint errorCode,
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
    public ushort ServiceRole { get; }
    public string ServiceName { get; }
    public string Endpoint { get; }
    public int Source { get; }
    public int State { get; }
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
                (ServiceKind)native.ServiceKind, (ushort)native.ServiceRole,
                NativeHelpers.ReadFixedString(serviceName, 256),
                NativeHelpers.ReadFixedString(endpoint, 256), native.Source,
                native.State, native.DesiredCount, native.ReadyCount,
                native.ErrorCode, native.LastReportedMs);
        }
    }
}

public readonly struct MemberPeerEntry
{
    public MemberPeerEntry(ServiceType serviceType, ushort serviceRole,
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
    public ushort ServiceRole { get; }
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
                native.ServiceRole,
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
        string nodeRoutingId, int state, uint configuredPeerCount,
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
    public int State { get; }
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
                native.State, native.ConfiguredPeerCount,
                native.ActivePeerCount, native.ConnectedPeerCount,
                native.SubjectCount, native.ReadySubjectCount,
                native.LastError, native.LastChangedMs);
        }
    }
}

public readonly struct SpotNodePeerEntry
{
    public SpotNodePeerEntry(string serviceName, string localEndpoint,
        string peerEndpoint, int source, int state, ulong connectedSinceMs,
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
    public int Source { get; }
    public int State { get; }
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
                NativeHelpers.ReadFixedString(peer, 256), native.Source,
                native.State, native.ConnectedSinceMs, native.LastChangedMs);
        }
    }
}

public readonly struct SpotNodeSubjectEntry
{
    public SpotNodeSubjectEntry(int role, string subject, uint subjectKind,
        uint readyPeerCount, uint activePeerCount, ulong lastChangedMs)
    {
        Role = role;
        Subject = subject;
        SubjectKind = subjectKind;
        ReadyPeerCount = readyPeerCount;
        ActivePeerCount = activePeerCount;
        LastChangedMs = lastChangedMs;
    }

    public int Role { get; }
    public string Subject { get; }
    public uint SubjectKind { get; }
    public uint ReadyPeerCount { get; }
    public uint ActivePeerCount { get; }
    public ulong LastChangedMs { get; }

    internal static unsafe SpotNodeSubjectEntry FromNative(
        ref ZlinkSpotNodeSubjectEntry native)
    {
        fixed (byte* subject = native.Subject)
        {
            return new SpotNodeSubjectEntry(native.Role,
                NativeHelpers.ReadFixedString(subject, 256), native.SubjectKind,
                native.ReadyPeerCount, native.ActivePeerCount,
                native.LastChangedMs);
        }
    }
}

public readonly struct ServiceMonitorEvent
{
    public ServiceMonitorEvent(ServiceKind serviceKind, uint eventType,
        int status, int errorCode, uint value, uint detailFlags,
        string serviceName, string endpoint, string routingId, string subject,
        uint subjectKind)
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
    public uint EventType { get; }
    public int Status { get; }
    public int ErrorCode { get; }
    public uint Value { get; }
    public uint DetailFlags { get; }
    public string ServiceName { get; }
    public string Endpoint { get; }
    public string RoutingId { get; }
    public string Subject { get; }
    public uint SubjectKind { get; }

    internal static unsafe ServiceMonitorEvent FromNative(
        ref ZlinkServiceEvent native)
    {
        fixed (byte* serviceName = native.ServiceName)
        fixed (byte* endpoint = native.Endpoint)
        fixed (byte* subject = native.Subject)
        {
            return new ServiceMonitorEvent((ServiceKind)native.ServiceKind,
                native.EventType, native.Status, native.ErrorCode,
                native.Value, native.DetailFlags,
                NativeHelpers.ReadFixedString(serviceName, 256),
                NativeHelpers.ReadFixedString(endpoint, 256),
                RoutingIdCodec.ToPublicString(
                    NativeHelpers.ReadRoutingId(ref native.RoutingId)),
                NativeHelpers.ReadFixedString(subject, 256),
                native.SubjectKind);
        }
    }
}
