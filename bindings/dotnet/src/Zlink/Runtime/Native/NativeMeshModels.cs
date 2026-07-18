// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

// Native struct layouts for the RouteMesh 10.0.0 service C API
// (core/include/zlink/service/{mesh_node,dispatch,actor,spot,stream_session,common}.h).
// All fixed buffers use the header's MAX+1 sizing.

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkMeshNodeOptions
{
    public uint StructSize;
    public uint Version;
    public IntPtr MeshName;
    public nuint MeshNameSize;
    public IntPtr TrustProfile;
    public nuint TrustProfileSize;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkMeshPeerConnectionOptions
{
    public uint StructSize;
    public uint Version;
    public IntPtr Endpoint;
    public nuint EndpointSize;
    public uint HasExpectedRid;
    public ZlinkRoutingId ExpectedRid;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkMeshMetadataView
{
    public IntPtr Data;
    public nuint Size;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkMeshPublishDetail
{
    public uint StructSize;
    public uint Version;
    public uint SnapshotRemoteTargetCount;
    public uint AdmittedRemoteTargetCount;
    public uint DroppedRemoteTargetCount;
    public uint UnreachableRemoteTargetCount;
    public uint SnapshotLocalSpotCount;
    public uint AdmittedLocalSpotCount;
    public uint DroppedLocalSpotCount;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkMeshNodeStatus
{
    public uint StructSize;
    public uint Version;
    public int State;
    public ZlinkRoutingId RoutingId;
    public fixed byte MeshName[256];
    public fixed byte LocalEndpoint[512];
    public ulong LifecycleGeneration;
    public ulong DescriptorRevision;
    public uint ChannelCount;
    public uint ConfiguredPeerCount;
    public uint AdmittedPeerCount;
    public uint DrainingPeerCount;
    public ulong PendingApplicationMessages;
    public ulong PendingInfrastructureMessages;
    public ulong PendingBytes;
    public ulong MulticastSubmitted;
    public ulong MulticastDroppedTargets;
    public int LastError;
    public ulong LastChangedMs;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkMeshPeerEntry
{
    public uint StructSize;
    public uint Version;
    public ulong ConnectionIntentId;
    public int Source;
    public int State;
    public ZlinkRoutingId RoutingId;
    public ulong LifecycleGeneration;
    public ulong DescriptorRevision;
    public fixed byte Endpoint[512];
    public uint ChannelCount;
    public int LastError;
    public ulong LastChangedMs;
}

// --- dispatch.h ---

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkMeshOperationId
{
    public ulong High;
    public ulong Low;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkMeshReplyToken
{
    public fixed ulong Opaque[4];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkMeshClaim
{
    public fixed ulong Opaque[4];
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkMeshReadyRecord
{
    public uint StructSize;
    public uint Version;
    public int OwnerKind;
    public uint Domain;
    public ZlinkRoutingId SpotRid;
    public ZlinkActorRef Actor;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkMeshReceiveRequirements
{
    public uint StructSize;
    public uint Version;
    public nuint MessageCount;
    public nuint PartCount;
    public nuint ByteCount;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkMeshReceiveRecord
{
    public uint StructSize;
    public uint Version;
    public int Kind;
    public uint Domain;
    public ZlinkRoutingId SourceNodeRid;
    public ZlinkRoutingId SourceSpotRid;
    public ZlinkActorRef SourceActor;
    public ZlinkMeshOperationId OperationId;
    public int OperationKind;
    public ZlinkMeshReplyToken ReplyToken;
    public IntPtr ChannelName;
    public nuint ChannelNameSize;
    public IntPtr Topic;
    public nuint TopicSize;
    public IntPtr ApplicationMetadata;
    public nuint ApplicationMetadataSize;
    public IntPtr KindData;
    public nuint KindDataSize;
    public nuint PartOffset;
    public nuint PartCount;
    public int TerminalResult;
    public int FailureErrno;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkMeshSendReadyData
{
    public uint StructSize;
    public uint Version;
    public int DestinationKind;
    public ZlinkRoutingId TargetNodeRid;
    public ZlinkRoutingId TargetSpotRid;
    public ZlinkActorRef TargetActor;
    public IntPtr ChannelName;
    public nuint ChannelNameSize;
}

// --- common.h / actor.h ---

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkActorRef
{
    public ZlinkRoutingId NodeRid;
    public fixed byte ActorId[256];
    public ulong Generation;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorLocation
{
    public uint StructSize;
    public uint Version;
    public ZlinkActorRef Actor;
    public ZlinkRoutingId SpotRid;
    public ulong SpotGeneration;
    public ulong MembershipEpoch;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorControlRecord
{
    public uint StructSize;
    public uint Version;
    public int Kind;
    public ZlinkActorRef PreviousActor;
    public ZlinkActorRef CurrentActor;
    public ZlinkRoutingId PreviousSpotRid;
    public ZlinkRoutingId CurrentSpotRid;
    public ulong PreviousSpotGeneration;
    public ulong CurrentSpotGeneration;
    public ulong PreviousMembershipEpoch;
    public ulong CurrentMembershipEpoch;
    public int ResultCode;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorJoinCompletion
{
    public uint StructSize;
    public uint Version;
    public int JoinResult;
    public ZlinkActorRef Actor;
    public ZlinkActorLocation Location;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorTransferId
{
    public ulong High;
    public ulong Low;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkActorTransferToken
{
    public fixed ulong Opaque[8];
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorTransferPrepare
{
    public uint StructSize;
    public uint Version;
    public int Role;
    public ZlinkActorTransferId TransferId;
    public ZlinkActorRef Actor;
    public ulong ExpectedMembershipEpoch;
    public ZlinkRoutingId PeerNodeRid;
    public ulong FinalSequence;
    public ulong ReserveMessageCount;
    public ulong ReserveByteCount;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorTransferPrepareResult
{
    public uint StructSize;
    public uint Version;
    public int Role;
    public ZlinkActorTransferId TransferId;
    public ZlinkActorRef Actor;
    public ulong FinalSequence;
    public ulong ReserveMessageCount;
    public ulong ReserveByteCount;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkActorTransferControl
{
    public uint StructSize;
    public uint Version;
    public int Phase;
    public int Role;
    public ZlinkActorTransferId TransferId;
    public ZlinkActorRef Actor;
    public ulong MembershipEpoch;
    public ulong FinalSequence;
    public int ResultCode;
    public int FailureErrno;
}

// --- spot.h ---

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkSpotStatus
{
    public uint StructSize;
    public uint Version;
    public ZlinkRoutingId SpotRid;
    public int SpotKind;
    public ulong LifecycleGeneration;
    public ulong PendingApplicationMessages;
    public ulong PendingInfrastructureMessages;
    public ulong PendingBytes;
    public uint ActiveActorCount;
    public uint Draining;
    public int LastError;
    public ulong LastChangedMs;
}

// --- stream_session.h ---

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkStreamSessionBinding
{
    public uint StructSize;
    public uint Version;
    public ZlinkRoutingId SessionRid;
    public ZlinkActorRef Actor;
    public ulong BindingGeneration;
    public ulong MembershipEpoch;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkStreamSessionStatus
{
    public uint StructSize;
    public uint Version;
    public int State;
    public ulong LifecycleGeneration;
    public ulong SessionCount;
    public ulong BindingCount;
    public ulong PendingMessageCount;
    public ulong PendingByteCount;
    public int LastError;
}
