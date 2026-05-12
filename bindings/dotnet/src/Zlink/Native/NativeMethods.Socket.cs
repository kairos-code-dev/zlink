using System;
using System.Runtime.InteropServices;

namespace Systems.Zlink.Native;

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkSocketMsgHandlerDelegate(
        IntPtr sourceRoutingId,
        IntPtr parts,
        nuint partCount,
        IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal unsafe delegate void ZlinkSubscribeHandlerDelegate(
        IntPtr sourceRoutingId,
        byte* topic,
        nuint topicLen,
        IntPtr parts,
        nuint partCount,
        IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkSendReadyHandlerDelegate(IntPtr subject,
        IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_socket(IntPtr context, int type);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_bind(IntPtr socket,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string addr);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_connect(IntPtr socket,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string addr);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_unbind(IntPtr socket,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string addr);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_disconnect(IntPtr socket,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string addr);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_disconnect_rid(IntPtr socket,
        ref ZlinkRoutingId peerRoutingId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_send_part(IntPtr socket, ref ZlinkMsg part,
        int flags, ZlinkPartFlag partFlag);

    // DONT_WAIT-only variant: same C function, kept as a separate entry point
    // so managed code can choose the non-blocking path explicitly.
    [DllImport(LibraryName, EntryPoint = "zlink_send_part",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_send_part_nowait(IntPtr socket,
        ref ZlinkMsg part, int flags, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_recv_part(IntPtr socket,
        out IntPtr sourceRoutingId, ref ZlinkMsg part, out int hasMore,
        int flags);

    // DONT_WAIT-only variant: same C function, kept as a separate entry point
    // so managed code can choose the non-blocking path explicitly.
    [DllImport(LibraryName, EntryPoint = "zlink_recv_part",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_recv_part_nowait(IntPtr socket,
        out IntPtr sourceRoutingId, ref ZlinkMsg part, out int hasMore,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_recv_part(IntPtr router,
        out IntPtr sourceNodeRoutingId, out IntPtr sourceSpotRoutingId,
        out ulong requestSeq, ref ZlinkMsg part, out int hasMore,
        int flags);

    // DONT_WAIT-only fast variant.
    [DllImport(LibraryName, EntryPoint = "zlink_router_recv_part",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_recv_part_nowait(IntPtr router,
        out IntPtr sourceNodeRoutingId, out IntPtr sourceSpotRoutingId,
        out ulong requestSeq, ref ZlinkMsg part, out int hasMore,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_attach_raw(IntPtr socket,
        ZlinkStreamOnRawDelegate onRaw, IntPtr userdata);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_detach(IntPtr socket);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_packet_handler(IntPtr socket,
        ZlinkStreamOnPacketDelegate handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_recv_handler(IntPtr subject,
        ZlinkSocketMsgHandlerDelegate handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern unsafe int zlink_subscribe_handler(IntPtr subject,
        ZlinkSubscribeHandlerDelegate handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_send_ready_handler(IntPtr subject,
        ZlinkSendReadyHandlerDelegate handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_send_part_rid(IntPtr handle,
        ref ZlinkRoutingId targetRoutingId, ref ZlinkMsg part, int flags,
        ZlinkPartFlag partFlag);

    // DONT_WAIT-only fast variant.
    [DllImport(LibraryName, EntryPoint = "zlink_send_part_rid",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_send_part_rid_nowait(IntPtr handle,
        ref ZlinkRoutingId targetRoutingId, ref ZlinkMsg part, int flags,
        ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_option(IntPtr handle, int option,
        IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_get_option(IntPtr handle, int option,
        IntPtr value, ref nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_routing_id(IntPtr handle, IntPtr data,
        nuint size);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_get_routing_id(IntPtr handle,
        out ZlinkRoutingId routingId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_tls_server(IntPtr handle,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string cert,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string key,
        int requireClientCert);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_tls_client(IntPtr handle,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string caCert,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string hostname,
        int trustSystem);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_router_option(IntPtr handle,
        int option, IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_get_router_option(IntPtr handle,
        int option, IntPtr value, ref nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_dealer_option(IntPtr handle,
        int option, IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_pub_option(IntPtr handle, int option,
        IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_get_pub_option(IntPtr handle, int option,
        IntPtr value, ref nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_sub_option(IntPtr handle, int option,
        IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_get_sub_option(IntPtr handle, int option,
        IntPtr value, ref nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_stream_option(IntPtr handle,
        int option, IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_get_stream_option(IntPtr handle,
        int option, IntPtr value, ref nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_attach_discovery(IntPtr socket,
        IntPtr discovery);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_bind_actor(IntPtr node,
        IntPtr stream,
        ref ZlinkRoutingId sessionRid,
        ref ZlinkActorRef actor,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_unbind_actor(IntPtr node,
        IntPtr stream,
        ref ZlinkRoutingId sessionRid,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_send_bound_actor_part(IntPtr node,
        IntPtr stream,
        ref ZlinkRoutingId sessionRid,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId,
        ref ZlinkMsg part,
        int flags,
        ZlinkPartFlag partFlag);
}
