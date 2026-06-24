// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

internal static partial class NativeMethods
{
    internal const uint SpotRouteBridgeCapSpotRoute = 0x00000001u;
    internal const uint SpotRouteBridgeRouteOnly = SpotRouteBridgeCapSpotRoute;

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_route_bridge_new(
        IntPtr ctx, IntPtr spotNode, IntPtr options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "zlink_spot_route_bridge_new")]
    internal static extern IntPtr zlink_spot_route_bridge_new(
        IntPtr ctx, IntPtr spotNode, ref ZlinkSpotRouteBridgeOptions options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_route_bridge_attach_router_channel(
        IntPtr bridge,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        IntPtr routerSocket,
        ref ZlinkSpotRouteBridgeEndpointOptions options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_route_bridge_send(
        IntPtr bridge,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        ref ZlinkRoutingId targetNodeRid,
        ref ZlinkRoutingId targetSpotRid,
        IntPtr parts,
        nuint partCount,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_route_bridge_request(
        IntPtr bridge,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        ref ZlinkRoutingId targetNodeRid,
        ref ZlinkRoutingId targetSpotRid,
        IntPtr parts,
        nuint partCount,
        IntPtr callback,
        IntPtr userData,
        int flags,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_route_bridge_handle_router_received(
        IntPtr bridge,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        ref ZlinkRoutingId sourceNodeRid,
        ulong requestSeq,
        IntPtr parts,
        nuint partCount,
        [MarshalAs(UnmanagedType.I1)] out bool handled);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_route_bridge_drain(IntPtr bridge);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_route_bridge_close(IntPtr bridge);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_node_publisher_new(IntPtr spotNode);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_publisher_publish(
        IntPtr publisher,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topic,
        IntPtr parts,
        nuint partCount,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_publisher_close(IntPtr publisher);
}
