using System;
using System.Runtime.InteropServices;

namespace Zlink.Native;

internal static class NativeMethods
{
    private const string LibraryName = "zlink";
    static NativeMethods()
    {
        NativeLibraryLoader.EnsureLoaded();
    }

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
    internal static extern int zlink_ctx_get(IntPtr context, int option);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_socket(IntPtr context, int type);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_close(IntPtr socket);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_errno();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_strerror(int errnum);

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
    internal static extern int zlink_send(IntPtr socket, byte[] buffer,
        nuint len, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_send_const(IntPtr socket, byte[] buffer,
        nuint len, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_recv(IntPtr socket, byte[] buffer,
        nuint len, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_init(ref ZlinkMsg msg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_init_size(ref ZlinkMsg msg,
        nuint size);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_init_data(ref ZlinkMsg msg,
        IntPtr data, nuint size, IntPtr freeFn, IntPtr hint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_send(ref ZlinkMsg msg, IntPtr socket,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_recv(ref ZlinkMsg msg, IntPtr socket,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_close(ref ZlinkMsg msg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_move(ref ZlinkMsg dest,
        ref ZlinkMsg src);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_copy(ref ZlinkMsg dest,
        ref ZlinkMsg src);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_msg_data(ref ZlinkMsg msg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint zlink_msg_size(ref ZlinkMsg msg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_more(ref ZlinkMsg msg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_get(ref ZlinkMsg msg, int property);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_msg_set(ref ZlinkMsg msg, int property,
        int optval);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_msg_gets(ref ZlinkMsg msg,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string property);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_setsockopt(IntPtr socket, int option,
        IntPtr optval, nuint optvallen);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_getsockopt(IntPtr socket, int option,
        IntPtr optval, ref nuint optvallen);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_monitor(IntPtr socket,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string addr, int events);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_socket_monitor_open(IntPtr socket,
        int events);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_monitor_recv(IntPtr monitorSocket,
        out ZlinkMonitorEvent evt, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_poll",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poll_unix(
        [In, Out] ZlinkPollItemUnix[] items, int nitems, long timeout);

    [DllImport(LibraryName, EntryPoint = "zlink_poll",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poll_windows(
        [In, Out] ZlinkPollItemWindows[] items, int nitems, long timeout);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zlink_msgv_close(IntPtr parts, nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_registry_new(IntPtr ctx);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_set_endpoints(IntPtr registry,
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
    internal static extern int zlink_registry_start(IntPtr registry);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_destroy(ref IntPtr registry);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_discovery_new(IntPtr ctx);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_connect_registry(
        IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string registryPubEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_subscribe(IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_unsubscribe(IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_get_providers(IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        [In, Out] ZlinkProviderInfo[] providers, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_provider_count(IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_service_available(
        IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_destroy(ref IntPtr discovery);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_gateway_new(IntPtr ctx,
        IntPtr discovery, [MarshalAs(UnmanagedType.LPUTF8Str)] string routingId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_send(IntPtr gateway,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        [In] ZlinkMsg[] parts, nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_gateway_recv(IntPtr gateway,
        out IntPtr parts, out nuint partCount, int flags, byte* serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_set_lb_strategy(IntPtr gateway,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName, int strategy);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_set_tls_client(IntPtr gateway,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string caCert,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string hostname, int trustSystem);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_connection_count(IntPtr gateway,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_setsockopt(IntPtr gateway,
        int option, IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_destroy(ref IntPtr gateway);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_provider_new(IntPtr ctx,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string routingId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_provider_bind(IntPtr provider,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string bindEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_provider_connect_registry(IntPtr provider,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string registryEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_provider_register(IntPtr provider,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string advertiseEndpoint,
        uint weight);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_provider_update_weight(IntPtr provider,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName, uint weight);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_provider_unregister(IntPtr provider,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_provider_register_result(
        IntPtr provider, [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        out int status, byte* resolvedEndpoint, byte* errorMessage);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_provider_set_tls_server(IntPtr provider,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string cert,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string key);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_provider_router(IntPtr provider);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_provider_setsockopt(IntPtr provider, int role,
        int option, IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_setsockopt(IntPtr registry, int role,
        int option, IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_setsockopt(IntPtr discovery, int role,
        int option, IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_provider_destroy(ref IntPtr provider);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_node_new(IntPtr ctx);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_destroy(ref IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_bind(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_connect_registry(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string registryEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_connect_peer_pub(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string peerPubEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_disconnect_peer_pub(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string peerPubEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_register(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string advertiseEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_unregister(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_set_discovery(IntPtr node,
        IntPtr discovery, [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_set_tls_server(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string cert,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string key);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_set_tls_client(IntPtr node,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string caCert,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string hostname, int trustSystem);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_node_pub_socket(IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_node_sub_socket(IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_new(IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_destroy(ref IntPtr spot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_topic_create(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId, int mode);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_topic_destroy(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_publish(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId,
        [In] ZlinkMsg[] parts, nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_subscribe(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_subscribe_pattern(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string pattern);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_unsubscribe(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicIdOrPattern);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_spot_recv(IntPtr spot,
        out IntPtr parts, out nuint partCount, int flags, byte* topicId,
        ref nuint topicIdLen);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_pub_socket(IntPtr spot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_sub_socket(IntPtr spot);
}
