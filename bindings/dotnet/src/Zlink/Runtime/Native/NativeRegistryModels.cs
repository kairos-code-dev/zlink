using System.Runtime.InteropServices;

namespace Systems.Zlink.Native;

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
    public int AutoConnectType;
    public int ServiceRole;
    public fixed byte ChannelName[256];
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
    public int AutoConnectType;
    public int ServiceRole;
    public fixed byte ChannelName[256];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkMemberPeerEntry
{
    public int AutoConnectType;
    public int ServiceRole;
    public fixed byte ChannelName[256];
    public fixed byte Endpoint[256];
    public uint Weight;
    public ZlinkRoutingId RoutingId;
    public long Value;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkRegistryTopologyEntry
{
    public int AutoConnectType;
    public ZlinkRoutingId RoutingId;
    public int ServiceKind;
    public int ServiceRole;
    public fixed byte ChannelName[256];
    public fixed byte Endpoint[256];
    public int Source;
    public int State;
    public uint DesiredCount;
    public uint ReadyCount;
    public uint ErrorCode;
    public ulong LastReportedMs;
    public int SpotKind;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkRegistryTopologyFilter
{
    public int AutoConnectType;
    public int ServiceKind;
    public int ServiceRole;
    public fixed byte ChannelName[256];
    public ZlinkRoutingId RoutingId;
    public int State;
    public int Source;
}
