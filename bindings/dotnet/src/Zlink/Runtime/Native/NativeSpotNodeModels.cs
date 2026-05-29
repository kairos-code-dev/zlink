using System.Runtime.InteropServices;

namespace Systems.Zlink.Native;

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotNodeStatus
{
    public fixed byte ChannelName[256];
    public fixed byte LocalEndpoint[256];
    public ZlinkRoutingId NodeRoutingId;
    public int State;
    public uint ConfiguredPeerCount;
    public uint ActivePeerCount;
    public uint ConnectedPeerCount;
    public uint SubjectCount;
    public uint ReadySubjectCount;
    public uint DisconnectedSubTargetCount;
    public uint DisconnectedRoutedTargetCount;
    public int LastError;
    public ulong LastChangedMs;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotNodePeerEntry
{
    public fixed byte ChannelName[256];
    public fixed byte LocalEndpoint[256];
    public fixed byte PeerEndpoint[256];
    public int Source;
    public int Kind;
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
internal struct ZlinkSpotNodeOptions
{
    public global::Systems.Zlink.SpotNodeMode Mode;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotNodeSocketFilter
{
    public global::Systems.Zlink.SpotNodeSocketOwner Owner;
    public global::Systems.Zlink.SpotNodeSocketType SocketType;
    public fixed byte SocketName[64];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotNodeSocketEntry
{
    public global::Systems.Zlink.SpotNodeSocketOwner Owner;
    public ulong OwnerId;
    public fixed byte OwnerName[64];
    public fixed byte SocketName[64];
    public global::Systems.Zlink.SpotNodeSocketType SocketType;
    public uint AutoHwmVisible;
    public ZlinkMonitorStatus MonitorStatus;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkSpotNodeSpotEntry
{
    public ZlinkRoutingId SpotRid;
    public int SpotKind;
    public uint DispatchHandlerAttached;
    public uint JoinedActorCount;
    public uint PendingActorJoinCount;
    public uint RouteSynced;
    public ulong LastChangedMs;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkSpotNodeActorEntry
{
    public ZlinkActorRef Actor;
    public ZlinkRoutingId CurrentSpotRid;
    public int CurrentSpotKind;
    public uint RouteSynced;
    public uint PendingMessageCount;
    public ulong LastChangedMs;
}
