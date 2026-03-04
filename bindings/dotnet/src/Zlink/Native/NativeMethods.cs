using System;
using System.Runtime.InteropServices;

namespace Zlink.Native;

internal static class NativeMethods
{
    private const string LibraryName = "zlink";
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int ZlinkStreamOnPacketsDelegate(
        IntPtr routingId,
        IntPtr messages,
        nuint messageCount);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int ZlinkStreamOnRawDelegate(
        IntPtr routingId,
        IntPtr message);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal unsafe delegate void ZlinkSpotSubHandlerDelegate(
        byte* topic,
        nuint topicLen,
        IntPtr parts,
        nuint partCount,
        IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkTimerDelegate(int timerId, IntPtr arg);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkThreadDelegate(IntPtr arg);

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

    [DllImport(LibraryName, EntryPoint = "zlink_send",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_send(IntPtr socket, byte* buffer,
        nuint len, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_send_const(IntPtr socket, byte[] buffer,
        nuint len, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_send_const",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_send_const(IntPtr socket,
        byte* buffer, nuint len, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_recv(IntPtr socket, byte[] buffer,
        nuint len, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_recv",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_recv(IntPtr socket, byte* buffer,
        nuint len, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_attach_raw(IntPtr socket,
        ZlinkStreamOnRawDelegate onRaw);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_attach_len32be(IntPtr socket,
        ZlinkStreamOnPacketsDelegate onPackets);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_detach(IntPtr socket);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_send(IntPtr socket,
        ref ZlinkRoutingId routingId, IntPtr data, nuint size, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_stream_send_msg(IntPtr socket,
        ref ZlinkRoutingId routingId, ref ZlinkMsg msg, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_peer_info(IntPtr socket,
        [In] ref ZlinkRoutingId routingId, out ZlinkPeerInfo info);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_peer_routing_id(IntPtr socket,
        int index, out ZlinkRoutingId routingId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_peer_count(IntPtr socket);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_peers(IntPtr socket,
        [In, Out] ZlinkPeerInfo[] peers, ref nuint count);

    [DllImport(LibraryName, EntryPoint = "zlink_socket_peers",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_peers(IntPtr socket, IntPtr peers,
        ref nuint count);

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
    internal static extern int zlink_proxy(IntPtr frontend, IntPtr backend,
        IntPtr capture);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_proxy_steerable(IntPtr frontend,
        IntPtr backend, IntPtr capture, IntPtr control);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_has(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string capability);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zlink_sleep(int seconds);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zlink_multipart_close(IntPtr parts, nuint count);

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
    internal static extern IntPtr zlink_discovery_new_typed(IntPtr ctx,
        ushort serviceType);

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
    internal static extern int zlink_discovery_get_receivers(IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        [In, Out] ZlinkProviderInfo[] providers, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_receiver_count(IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_service_available(
        IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_destroy(ref IntPtr discovery);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_gateway_new(IntPtr ctx,
        IntPtr discovery, [MarshalAs(UnmanagedType.LPUTF8Str)] string? routingId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_send(IntPtr gateway,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        [In] ZlinkMsg[] parts, nuint partCount, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_gateway_send",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_gateway_send(IntPtr gateway,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        ZlinkMsg* parts, nuint partCount, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_gateway_send",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_gateway_send(IntPtr gateway,
        byte* serviceName, ZlinkMsg* parts, nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_send_rid(IntPtr gateway,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        [In] ref ZlinkRoutingId routingId, [In] ZlinkMsg[] parts,
        nuint partCount, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_gateway_send_rid",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_gateway_send_rid(IntPtr gateway,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        ZlinkRoutingId* routingId, ZlinkMsg* parts, nuint partCount, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_gateway_send_rid",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_gateway_send_rid(IntPtr gateway,
        byte* serviceName, ZlinkRoutingId* routingId, ZlinkMsg* parts,
        nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_send_bytes(IntPtr gateway,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        IntPtr data, nuint size, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_gateway_send_bytes",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_gateway_send_bytes(IntPtr gateway,
        byte* serviceName, byte* data, nuint size, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_send_rid_bytes(IntPtr gateway,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        [In] ref ZlinkRoutingId routingId, IntPtr data, nuint size, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_gateway_send_rid_bytes",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_gateway_send_rid_bytes(
        IntPtr gateway, byte* serviceName, ZlinkRoutingId* routingId,
        byte* data, nuint size, int flags);

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
    internal static extern IntPtr zlink_gateway_router_socket_unsafe(
        IntPtr gateway);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_router_peers(IntPtr gateway,
        [In, Out] ZlinkPeerInfo[] peers, ref nuint count);

    [DllImport(LibraryName, EntryPoint = "zlink_gateway_router_peers",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_router_peers(IntPtr gateway,
        IntPtr peers, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_gateway_destroy(ref IntPtr gateway);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_receiver_new(IntPtr ctx,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? routingId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_receiver_bind(IntPtr receiver,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string bindEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_receiver_connect_registry(IntPtr receiver,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string registryEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_receiver_register(IntPtr receiver,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string advertiseEndpoint,
        uint weight);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_receiver_update_weight(IntPtr receiver,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName, uint weight);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_receiver_unregister(IntPtr receiver,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_receiver_register_result(
        IntPtr receiver, [MarshalAs(UnmanagedType.LPUTF8Str)] string serviceName,
        out int status, byte* resolvedEndpoint, byte* errorMessage);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_receiver_set_tls_server(IntPtr receiver,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string cert,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string key);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_receiver_router_socket_unsafe(IntPtr receiver);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_receiver_router_peers(IntPtr receiver,
        [In, Out] ZlinkPeerInfo[] peers, ref nuint count);

    [DllImport(LibraryName, EntryPoint = "zlink_receiver_router_peers",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_receiver_router_peers(IntPtr receiver,
        IntPtr peers, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_receiver_setsockopt(IntPtr receiver, int role,
        int option, IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_setsockopt(IntPtr registry, int role,
        int option, IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_setsockopt(IntPtr discovery, int role,
        int option, IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_receiver_destroy(ref IntPtr receiver);

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
    internal static extern int zlink_spot_node_setsockopt(IntPtr node, int role,
        int option, IntPtr value, nuint length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_node_pub_socket_unsafe(IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_node_sub_socket_unsafe(IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_pub_peers(IntPtr node,
        [In, Out] ZlinkPeerInfo[] peers, ref nuint count);

    [DllImport(LibraryName, EntryPoint = "zlink_spot_node_pub_peers",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_pub_peers(IntPtr node,
        IntPtr peers, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_sub_peers(IntPtr node,
        [In, Out] ZlinkPeerInfo[] peers, ref nuint count);

    [DllImport(LibraryName, EntryPoint = "zlink_spot_node_sub_peers",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_node_sub_peers(IntPtr node,
        IntPtr peers, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_pub_new(IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_pub_destroy(ref IntPtr pub);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_pub_publish(IntPtr pub,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId,
        [In] ZlinkMsg[] parts, nuint partCount, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_spot_pub_publish",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_spot_pub_publish(IntPtr pub,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId,
        ZlinkMsg* parts, nuint partCount, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_spot_pub_publish",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_spot_pub_publish(IntPtr pub,
        byte* topicId, ZlinkMsg* parts, nuint partCount, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_pub_publish_bytes(IntPtr pub,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId,
        IntPtr data, nuint size, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_spot_pub_publish_bytes",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_spot_pub_publish_bytes(IntPtr pub,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId,
        byte* data, nuint size, int flags);

    [DllImport(LibraryName, EntryPoint = "zlink_spot_pub_publish_bytes",
        CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_spot_pub_publish_bytes(IntPtr pub,
        byte* topicId, byte* data, nuint size, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_sub_new(IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_sub_destroy(ref IntPtr sub);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_sub_subscribe(IntPtr sub,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_sub_subscribe_pattern(IntPtr sub,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string pattern);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_sub_unsubscribe(IntPtr sub,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string topicIdOrPattern);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern unsafe int zlink_spot_sub_set_handler(IntPtr sub,
        ZlinkSpotSubHandlerDelegate? handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static unsafe extern int zlink_spot_sub_recv(IntPtr sub,
        out IntPtr parts, out nuint partCount, int flags, byte* topicId,
        ref nuint topicIdLen);

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
    internal static extern IntPtr zlink_timers_new();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_timers_destroy(ref IntPtr timers);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_timers_add(IntPtr timers, nuint interval,
        ZlinkTimerDelegate handler, IntPtr arg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_timers_cancel(IntPtr timers, int timerId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_timers_set_interval(IntPtr timers,
        int timerId, nuint interval);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_timers_reset(IntPtr timers, int timerId);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern long zlink_timers_timeout(IntPtr timers);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_timers_execute(IntPtr timers);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_stopwatch_start();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong zlink_stopwatch_intermediate(IntPtr watch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong zlink_stopwatch_stop(IntPtr watch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_thread_start(ZlinkThreadDelegate func,
        IntPtr arg);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zlink_thread_join(IntPtr thread);

}
