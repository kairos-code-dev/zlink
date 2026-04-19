using System;
using System.Runtime.InteropServices;

namespace Zlink.Native;

internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_registry_new(IntPtr ctx);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_new(IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_destroy(ref IntPtr spot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_publish(IntPtr subject,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId, IntPtr parts,
        nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_publish_part(IntPtr subject,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId, ref ZlinkMsg part,
        int flags, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_subscription(IntPtr handle,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filter);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_unset_subscription(IntPtr handle,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filter);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_subscribe(IntPtr subject,
        IntPtr sourceRoutingId, out IntPtr parts, out nuint partCount,
        byte[] topicIdOut, ref nuint topicIdLenOut, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_subscribe_part(IntPtr subject,
        out IntPtr sourceRoutingId, byte[] topicIdBuffer, nuint topicIdCapacity,
        out nuint topicIdLenOut, ref ZlinkMsg part, out int hasMore, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_subscription_event(IntPtr subject,
        IntPtr sourceRoutingId, out int subscribed, byte[] topicIdOut,
        ref nuint topicIdLenOut, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_bind(IntPtr registry,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string pubEndpoint,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string routerEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_set_id(IntPtr registry,
        uint registryId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_add_peer(IntPtr registry,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string peerPubEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_set_heartbeat(IntPtr registry,
        uint intervalMs, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_set_broadcast_interval(
        IntPtr registry, uint intervalMs);

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
        int serviceType,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_member_peer_metadata(
        IntPtr registry, int serviceType,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        ushort serviceRole,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint,
        ref ZlinkMsg metadata);

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
        int serviceType,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_connect_registry(
        IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string registryEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_set_dealer_peer_mode(
        IntPtr discovery, int mode);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_resolve_spot(IntPtr discovery,
        ref ZlinkRoutingId spotRoutingId, out ZlinkRoutingId ownerNodeRoutingId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_set_value(IntPtr discovery,
        long value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_get_value(IntPtr discovery,
        out long value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_set_metadata(IntPtr discovery,
        IntPtr data, nuint size);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_get_metadata(IntPtr discovery,
        ref ZlinkMsg metadata);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_member_peers(IntPtr discovery,
        IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_member_peer_metadata(
        IntPtr discovery, ushort serviceRole,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint,
        ref ZlinkMsg metadata);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_destroy(ref IntPtr discovery);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_node_new(IntPtr ctx);

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
    internal static extern int zlink_spot_node_attach_pub_ingress(
        IntPtr node, IntPtr pub);

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
    internal static extern int zlink_spot_send_channel(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName, IntPtr parts,
        nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_send_channel_part(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        ref ZlinkMsg part, int flags, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_request_channel(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName, IntPtr parts,
        nuint partCount, ZlinkReplyHandlerDelegate handler, IntPtr userData,
        int flags, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_request_channel_part(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        ref ZlinkMsg part, ZlinkReplyHandlerDelegate? handler, IntPtr userData,
        int flags, ZlinkPartFlag partFlag, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_publish(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId, IntPtr parts,
        nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_publish_part(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId, ref ZlinkMsg part,
        int flags, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_subscribe(IntPtr spot,
        IntPtr sourceRoutingId, out IntPtr parts, out nuint partCount,
        byte[] serviceNameOut, ref nuint serviceNameLenOut,
        byte[] topicIdOut, ref nuint topicIdLenOut, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_subscribe_part(IntPtr spot,
        out IntPtr sourceRoutingId, byte[] serviceNameBuffer,
        nuint serviceNameCapacity, out nuint serviceNameLenOut,
        byte[] topicIdBuffer, nuint topicIdCapacity, out nuint topicIdLenOut,
        ref ZlinkMsg part, out int hasMore, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_subscription_event(IntPtr spot,
        IntPtr sourceRoutingId, out int subscribed, byte[] serviceNameOut,
        ref nuint serviceNameLenOut, byte[] topicIdOut,
        ref nuint topicIdLenOut, int flags);
}
