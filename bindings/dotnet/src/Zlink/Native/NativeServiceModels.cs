using System.Runtime.InteropServices;

namespace Zlink.Native;

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkSocketMonitorOpenOptions
{
    public uint Events;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkMonitorSnapshot
{
    public int SourceKind;
    public uint StateFlags;
    public uint DetailFlags;
    public ulong SndPendingMsgs;
    public ulong RcvPendingMsgs;
    public uint AutoHwmEnabled;
    public uint AutoHwmRole;
    public uint AutoHwmManagedConnections;
    public uint AutoHwmActiveHwmConnections;
    public uint AutoHwmPlanningTransportConnections;
    public uint AutoHwmBaseFloorPerConnection;
    public int AutoHwmAppliedSndHwm;
    public int AutoHwmAppliedRcvHwm;
    public int AutoHwmRequestedSndBuf;
    public int AutoHwmRequestedRcvBuf;
    public int AutoHwmEffectiveSndBuf;
    public int AutoHwmEffectiveRcvBuf;
    public ulong AutoHwmTotalMemoryBudgetBytes;
    public ulong AutoHwmQueueBudgetBytes;
    public ulong AutoHwmTransportBudgetBytes;
    public ulong AutoHwmRuntimeReserveBytes;
    public ulong AutoHwmGroupBudgetBytes;
    public ulong AutoHwmGroupMessageSlots;
    public ulong AutoHwmEffectiveMessageBytes;
    public ulong AutoHwmControlBudgetBytes;
    public ulong AutoHwmRoutedBudgetBytes;
    public ulong AutoHwmFanoutBudgetBytes;
    public ulong AutoHwmRecvIngressBudgetBytes;
    public uint AutoHwmControlActiveConnections;
    public uint AutoHwmRoutedActiveConnections;
    public uint AutoHwmFanoutActiveConnections;
    public uint AutoHwmRecvIngressActiveConnections;
    public ulong AutoHwmEstimatedMaxMemoryBytes;
    public ulong AutoHwmLastRecalcMs;
    public uint AutoHwmLastRecalcReason;
    public uint AutoHwmSendBlockedRatioPpm;
    public uint AutoHwmScope;
    public uint AutoHwmScopeCount;
    public ulong AutoHwmRoleGroupBudgetBytes;
    public ulong AutoHwmScopeGroupBudgetBytes;
    public ulong AutoHwmAutoBufferBytes;
    public ulong AutoHwmManualBufferBytes;
    public uint AutoHwmBufferConnections;
    public int AutoHwmDeferredSndHwm;
    public int AutoHwmDeferredRcvHwm;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkServiceMonitorOpenOptions
{
    public uint Events;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkServiceEvent
{
    public int ServiceKind;
    public uint EventType;
    public int Status;
    public int ErrorCode;
    public uint Value;
    public uint DetailFlags;
    public fixed byte ServiceName[256];
    public fixed byte Endpoint[256];
    public ZlinkRoutingId RoutingId;
    public fixed byte Subject[256];
    public uint SubjectKind;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotNodeStatus
{
    public fixed byte ServiceName[256];
    public fixed byte LocalEndpoint[256];
    public ZlinkRoutingId NodeRoutingId;
    public int State;
    public uint ConfiguredPeerCount;
    public uint ActivePeerCount;
    public uint ConnectedPeerCount;
    public uint SubjectCount;
    public uint ReadySubjectCount;
    public int LastError;
    public ulong LastChangedMs;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotNodePeerEntry
{
    public fixed byte ServiceName[256];
    public fixed byte LocalEndpoint[256];
    public fixed byte PeerEndpoint[256];
    public int Source;
    public int State;
    public uint Weight;
    public ulong ConnectedSinceMs;
    public ulong LastChangedMs;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotNodePeerFilter
{
    public fixed byte PeerEndpoint[256];
    public int Source;
    public int State;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotNodeSubjectEntry
{
    public int Role;
    public fixed byte Subject[256];
    public uint SubjectKind;
    public uint ReadyPeerCount;
    public uint ActivePeerCount;
    public ulong LastChangedMs;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotNodeSubjectFilter
{
    public int Role;
    public fixed byte Subject[256];
    public uint SubjectKind;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkRegistryStatus
{
    public uint RegistryId;
    public fixed byte BindEndpoint[256];
    public int State;
    public uint TopologyEntryCount;
    public uint PeerRegistryCount;
    public uint ConnectedPeerRegistryCount;
    public ulong ListSeq;
    public int LastError;
    public ulong LastChangedMs;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkRegistryServiceSummaryEntry
{
    public int ServiceKind;
    public int ServiceRole;
    public fixed byte ServiceName[256];
    public uint TotalCount;
    public uint ConnectingCount;
    public uint ReadyCount;
    public uint ErrorCount;
    public uint StoppedCount;
    public ulong LastReportedMs;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkRegistryServiceSummaryFilter
{
    public int ServiceKind;
    public int ServiceRole;
    public fixed byte ServiceName[256];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkMemberPeerEntry
{
    public int ServiceType;
    public ushort ServiceRole;
    public fixed byte ServiceName[256];
    public fixed byte Endpoint[256];
    public ZlinkRoutingId RoutingId;
    public uint Weight;
    public long Value;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkRegistryTopologyEntry
{
    public ZlinkRoutingId RoutingId;
    public int ServiceKind;
    public int ServiceRole;
    public fixed byte ServiceName[256];
    public fixed byte Endpoint[256];
    public int Source;
    public int State;
    public uint DesiredCount;
    public uint ReadyCount;
    public uint ErrorCode;
    public ulong LastReportedMs;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkRegistryTopologyFilter
{
    public int ServiceKind;
    public int ServiceRole;
    public fixed byte ServiceName[256];
    public ZlinkRoutingId RoutingId;
    public int State;
    public int Source;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotServiceAttachmentStats
{
    public fixed byte ServiceName[256];
    public uint RouterCount;
    public uint PubCount;
    public uint SubCount;
    public uint AutoRouterCount;
    public uint AutoPubCount;
    public uint AutoSubCount;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotServiceMonitorEvent
{
    public fixed byte ServiceName[256];
    public int Role;
    public ZlinkMonitorEvent Event;
}
