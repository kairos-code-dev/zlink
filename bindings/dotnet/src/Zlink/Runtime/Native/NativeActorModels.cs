using System;
using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

internal static class NativeConstants
{
    internal const int ActorIdMax = 256;
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
internal struct ZlinkActorJoinResult
{
    public int Result;
    public int JoinResultCode;
    public ZlinkActorRef Actor;
    public ZlinkRoutingId JoinedSpotRid;
    public ulong JoinEpoch;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorJoinEntrySpotResult
{
    public int Result;
    public ZlinkActorRef Actor;
    public ZlinkRoutingId TargetNodeRid;
    public ulong JoinEpoch;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorLookupResult
{
    public int Result;
    public ZlinkActorRef Actor;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkSpotActorLifecycleInfo
{
    public ZlinkActorRef PreviousActor;
    public ZlinkActorRef CurrentActor;
    public ZlinkRoutingId PreviousSpotRid;
    public ZlinkRoutingId CurrentSpotRid;
    public ulong JoinEpoch;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkSpotActorLifecycleEvent
{
    public int Kind;
    public ZlinkSpotActorLifecycleInfo Info;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorRoute
{
    public ZlinkActorRef Actor;
    public ZlinkRoutingId CurrentSpotRid;
    public int CurrentSpotKind;
}
