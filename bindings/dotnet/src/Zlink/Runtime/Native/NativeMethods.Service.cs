using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Systems.Zlink.Native;

internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_registry_new(IntPtr ctx);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_new(IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_destroy(ref IntPtr spot);

    [LibraryImport(LibraryName, EntryPoint = "zlink_publish_part")]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe partial int zlink_publish_part_utf8(IntPtr subject,
        byte* topicId, ref ZlinkMsg part, int flags, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_subscription(IntPtr handle,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filter);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_unset_subscription(IntPtr handle,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filter);

    [LibraryImport(LibraryName)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int zlink_subscribe_part(IntPtr subject,
        out IntPtr sourceRoutingId, byte[] topicIdBuffer, nuint topicIdCapacity,
        out nuint topicIdLenOut, ref ZlinkMsg part, out int hasMore, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_xpub_recv_part(IntPtr subject,
        out IntPtr sourceRoutingId, out int subscribed, byte[] topicIdBuffer,
        nuint topicIdCapacity, out nuint topicIdLenOut, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_bind(IntPtr registry,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string pubEndpoint,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string routerEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_set(IntPtr registry,
        int option, uint value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint zlink_registry_get(IntPtr registry,
        int option, out int error);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_add_peer(IntPtr registry,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string peerPubEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_status_snapshot(IntPtr registry,
        out ZlinkRegistryStatus status);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_service_summary_snapshot(
        IntPtr registry, IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_topology_snapshot(
        IntPtr registry, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_topology_query(IntPtr registry,
        IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_member_peers(IntPtr registry,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_registry_query_client_new(IntPtr ctx);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_query_client_connect(
        IntPtr client,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_query_snapshot(IntPtr client,
        IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_query_destroy(ref IntPtr client);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_destroy(ref IntPtr registry);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_discovery_new(IntPtr ctx,
        int autoConnectType,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_connect_registry(
        IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string registryEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_resolve_spot(IntPtr discovery,
        ref ZlinkRoutingId spotRoutingId, out ZlinkRoutingId ownerNodeRoutingId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_resolve_actor(IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId,
        out ZlinkActorRoute route);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_discovery_bind_route(
        IntPtr discovery, uint kind, byte* key, nuint keySize, byte* value,
        nuint valueSize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_discovery_unbind_route(
        IntPtr discovery, uint kind, byte* key, nuint keySize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_discovery_resolve_route(
        IntPtr discovery, uint kind, byte* key, nuint keySize,
        out ZlinkRoutingId ownerRoutingId, ref ZlinkMsg value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_set_value(IntPtr discovery,
        long value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_get_value(IntPtr discovery,
        out long value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_member_peers(IntPtr discovery,
        IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_destroy(ref IntPtr discovery);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_node_new(IntPtr ctx, IntPtr options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "zlink_spot_node_new")]
    internal static extern IntPtr zlink_spot_node_new(IntPtr ctx,
        ref ZlinkSpotNodeOptions options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_destroy(ref IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_bind(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_connect_peer(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_disconnect_peer(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_disconnect_peer_rid(IntPtr node,
        ref ZlinkRoutingId targetNodeRid);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_connect_router_channel_peer(
        IntPtr node, [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_disconnect_router_channel_peer(
        IntPtr node, [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_disconnect_router_channel_peer_rid(
        IntPtr node, [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        ref ZlinkRoutingId peerRid);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_attach_channel_dealer(
        IntPtr node, IntPtr discovery, IntPtr dealer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_attach_channel_dealer_manual(
        IntPtr node, [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        IntPtr dealer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_attach_discovery(IntPtr node,
        IntPtr discovery);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_attach_router_channel_discovery(
        IntPtr node, [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        IntPtr discovery);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_attach_pub_ingress(
        IntPtr node, IntPtr pub);

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
    internal static extern int zlink_spot_node_status_snapshot(IntPtr node,
        out ZlinkSpotNodeStatus status);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_peers_snapshot(IntPtr node,
        IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_peers_query(IntPtr node,
        IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_subjects_snapshot(IntPtr node,
        IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_internal_sockets_snapshot(
        IntPtr node, IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_new(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId,
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
    internal static extern int zlink_spot_actor_join_recv(IntPtr spot,
        out ZlinkActorJoinInfo info,
        out IntPtr parts,
        out nuint partCount,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_actor_join_reply(IntPtr spot,
        ref ZlinkActorJoinInfo info,
        uint accepted,
        ref ZlinkMsg message,
        nuint partCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "zlink_spot_actor_join_reply")]
    internal static extern int zlink_spot_actor_join_reply_empty(IntPtr spot,
        ref ZlinkActorJoinInfo info,
        uint accepted,
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
    internal static extern int zlink_spot_actor_lifecycle_handler(
        IntPtr spot,
        NativeMethods.ZlinkSpotActorLifecycleHandlerDelegate? onJoin,
        NativeMethods.ZlinkSpotActorLifecycleHandlerDelegate? onLeave,
        IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_send_bound_session_msg(
        IntPtr node,
        ref ZlinkActorRef actor,
        ref ZlinkMsg message,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actor_close_bound_session(
        IntPtr node,
        ref ZlinkActorRef actor,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_spots_snapshot(IntPtr node,
        IntPtr entries,
        ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_actors_snapshot(IntPtr node,
        IntPtr entries,
        ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_actors_snapshot(IntPtr spot,
        IntPtr entries,
        ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_send_channel_part(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        ref ZlinkMsg part, int flags, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_request_channel_part(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        ref ZlinkMsg part, IntPtr handler, IntPtr userData,
        int flags, ZlinkPartFlag partFlag, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_spot_option(IntPtr spot,
        SpotOption option, IntPtr value, nuint valueSize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_get_spot_option(IntPtr spot,
        SpotOption option, IntPtr value, ref nuint valueSize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_subscription_at(IntPtr handle,
        nuint index, IntPtr filterOut, ref nuint filterLength,
        out int isPattern);

    [DllImport(LibraryName, EntryPoint = "zlink_spot_publish_part",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern unsafe int zlink_spot_publish_part_utf8(IntPtr spot,
        byte* topicId, ref ZlinkMsg part, int flags, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_subscribe_part(IntPtr spot,
        out IntPtr sourceRoutingId, byte[] topicIdBuffer,
        nuint topicIdCapacity, out nuint topicIdLenOut,
        ref ZlinkMsg part, out int hasMore, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_spot_subscribe_part",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern unsafe int zlink_spot_subscribe_part_buffer(
        IntPtr spot, out IntPtr sourceRoutingId, byte* topicIdBuffer,
        nuint topicIdCapacity, out nuint topicIdLenOut,
        ref ZlinkMsg part, out int hasMore, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_subscription_event_recv(IntPtr spot,
        out IntPtr sourceRoutingId, out int subscribed, byte[] topicIdBuffer,
        nuint topicIdCapacity, out nuint topicIdLenOut, int flags);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkTimerHandlerDelegate(IntPtr timer,
        ulong fireCount, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_timer_new();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_timer_new(IntPtr spot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_timer_destroy(ref IntPtr timer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_timer_start(IntPtr timer,
        ulong intervalNs, ulong repeatCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_timer_stop(IntPtr timer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_timer_recv(IntPtr timer,
        out ulong fireCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_timer_handler(IntPtr timer,
        ZlinkTimerHandlerDelegate handler, IntPtr userData);
}
