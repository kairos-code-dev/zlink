using System.Runtime.InteropServices;

namespace Zlink.Native;

internal static class NativeConstants
{
    internal const int ActorIdMax = 256;
}

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
    public uint AutoHwmProfile;
    public uint AutoHwmRole;
    public uint AutoHwmPolicyClass;
    public ulong AutoHwmUnitBudgetBytes;
    public uint AutoHwmSizeCap;
    public ulong AutoHwmSocketMessageSlots;
    public ulong AutoHwmEffectiveMessageBytes;
    public int AutoHwmAppliedSndHwm;
    public int AutoHwmAppliedRcvHwm;
    public ulong AutoHwmLastRecalcMs;
    public uint AutoHwmLastRecalcReason;
    public uint AutoHwmSendBlockedRatioPpm;
    public int AutoHwmDeferredSndHwm;
    public int AutoHwmDeferredRcvHwm;
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
    public uint DisconnectedSubTargetCount;
    public uint DisconnectedRoutedTargetCount;
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
internal struct ZlinkSpotNodeOptions
{
    public global::Zlink.SpotNodeMode Mode;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotNodeSocketSnapshotFilter
{
    public global::Zlink.SpotNodeSocketOwner Owner;
    public global::Zlink.SpotNodeSocketType SocketType;
    public fixed byte SocketName[64];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotNodeSocketSnapshotEntry
{
    public global::Zlink.SpotNodeSocketOwner Owner;
    public ulong OwnerId;
    public fixed byte OwnerName[64];
    public fixed byte SocketName[64];
    public global::Zlink.SpotNodeSocketType SocketType;
    public uint AutoHwmVisible;
    public ZlinkMonitorSnapshot Snapshot;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkActorRef
{
    public ZlinkRoutingId NodeRid;
    public fixed byte ActorId[NativeConstants.ActorIdMax];
    public ulong Generation;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorRecvInfo
{
    public ZlinkActorRef Actor;
    public ZlinkRoutingId SourceNodeRid;
    public ZlinkRoutingId SourceSessionRid;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorJoinInfo
{
    public ZlinkActorRef SourceActor;
    public ZlinkActorRef TargetActor;
    public ZlinkRoutingId SourceNodeRid;
    public ZlinkRoutingId SourceSpotRid;
    public ZlinkRoutingId TargetNodeRid;
    public ZlinkRoutingId TargetSpotRid;
    public ulong JoinEpoch;
    public IntPtr Request;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorCreateResult
{
    public global::Zlink.ActorCreateStatus Status;
    public ZlinkActorRef Actor;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorRoute
{
    public ZlinkActorRef Actor;
    public uint Joined;
    public ZlinkRoutingId JoinedSpotRid;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkSpotNodeSpotEntry
{
    public ZlinkRoutingId SpotRid;
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
    public uint Joined;
    public ZlinkRoutingId JoinedSpotRid;
    public uint RouteSynced;
    public uint PendingMessageCount;
    public ulong LastChangedMs;
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
