using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_new(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId,
        out ZlinkActorRef actorRef);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern unsafe int zlink_spot_node_actor_new_with_request(
        IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId,
        ZlinkMsg* parts,
        nuint partCount,
        out ZlinkActorRef actorRef);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_destroy(IntPtr node,
        ref ZlinkActorRef actor,
        IntPtr handler,
        IntPtr userData,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_lookup(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId,
        out ZlinkActorRef actorRef);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_remote_actor_get_ref(
        IntPtr node,
        ref ZlinkRoutingId targetNodeRid,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId,
        IntPtr handler,
        IntPtr userData,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_join_spot(IntPtr node,
        ref ZlinkActorRef actor,
        ref ZlinkRoutingId destNodeRid,
        ref ZlinkRoutingId destSpotRid,
        ref ZlinkMsg message,
        nuint partCount,
        IntPtr handler,
        IntPtr userData,
        int flags,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_join_entry_spot(
        IntPtr node,
        ref ZlinkActorRef actor,
        ref ZlinkRoutingId destNodeRid,
        ref ZlinkMsg parts,
        nuint partCount,
        IntPtr handler,
        IntPtr userData,
        int flags,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_actor_join_recv(IntPtr spot,
        out ZlinkActorJoinInfo info,
        out IntPtr parts,
        out nuint partCount,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_actor_join_reply(IntPtr spot,
        ref ZlinkActorJoinInfo info,
        int joinResultCode,
        ref ZlinkMsg message,
        nuint partCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "zlink_spot_actor_join_reply")]
    internal static extern int zlink_spot_actor_join_reply_empty(IntPtr spot,
        ref ZlinkActorJoinInfo info,
        int joinResultCode,
        IntPtr parts,
        nuint partCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_leave_spot(IntPtr node,
        ref ZlinkActorRef actor,
        ref ZlinkRoutingId destSpotRid,
        IntPtr handler,
        IntPtr userData,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_recv_part(IntPtr node,
        ref ZlinkActorRef actor,
        out ZlinkActorRecvInfo info,
        ref ZlinkMsg part,
        out ZlinkPartFlag hasMore,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_recv_actor_lifecycle(
        IntPtr spot,
        out ZlinkSpotActorLifecycleEvent lifecycleEvent,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_recv_actor_lifecycle_with_request(
        IntPtr spot,
        out ZlinkSpotActorLifecycleEvent lifecycleEvent,
        out IntPtr parts,
        out nuint partCount,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_send_bound_session_msg(
        IntPtr node,
        ref ZlinkActorRef actor,
        ref ZlinkMsg message,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_forward_bound_session_part(
        IntPtr node,
        ref ZlinkActorRef actor,
        ref ZlinkRoutingId sourceNodeRid,
        ref ZlinkRoutingId sourceSessionRid,
        ref ZlinkMsg message,
        int flags,
        ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_bind_remote_session(
        IntPtr node,
        ref ZlinkActorRef actor,
        ref ZlinkRoutingId sourceNodeRid,
        ref ZlinkRoutingId sourceSessionRid);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_close_bound_session(
        IntPtr node,
        ref ZlinkActorRef actor,
        uint timeoutMs);
}