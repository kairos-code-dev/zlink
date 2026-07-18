// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

// P/Invoke surface for core/include/zlink/service/spot.h (RouteMesh 10.0.0).
internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_new(IntPtr meshNode);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_entry_spot(IntPtr meshNode,
        out IntPtr spot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_spot_lookup(IntPtr meshNode,
        ref ZlinkRoutingId spotRid, out IntPtr spot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_spot_get_or_new(IntPtr meshNode,
        ref ZlinkRoutingId spotRid, out IntPtr spot, out uint created);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_destroy(ref IntPtr spot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_status(IntPtr spot,
        ref ZlinkSpotStatus status);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_send_to_channel(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName, IntPtr metadata,
        IntPtr parts, nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_request_to_channel(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName, IntPtr metadata,
        IntPtr parts, nuint partCount, out ZlinkMeshOperationId operationId,
        int flags, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_send_to_spot(IntPtr spot,
        ref ZlinkRoutingId targetNodeRid, ref ZlinkRoutingId targetSpotRid,
        ulong targetSpotGeneration, IntPtr metadata, IntPtr parts,
        nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_request_to_spot(IntPtr spot,
        ref ZlinkRoutingId targetNodeRid, ref ZlinkRoutingId targetSpotRid,
        ulong targetSpotGeneration, IntPtr metadata, IntPtr parts,
        nuint partCount, out ZlinkMeshOperationId operationId, int flags,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_publish(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? topic, IntPtr metadata,
        IntPtr parts, nuint partCount, IntPtr detail, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_set_publish_option(IntPtr spot,
        int option, IntPtr optval, nuint optvallen);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_get_publish_option(IntPtr spot,
        int option, IntPtr optval, ref nuint optvallen);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_set_subscription(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicFilter, int kind);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_unset_subscription(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicFilter, int kind);
}
