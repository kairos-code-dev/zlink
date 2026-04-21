using System;
using System.Runtime.InteropServices;

namespace Zlink.Native;

internal static partial class NativeMethods
{
    internal static ReadOnlySpan<string> RequiredExports => new[]
    {
        "zlink_version",
        "zlink_ctx_new",
        "zlink_ctx_term",
        "zlink_ctx_shutdown",
        "zlink_ctx_set",
        "zlink_ctx_get",
        "zlink_socket",
        "zlink_close",
        "zlink_send_part",
        "zlink_send_part_rid",
        "zlink_recv_part",
        "zlink_publish_part",
        "zlink_router_recv_part",
        "zlink_subscribe_part",
        "zlink_xpub_recv_part",
        "zlink_set_admission_state",
        "zlink_get_admission_state",
        "zlink_errno",
        "zlink_strerror",
        "zlink_msg_init",
        "zlink_msg_init_size",
        "zlink_msg_init_data",
        "zlink_msg_close",
        "zlink_msg_move",
        "zlink_msg_copy",
        "zlink_msg_adopt",
        "zlink_msg_data",
        "zlink_msg_size",
        "zlink_msg_refcnt",
        "zlink_msg_gets",
        "zlink_multipart_close",
        "zlink_dealer_request_part",
        "zlink_router_request_part",
        "zlink_socket_request_progress_internal",
        "zlink_socket_set_channel_name",
        "zlink_socket_get_channel_name",
        "zlink_router_reply_part",
        "zlink_router_request_spot_part",
        "zlink_router_reply_spot_part",
        "zlink_router_send_spot_part",
        "zlink_spot_reply_spot_part",
        "zlink_spot_reply_router_part",
        "zlink_spot_handler",
        "zlink_spot_dispatch_event_handler",
        "zlink_spot_recv_part",
        "zlink_spot_send_channel_part",
        "zlink_spot_request_channel_part",
        "zlink_spot_request_progress_internal",
        "zlink_spot_channel_reply_progress_from",
        "zlink_spot_publish_part",
        "zlink_spot_subscribe_part",
        "zlink_spot_subscription_event_recv",
        "zlink_spot_node_attach_channel_dealer",
        "zlink_spot_node_attach_channel_dealer_manual",
        "zlink_spot_node_attach_pub_ingress",
        "zlink_timer_new",
        "zlink_spot_timer_new",
        "zlink_timer_destroy",
        "zlink_timer_start",
        "zlink_timer_stop",
        "zlink_timer_recv",
        "zlink_timer_handler",
        "zlink_poller_add_timer",
        "zlink_poller_remove_timer",
        "zlink_atomic_counter_new",
        "zlink_atomic_counter_set",
        "zlink_atomic_counter_inc",
        "zlink_atomic_counter_dec",
        "zlink_atomic_counter_value",
        "zlink_atomic_counter_destroy",
        "zlink_stopwatch_start",
        "zlink_stopwatch_intermediate",
        "zlink_stopwatch_stop",
    };

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zlink_version(out int major, out int minor,
        out int patch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_ctx_new();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_ctx_term(IntPtr context);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_ctx_shutdown(IntPtr context);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_ctx_set(IntPtr context, int option,
        int optval);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_ctx_get(IntPtr context, int option,
        out int errorOut);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_close(IntPtr socket);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_errno();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_strerror(int errnum);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_init(ref ZlinkMsg msg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_init_size(ref ZlinkMsg msg,
        nuint size);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_init_data(ref ZlinkMsg msg,
        IntPtr data, nuint size, IntPtr freeFn, IntPtr hint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_close(ref ZlinkMsg msg);

    [DllImport(LibraryName, EntryPoint = "zlink_msg_close",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_close(IntPtr msg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_move(ref ZlinkMsg dest,
        ref ZlinkMsg src);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_copy(ref ZlinkMsg dest,
        ref ZlinkMsg src);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_adopt(ref ZlinkMsg dest,
        IntPtr src);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_msg_data(ref ZlinkMsg msg);

    [DllImport(LibraryName, EntryPoint = "zlink_msg_data",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_msg_data(IntPtr msg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint zlink_msg_size(ref ZlinkMsg msg);

    [DllImport(LibraryName, EntryPoint = "zlink_msg_size",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint zlink_msg_size(IntPtr msg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_refcnt(ref ZlinkMsg msg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_msg_gets(ref ZlinkMsg msg,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string property);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zlink_multipart_close(IntPtr parts, nuint count);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkReplyHandlerDelegate(int result, IntPtr parts,
        nuint partCount, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal unsafe delegate void ZlinkSpotRequestHandlerDelegate(
        ZlinkRoutingId* sourceRoutingId, ZlinkRoutingId* spotRoutingId,
        ulong requestSequence, IntPtr parts, nuint partCount, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal unsafe delegate void ZlinkSpotDispatchEventHandlerDelegate(
        IntPtr spot, ZlinkSpotDispatchInfoNative* info, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_dealer_request_part(IntPtr dealer,
        ref ZlinkMsg part, int flags, ZlinkPartFlag partFlag, uint timeoutMs,
        IntPtr handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_request_part(IntPtr router,
        ref ZlinkRoutingId peerRoutingId, ref ZlinkMsg part, int flags,
        ZlinkPartFlag partFlag, uint timeoutMs,
        IntPtr handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_request_progress_internal(IntPtr socket);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_set_channel_name(IntPtr socket,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_get_channel_name(IntPtr socket,
        byte[] buffer, nuint capacity, out nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_reply_part(IntPtr router,
        ref ZlinkRoutingId peerRoutingId, ulong requestSequence,
        ref ZlinkMsg part, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_request_spot_part(IntPtr router,
        ref ZlinkRoutingId destNodeRoutingId,
        ref ZlinkRoutingId destSpotRoutingId, ref ZlinkMsg part,
        IntPtr handler, IntPtr userData, int flags,
        ZlinkPartFlag partFlag, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_reply_spot_part(IntPtr router,
        ref ZlinkRoutingId destNodeRoutingId,
        ref ZlinkRoutingId destSpotRoutingId, ulong requestSequence,
        ref ZlinkMsg part, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_send_spot_part(IntPtr router,
        ref ZlinkRoutingId destNodeRoutingId,
        ref ZlinkRoutingId destSpotRoutingId, ref ZlinkMsg part, int flags,
        ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_reply_spot_part(IntPtr spot,
        ref ZlinkRoutingId destNodeRoutingId,
        ref ZlinkRoutingId destSpotRoutingId, ulong requestSequence,
        ref ZlinkMsg part, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_reply_router_part(IntPtr spot,
        ref ZlinkRoutingId peerRoutingId, ulong requestSequence,
        ref ZlinkMsg part, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_handler(IntPtr spot,
        ZlinkSpotRequestHandlerDelegate handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_dispatch_event_handler(IntPtr spot,
        ZlinkSpotDispatchEventHandlerDelegate handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_recv_part(IntPtr spot,
        out IntPtr sourceRoutingId, out IntPtr spotRoutingId,
        out ulong requestSequence, ref ZlinkMsg part, out int hasMore,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_channel_reply_progress_from(IntPtr spot,
        IntPtr dealer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_atomic_counter_new();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zlink_atomic_counter_set(IntPtr counter,
        int value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_atomic_counter_inc(IntPtr counter);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_atomic_counter_dec(IntPtr counter);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_atomic_counter_value(IntPtr counter);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zlink_atomic_counter_destroy(ref IntPtr counter);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_stopwatch_start();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong zlink_stopwatch_intermediate(IntPtr watch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong zlink_stopwatch_stop(IntPtr watch);
}
