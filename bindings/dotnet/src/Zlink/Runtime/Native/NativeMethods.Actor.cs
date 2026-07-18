// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

// P/Invoke surface for core/include/zlink/service/actor.h (RouteMesh 10.0.0).
internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_new(IntPtr meshNode,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId, IntPtr creationParts,
        nuint creationPartCount, out ZlinkActorRef actor, int flags,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_lookup(IntPtr meshNode,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId,
        ref ZlinkActorLocation location);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_lookup_remote(IntPtr meshNode,
        ref ZlinkRoutingId targetNodeRid,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId,
        out ZlinkMeshOperationId operationId, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_destroy(IntPtr meshNode,
        ref ZlinkActorRef actor, out ZlinkMeshOperationId operationId,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_join_spot(IntPtr meshNode,
        ref ZlinkActorRef actor, ref ZlinkRoutingId targetNodeRid,
        ref ZlinkRoutingId targetSpotRid, ulong targetSpotGeneration,
        IntPtr creationParts, nuint creationPartCount,
        out ZlinkMeshOperationId operationId, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_join_entry_spot(
        IntPtr meshNode, ref ZlinkActorRef actor,
        ref ZlinkRoutingId targetNodeRid, IntPtr creationParts,
        nuint creationPartCount, out ZlinkMeshOperationId operationId,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_leave_spot(IntPtr meshNode,
        ref ZlinkActorRef actor, ulong expectedMembershipEpoch,
        out ZlinkMeshOperationId operationId, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_actor_join_reply(ref ZlinkMeshReplyToken token,
        int joinResult, IntPtr parts, nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_send_to_actor(IntPtr meshNode,
        ref ZlinkActorRef actor, IntPtr actorMetadata, IntPtr parts,
        nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_request_to_actor(IntPtr meshNode,
        ref ZlinkActorRef actor, IntPtr actorMetadata, IntPtr parts,
        nuint partCount, out ZlinkMeshOperationId operationId, int flags,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_actor_send_to_actor(IntPtr meshNode,
        ref ZlinkActorRef sourceActor, ref ZlinkActorRef targetActor,
        IntPtr actorMetadata, IntPtr parts, nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_actor_request_to_actor(IntPtr meshNode,
        ref ZlinkActorRef sourceActor, ref ZlinkActorRef targetActor,
        IntPtr actorMetadata, IntPtr parts, nuint partCount,
        out ZlinkMeshOperationId operationId, int flags, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_transfer_prepare(
        IntPtr meshNode, ref ZlinkActorTransferPrepare prepare, uint timeoutMs,
        out ZlinkActorTransferToken token,
        ref ZlinkActorTransferPrepareResult result);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_transfer_commit(
        in ZlinkActorTransferToken token, ulong newMembershipEpoch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_transfer_activate(
        in ZlinkActorTransferToken token);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_transfer_abort(
        in ZlinkActorTransferToken token);
}
