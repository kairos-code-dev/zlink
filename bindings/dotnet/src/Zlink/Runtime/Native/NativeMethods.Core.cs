using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Systems.Zlink.Native;

internal static partial class NativeMethods
{
    internal static ReadOnlySpan<string> RequiredExports => new[]
    {
        "zlink_version",
        "zlink_ctx_new",
        "zlink_ctx_term",
        "zlink_ctx_shutdown",
        "zlink_ctx_set",
        "zlink_ctx_set_data",
        "zlink_ctx_get",
        "zlink_ctx_auto_hwm_recalculate",
        "zlink_socket",
        "zlink_close",
        "zlink_send_part",
        "zlink_send_part_rid",
        "zlink_recv_part",
        "zlink_publish_part",
        "zlink_router_recv_part",
        "zlink_subscribe_part",
        "zlink_xpub_recv_part",
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
        "zlink_socket_set_channel_name",
        "zlink_socket_get_channel_name",
        "zlink_router_reply_part",
        "zlink_router_request_spot_part",
        "zlink_router_reply_spot_part",
        "zlink_router_send_spot_part",
        "zlink_spot_request_spot_part",
        "zlink_spot_request_router_part",
        "zlink_spot_reply_spot_part",
        "zlink_spot_reply_router_part",
        "zlink_spot_handler",
        "zlink_spot_dispatch_event_handler",
        "zlink_spot_recv_part",
        "zlink_stream_bind_actor",
        "zlink_stream_unbind_actor",
        "zlink_stream_send_bound_actor_part",
        "zlink_stream_bound_actors",
        "zlink_spot_node_actor_send_bound_session_msg",
        "zlink_spot_node_actor_close_bound_session",
        "zlink_spot_send_channel_part",
        "zlink_spot_request_channel_part",
        "zlink_spot_publish_part",
        "zlink_spot_subscribe_part",
        "zlink_spot_subscription_event_recv",
        "zlink_spot_node_attach_channel_dealer",
        "zlink_spot_node_attach_channel_dealer_manual",
        "zlink_spot_node_attach_pub_ingress",
        "zlink_discovery_resolve_actor",
        "zlink_spot_node_actor_new",
        "zlink_spot_node_actor_destroy",
        "zlink_spot_node_actor_lookup",
        "zlink_remote_actor_get_ref",
        "zlink_spot_node_actor_join_spot",
        "zlink_spot_actor_join_recv",
        "zlink_spot_actor_join_reply",
        "zlink_spot_node_actor_leave_spot",
        "zlink_spot_node_actor_recv_part",
        "zlink_spot_actor_lifecycle_handler",
        "zlink_spot_node_entry_spot",
        "zlink_spot_node_spot_get_or_new",
        "zlink_spot_node_spot_lookup",
        "zlink_spot_node_spots_snapshot",
        "zlink_spot_node_actors_snapshot",
        "zlink_spot_actors_snapshot",
        "zlink_timer_new",
        "zlink_spot_timer_new",
        "zlink_timer_destroy",
        "zlink_timer_start",
        "zlink_timer_stop",
        "zlink_timer_recv",
        "zlink_timer_handler",
        "zlink_poller_add_timer",
        "zlink_poller_remove_timer",
        "zlink_subscription_at",
        "zlink_set_spot_node_option",
        "zlink_get_spot_node_option",
        "zlink_set_spot_option",
        "zlink_get_spot_option",
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
    internal static extern int zlink_ctx_set_data(IntPtr context, int option,
        byte[] optval, nuint optvallen);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_ctx_get(IntPtr context, int option,
        out int errorOut);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_ctx_auto_hwm_recalculate(IntPtr context);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_close(IntPtr socket);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int zlink_errno();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_strerror(int errnum);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int zlink_msg_init(ref ZlinkMsg msg);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int zlink_msg_init_size(ref ZlinkMsg msg,
        nuint size);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int zlink_msg_init_data(ref ZlinkMsg msg,
        IntPtr data, nuint size, IntPtr freeFn, IntPtr hint);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int zlink_msg_close(ref ZlinkMsg msg);

    [DllImport(LibraryName, EntryPoint = "zlink_msg_close",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_close(IntPtr msg);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int zlink_msg_move(ref ZlinkMsg dest,
        ref ZlinkMsg src);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_copy(ref ZlinkMsg dest,
        ref ZlinkMsg src);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_adopt(ref ZlinkMsg dest,
        IntPtr src);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial IntPtr zlink_msg_data(ref ZlinkMsg msg);

    [DllImport(LibraryName, EntryPoint = "zlink_msg_data",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_msg_data(IntPtr msg);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial nuint zlink_msg_size(ref ZlinkMsg msg);

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
    internal delegate void ZlinkActorJoinHandlerDelegate(IntPtr result,
        IntPtr parts, nuint partCount, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkActorLookupHandlerDelegate(IntPtr result,
        IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal unsafe delegate void ZlinkSpotRequestHandlerDelegate(
        ZlinkRoutingId* sourceRoutingId, ZlinkRoutingId* spotRoutingId,
        ulong requestSeq, IntPtr parts, nuint partCount, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal unsafe delegate void ZlinkSpotDispatchEventHandlerDelegate(
        IntPtr spot, ZlinkSpotDispatchInfoNative* info, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal unsafe delegate void ZlinkSpotActorLifecycleHandlerDelegate(
        IntPtr spot, ZlinkSpotActorLifecycleInfo* info, IntPtr userData);

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
    internal static extern int zlink_socket_set_channel_name(IntPtr socket,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_get_channel_name(IntPtr socket,
        byte[] buffer, nuint capacity, out nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_reply_part(IntPtr router,
        ref ZlinkRoutingId peerRoutingId, ulong requestSeq,
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
        ref ZlinkRoutingId destSpotRoutingId, ulong requestSeq,
        ref ZlinkMsg part, ZlinkPartFlag partFlag);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int zlink_router_send_spot_part(IntPtr router,
        ref ZlinkRoutingId destNodeRoutingId,
        ref ZlinkRoutingId destSpotRoutingId, ref ZlinkMsg part, int flags,
        ZlinkPartFlag partFlag);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int zlink_spot_send_spot_part(IntPtr spot,
        ref ZlinkRoutingId destNodeRoutingId,
        ref ZlinkRoutingId destSpotRoutingId, ref ZlinkMsg part, int flags,
        ZlinkPartFlag partFlag);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int zlink_spot_request_spot_part(IntPtr spot,
        ref ZlinkRoutingId destNodeRoutingId,
        ref ZlinkRoutingId destSpotRoutingId, ref ZlinkMsg part,
        IntPtr handler, IntPtr userData, int flags,
        ZlinkPartFlag partFlag, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_request_router_part(IntPtr spot,
        ref ZlinkRoutingId peerRoutingId, ref ZlinkMsg part,
        IntPtr handler, IntPtr userData, int flags,
        ZlinkPartFlag partFlag, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_reply_spot_part(IntPtr spot,
        ref ZlinkRoutingId destNodeRoutingId,
        ref ZlinkRoutingId destSpotRoutingId, ulong requestSeq,
        ref ZlinkMsg part, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_reply_router_part(IntPtr spot,
        ref ZlinkRoutingId peerRoutingId, ulong requestSeq,
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
        out ulong requestSeq, ref ZlinkMsg part, out int hasMore,
        int flags);

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
