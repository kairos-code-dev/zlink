// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

// P/Invoke surface for core/include/zlink/service/mesh_node.h (RouteMesh 10.0.0).
internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_mesh_node_new(IntPtr ctx, IntPtr options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "zlink_mesh_node_new")]
    internal static extern IntPtr zlink_mesh_node_new(IntPtr ctx,
        ref ZlinkMeshNodeOptions options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_set_bind(IntPtr meshNode,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_start(IntPtr meshNode);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_shutdown(IntPtr meshNode,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_destroy(ref IntPtr meshNode);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_add_channel_name(IntPtr meshNode,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_set_channel_weight(IntPtr meshNode,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName, uint weight);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_connect_peer(IntPtr meshNode,
        ref ZlinkMeshPeerConnectionOptions options,
        out ulong connectionIntentId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_remove_peer_connection(
        IntPtr meshNode, ulong connectionIntentId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_disconnect_peer(IntPtr meshNode,
        ref ZlinkRoutingId peerRid, ulong lifecycleGeneration);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_send_to_node(IntPtr meshNode,
        ref ZlinkRoutingId targetRid, IntPtr metadata, IntPtr parts,
        nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_request_to_node(IntPtr meshNode,
        ref ZlinkRoutingId targetRid, IntPtr metadata, IntPtr parts,
        nuint partCount, out ZlinkMeshOperationId operationId, int flags,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_send_to_channel(IntPtr meshNode,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName, IntPtr metadata,
        IntPtr parts, nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_request_to_channel(IntPtr meshNode,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName, IntPtr metadata,
        IntPtr parts, nuint partCount, out ZlinkMeshOperationId operationId,
        int flags, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_mesh_node_publisher_new(IntPtr meshNode);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_publisher_publish(IntPtr publisher,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? topic, IntPtr metadata,
        IntPtr parts, nuint partCount, IntPtr detail, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_publisher_destroy(
        ref IntPtr publisher);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_mesh_node_option(IntPtr meshNode,
        int option, IntPtr optval, nuint optvallen);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_get_mesh_node_option(IntPtr meshNode,
        int option, IntPtr optval, ref nuint optvallen);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_status(IntPtr meshNode,
        ref ZlinkMeshNodeStatus status);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_peers(IntPtr meshNode,
        IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_peer_channels(IntPtr meshNode,
        ref ZlinkRoutingId peerRid, ulong lifecycleGeneration,
        IntPtr channelNamesOut, IntPtr weightsOut, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_mesh_node_monitor_open(IntPtr meshNode,
        ref ZlinkMeshMonitorOpenOptions options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_monitor_recv(IntPtr monitor,
        ref ZlinkMeshMonitorEvent eventOut, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_monitor_status(IntPtr monitor,
        ref ZlinkMeshMonitorStatus statusOut);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_monitor_close(ref IntPtr monitor);
}
