// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

// P/Invoke surface for core/include/zlink/service/stream_session.h
// (RouteMesh 10.0.0).
internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_stream_session_service_new(
        IntPtr meshNode, IntPtr stream);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_session_service_start(IntPtr service);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_session_service_shutdown(
        IntPtr service, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_session_service_destroy(
        ref IntPtr service);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_session_service_status(
        IntPtr service, ref ZlinkStreamSessionStatus status);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_session_bind_actor(IntPtr service,
        ref ZlinkRoutingId sessionRid, ref ZlinkActorRef actor,
        out ZlinkMeshOperationId operationId, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_session_unbind_actor(IntPtr service,
        ref ZlinkRoutingId sessionRid, ref ZlinkActorRef actor,
        ulong expectedBindingGeneration, out ZlinkMeshOperationId operationId,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_session_bindings(IntPtr service,
        ref ZlinkRoutingId sessionRid, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_session_send_to_actor(IntPtr service,
        ref ZlinkRoutingId sessionRid, ref ZlinkActorRef actor,
        IntPtr actorMetadata, IntPtr parts, nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_session_request_to_actor(
        IntPtr service, ref ZlinkRoutingId sessionRid, ref ZlinkActorRef actor,
        IntPtr actorMetadata, IntPtr parts, nuint partCount,
        out ZlinkMeshOperationId operationId, int flags, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_send_bound_session(
        IntPtr meshNode, ref ZlinkActorRef actor, IntPtr parts, nuint partCount,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_actor_close_bound_session(
        IntPtr meshNode, ref ZlinkActorRef actor,
        ulong expectedBindingGeneration, out ZlinkMeshOperationId operationId,
        uint timeoutMs);
}
