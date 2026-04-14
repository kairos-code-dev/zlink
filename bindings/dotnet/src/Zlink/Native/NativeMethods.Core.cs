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
        "zlink_send",
        "zlink_send_rid",
        "zlink_recv",
        "zlink_publish",
        "zlink_errno",
        "zlink_strerror",
        "zlink_msg_init",
        "zlink_msg_init_size",
        "zlink_msg_init_data",
        "zlink_msg_close",
        "zlink_msg_move",
        "zlink_msg_copy",
        "zlink_msg_data",
        "zlink_msg_size",
        "zlink_msg_refcnt",
        "zlink_msg_gets",
        "zlink_multipart_close",
        "zlink_dealer_request",
        "zlink_router_request",
        "zlink_router_reply",
        "zlink_router_handler",
        "zlink_router_request_spot",
        "zlink_router_reply_spot",
        "zlink_router_send_spot",
        "zlink_spot_request_spot",
        "zlink_spot_send_spot",
        "zlink_spot_send_router",
        "zlink_spot_request_router",
        "zlink_spot_reply_spot",
        "zlink_spot_reply_router",
        "zlink_spot_handler",
        "zlink_spot_dispatch_event_handler",
        "zlink_spot_recv",
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
    internal unsafe delegate void ZlinkRouterRequestHandlerDelegate(
        ZlinkRoutingId* sourceNodeRoutingId, ZlinkRoutingId* sourceSpotRoutingId,
        ulong requestSequence, IntPtr parts, nuint partCount, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal unsafe delegate void ZlinkSpotRequestHandlerDelegate(
        ZlinkRoutingId* sourceRoutingId, ZlinkRoutingId* spotRoutingId,
        ulong requestSequence, IntPtr parts, nuint partCount, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkSpotDispatchEventHandlerDelegate(
        IntPtr spot, int @event, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_dealer_request(IntPtr dealer,
        IntPtr parts, nuint partCount, ZlinkReplyHandlerDelegate handler,
        IntPtr userData, int flags, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_request(IntPtr router,
        ref ZlinkRoutingId peerRoutingId, IntPtr parts, nuint partCount,
        ZlinkReplyHandlerDelegate handler, IntPtr userData, int flags,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_reply(IntPtr router,
        ref ZlinkRoutingId peerRoutingId, ulong requestSequence, IntPtr parts,
        nuint partCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_handler(IntPtr router,
        ZlinkRouterRequestHandlerDelegate handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_request_spot(IntPtr router,
        ref ZlinkRoutingId destNodeRoutingId, ref ZlinkRoutingId destSpotRoutingId,
        IntPtr parts, nuint partCount, ZlinkReplyHandlerDelegate handler,
        IntPtr userData, int flags, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_reply_spot(IntPtr router,
        ref ZlinkRoutingId destNodeRoutingId, ref ZlinkRoutingId destSpotRoutingId,
        ulong requestSequence, IntPtr parts, nuint partCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_router_send_spot(IntPtr router,
        ref ZlinkRoutingId destNodeRoutingId, ref ZlinkRoutingId destSpotRoutingId,
        IntPtr parts, nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_request_spot(IntPtr spot,
        ref ZlinkRoutingId destNodeRoutingId, ref ZlinkRoutingId destSpotRoutingId,
        IntPtr parts, nuint partCount, ZlinkReplyHandlerDelegate handler,
        IntPtr userData, int flags, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_send_spot(IntPtr spot,
        ref ZlinkRoutingId destNodeRoutingId, ref ZlinkRoutingId destSpotRoutingId,
        IntPtr parts, nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_send_router(IntPtr spot,
        ref ZlinkRoutingId peerRoutingId, IntPtr parts, nuint partCount,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_request_router(IntPtr spot,
        ref ZlinkRoutingId peerRoutingId, IntPtr parts, nuint partCount,
        ZlinkReplyHandlerDelegate handler, IntPtr userData, int flags,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_reply_spot(IntPtr spot,
        ref ZlinkRoutingId destNodeRoutingId, ref ZlinkRoutingId destSpotRoutingId,
        ulong requestSequence, IntPtr parts, nuint partCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_reply_router(IntPtr spot,
        ref ZlinkRoutingId peerRoutingId, ulong requestSequence, IntPtr parts,
        nuint partCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_handler(IntPtr spot,
        ZlinkSpotRequestHandlerDelegate handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_dispatch_event_handler(IntPtr spot,
        ZlinkSpotDispatchEventHandlerDelegate handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_recv(IntPtr spot,
        out IntPtr sourceRoutingId, out IntPtr spotRoutingId,
        out ulong requestSequence, out IntPtr parts, out nuint partCount,
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
