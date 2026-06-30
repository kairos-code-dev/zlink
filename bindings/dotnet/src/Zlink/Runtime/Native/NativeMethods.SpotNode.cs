using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_node_new(IntPtr ctx, IntPtr options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "zlink_spot_node_new")]
    internal static extern IntPtr zlink_spot_node_new(IntPtr ctx,
        ref ZlinkSpotNodeOptions options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_destroy(ref IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_set_pub_bind(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_set_pub_routing_id(
        IntPtr node, IntPtr data, nuint size);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_set_sub_routing_id(
        IntPtr node, IntPtr data, nuint size);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_set_router_bind(
        IntPtr node, [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_connect_peer(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_connect_peer_rid(IntPtr node,
        ref ZlinkRoutingId targetNodeRid,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_disconnect_peer(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_disconnect_peer_rid(IntPtr node,
        ref ZlinkRoutingId targetNodeRid);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_attach_discovery(IntPtr node,
        IntPtr discovery);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_spot_node_option(IntPtr node,
        SpotNodeOption option, IntPtr value, nuint valueSize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_get_spot_node_option(IntPtr node,
        SpotNodeOption option, IntPtr value, ref nuint valueSize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_entry_spot(IntPtr node,
        out IntPtr spot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_spot_lookup(IntPtr node,
        ref ZlinkRoutingId spotRid, out IntPtr spot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_spot_get_or_new(IntPtr node,
        ref ZlinkRoutingId spotRid, out IntPtr spot, out uint created);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_status(IntPtr node,
        out ZlinkSpotNodeStatus status);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_peers(IntPtr node,
        IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_subjects(IntPtr node,
        IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_internal_sockets(
        IntPtr node, IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_spots(IntPtr node,
        IntPtr entries,
        ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actors(IntPtr node,
        IntPtr entries,
        ref nuint count);
}