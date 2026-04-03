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
