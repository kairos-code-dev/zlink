package dev.kairoscode.zlink.internal;

import dev.kairoscode.zlink.MonitorEvent;
import dev.kairoscode.zlink.ZlinkException;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

public final class Native {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP = LibraryLoader.lookup();

    private static MemorySegment requireSymbol(String name) {
        return LOOKUP.find(name).orElseThrow(
          () -> new IllegalStateException(
            "Missing native symbol '" + name
              + "'. Loaded libzlink is incompatible with this Java binding."));
    }

    private static MethodHandle downcall(String name, FunctionDescriptor fd) {
        return LINKER.downcallHandle(requireSymbol(name), fd);
    }

    private static final MethodHandle MH_VERSION = downcall("zlink_version",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_CTX_NEW = downcall("zlink_ctx_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS));
    private static final MethodHandle MH_CTX_TERM = downcall("zlink_ctx_term",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_CTX_SET = downcall("zlink_ctx_set",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_CTX_GET = downcall("zlink_ctx_get",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));
    private static final MethodHandle MH_CTX_SHUTDOWN = downcall("zlink_ctx_shutdown",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SOCKET = downcall("zlink_socket",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_CLOSE = downcall("zlink_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_BIND = downcall("zlink_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_CONNECT = downcall("zlink_connect",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_UNBIND = downcall("zlink_unbind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISCONNECT = downcall("zlink_disconnect",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SEND = downcall("zlink_send",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_RECV = downcall("zlink_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_STREAM_ATTACH = downcall("zlink_stream_attach",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_STREAM_ATTACH_RAW = downcall("zlink_stream_attach_raw",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_STREAM_ATTACH_LEN32BE = downcall("zlink_stream_attach_len32be",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_STREAM_DETACH = downcall("zlink_stream_detach",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_STREAM_SEND = downcall("zlink_stream_send",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_STREAM_SEND_MSG = downcall("zlink_stream_send_msg",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SOCKET_PEER_ROUTING_ID =
      downcall("zlink_socket_peer_routing_id",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
          ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SETSOCKOPT = downcall("zlink_setsockopt",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_GETSOCKOPT = downcall("zlink_getsockopt",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private static final MethodHandle MH_POLL = downcall("zlink_poll",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_POLLER_NEW = downcall("zlink_poller_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_DESTROY = downcall("zlink_poller_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_SIZE = downcall("zlink_poller_size",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_ADD = downcall("zlink_poller_add",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_ADD_SPOT_SUB = downcall(
        "zlink_poller_add_spot_sub",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_ADD_SPOT_PUB = downcall(
        "zlink_poller_add_spot_pub",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_ADD_GATEWAY = downcall(
        "zlink_poller_add_gateway",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_ADD_RECEIVER = downcall(
        "zlink_poller_add_receiver",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_ADD_FD = downcall("zlink_poller_add_fd",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_MODIFY = downcall("zlink_poller_modify",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_MODIFY_SPOT_SUB = downcall(
        "zlink_poller_modify_spot_sub",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_MODIFY_SPOT_PUB = downcall(
        "zlink_poller_modify_spot_pub",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_MODIFY_GATEWAY = downcall(
        "zlink_poller_modify_gateway",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_MODIFY_RECEIVER = downcall(
        "zlink_poller_modify_receiver",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_MODIFY_FD = downcall("zlink_poller_modify_fd",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_POLLER_REMOVE = downcall("zlink_poller_remove",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_REMOVE_SPOT_SUB = downcall(
        "zlink_poller_remove_spot_sub",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_REMOVE_SPOT_PUB = downcall(
        "zlink_poller_remove_spot_pub",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_REMOVE_GATEWAY = downcall(
        "zlink_poller_remove_gateway",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_REMOVE_RECEIVER = downcall(
        "zlink_poller_remove_receiver",
        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.ADDRESS));
    private static final MethodHandle MH_POLLER_REMOVE_FD = downcall("zlink_poller_remove_fd",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));
    private static final MethodHandle MH_POLLER_WAIT_ALL = downcall("zlink_poller_wait_all",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG));

    private static final MethodHandle MH_MONITOR_OPEN = downcall("zlink_socket_monitor_open",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_MONITOR = downcall("zlink_socket_monitor",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_MONITOR_RECV = downcall("zlink_monitor_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_ERRNO = downcall("zlink_errno",
            FunctionDescriptor.of(ValueLayout.JAVA_INT));
    private static final MethodHandle MH_STRERROR = downcall("zlink_strerror",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_HAS = downcall("zlink_has",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SLEEP = downcall("zlink_sleep",
            FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT));

    private static final MethodHandle MH_REG_NEW = downcall("zlink_registry_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_SET_EP = downcall("zlink_registry_set_endpoints",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_SET_ID = downcall("zlink_registry_set_id",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_REG_ADD_PEER = downcall("zlink_registry_add_peer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_SET_HB = downcall("zlink_registry_set_heartbeat",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_REG_SET_BCAST = downcall("zlink_registry_set_broadcast_interval",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_REG_START = downcall("zlink_registry_start",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_DESTROY = downcall("zlink_registry_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_DISC_NEW = downcall("zlink_discovery_new_typed",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT));
    private static final MethodHandle MH_DISC_CONNECT = downcall("zlink_discovery_connect_registry",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_SUB = downcall("zlink_discovery_subscribe",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_UNSUB = downcall("zlink_discovery_unsubscribe",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_GET = downcall("zlink_discovery_get_receivers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_COUNT = downcall("zlink_discovery_receiver_count",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_AVAIL = downcall("zlink_discovery_service_available",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_DESTROY = downcall("zlink_discovery_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_GATEWAY_NEW = downcall("zlink_gateway_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_GATEWAY_SEND = downcall("zlink_gateway_send",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_GATEWAY_SEND_RID_BYTES = downcall("zlink_gateway_send_rid_bytes",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_GATEWAY_SEND_RID = downcall("zlink_gateway_send_rid",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_GATEWAY_RECV = downcall("zlink_gateway_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_GATEWAY_LB = downcall("zlink_gateway_set_lb_strategy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_GATEWAY_TLS = downcall("zlink_gateway_set_tls_client",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_GATEWAY_COUNT = downcall("zlink_gateway_connection_count",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_GATEWAY_ROUTER_PEERS = downcall("zlink_gateway_router_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_GATEWAY_DESTROY = downcall("zlink_gateway_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_PROVIDER_NEW = downcall("zlink_receiver_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_GATEWAY_SET_OPTION = downcall("zlink_gateway_set_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_PROVIDER_SET_OPTION = downcall("zlink_receiver_set_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_REGISTRY_SETSOCKOPT = downcall("zlink_registry_setsockopt",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_PROVIDER_BIND = downcall("zlink_receiver_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_CONN = downcall("zlink_receiver_connect_registry",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_REG = downcall("zlink_receiver_register",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_PROVIDER_UPD = downcall("zlink_receiver_update_weight",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_PROVIDER_UNREG = downcall("zlink_receiver_unregister",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_RESULT = downcall("zlink_receiver_register_result",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_TLS = downcall("zlink_receiver_set_tls_server",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_RECV = downcall("zlink_receiver_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_LAST_ENDPOINT = downcall("zlink_receiver_last_endpoint",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_PEER_INFO = downcall("zlink_receiver_peer_info",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_SET_ROUTING_ID = downcall("zlink_receiver_set_routing_id",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_PROVIDER_ROUTING_ID = downcall("zlink_receiver_routing_id",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_ROUTER_PEERS = downcall("zlink_receiver_router_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_DESTROY = downcall("zlink_receiver_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_SPOT_NODE_NEW = downcall("zlink_spot_node_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_DESTROY = downcall("zlink_spot_node_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_BIND = downcall("zlink_spot_node_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_CONN_PEER = downcall("zlink_spot_node_connect_peer_pub",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_DISC_PEER = downcall("zlink_spot_node_disconnect_peer_pub",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_REG = downcall("zlink_spot_node_register",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_UNREG = downcall("zlink_spot_node_unregister",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_SET_DISC = downcall("zlink_spot_node_set_discovery",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_TLS_SRV = downcall("zlink_spot_node_set_tls_server",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_TLS_CLI = downcall("zlink_spot_node_set_tls_client",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_NODE_SET_PUB_OPTION = downcall("zlink_spot_node_set_pub_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_SPOT_NODE_SET_SUB_OPTION = downcall("zlink_spot_node_set_sub_option",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_SPOT_NODE_DEFAULT_PUB = downcall("zlink_spot_node_default_pub",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_DEFAULT_SUB = downcall("zlink_spot_node_default_sub",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_PUB_PEERS = downcall("zlink_spot_pub_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_PEERS = downcall("zlink_spot_sub_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private static final MethodHandle MH_SPOT_PUB_NEW = downcall("zlink_spot_pub_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_PUB_DESTROY = downcall("zlink_spot_pub_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_PUB_PUBLISH = downcall("zlink_spot_pub_publish",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_SUB_NEW = downcall("zlink_spot_sub_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_DESTROY = downcall("zlink_spot_sub_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_SUBSCRIBE = downcall("zlink_spot_sub_subscribe",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_SUBSCRIBE_PATTERN = downcall("zlink_spot_sub_subscribe_pattern",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_UNSUBSCRIBE = downcall("zlink_spot_sub_unsubscribe",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_RECV = downcall("zlink_spot_sub_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private Native() {}

    public static int[] version() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment major = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment minor = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment patch = arena.allocate(ValueLayout.JAVA_INT);
            MH_VERSION.invokeExact(major, minor, patch);
            return new int[] {
                    major.get(ValueLayout.JAVA_INT, 0),
                    minor.get(ValueLayout.JAVA_INT, 0),
                    patch.get(ValueLayout.JAVA_INT, 0)
            };
        } catch (Throwable t) {
            throw new RuntimeException("zlink_version failed", t);
        }
    }

    public static MemorySegment ctxNew() {
        try {
            return (MemorySegment) MH_CTX_NEW.invokeExact();
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_new failed", t);
        }
    }

    public static int ctxTerm(MemorySegment ctx) {
        try {
            return (int) MH_CTX_TERM.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_term failed", t);
        }
    }

    public static int ctxSet(MemorySegment ctx, int option, int value) {
        try {
            return (int) MH_CTX_SET.invokeExact(ctx, option, value);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_set failed", t);
        }
    }

    public static int ctxGet(MemorySegment ctx, int option) {
        try {
            return (int) MH_CTX_GET.invokeExact(ctx, option);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_get failed", t);
        }
    }

    public static int ctxShutdown(MemorySegment ctx) {
        try {
            return (int) MH_CTX_SHUTDOWN.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_ctx_shutdown failed", t);
        }
    }

    public static MemorySegment socket(MemorySegment ctx, int type) {
        try {
            return (MemorySegment) MH_SOCKET.invokeExact(ctx, type);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_socket failed", t);
        }
    }

    public static int close(MemorySegment socket) {
        try {
            return (int) MH_CLOSE.invokeExact(socket);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_close failed", t);
        }
    }

    public static int bind(MemorySegment socket, MemorySegment addr) {
        try {
            return (int) MH_BIND.invokeExact(socket, addr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_bind failed", t);
        }
    }

    public static int connect(MemorySegment socket, MemorySegment addr) {
        try {
            return (int) MH_CONNECT.invokeExact(socket, addr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_connect failed", t);
        }
    }

    public static int unbind(MemorySegment socket, MemorySegment addr) {
        try {
            return (int) MH_UNBIND.invokeExact(socket, addr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_unbind failed", t);
        }
    }

    public static int disconnect(MemorySegment socket, MemorySegment addr) {
        try {
            return (int) MH_DISCONNECT.invokeExact(socket, addr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_disconnect failed", t);
        }
    }

    public static int send(MemorySegment socket, MemorySegment buf, long len, int flags) {
        try {
            return (int) MH_SEND.invokeExact(socket, buf, len, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_send failed", t);
        }
    }

    public static int recv(MemorySegment socket, MemorySegment buf, long len, int flags) {
        try {
            return (int) MH_RECV.invokeExact(socket, buf, len, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_recv failed", t);
        }
    }

    public static int streamAttach(MemorySegment socket, MemorySegment callback,
                                   int flags) {
        try {
            return (int) MH_STREAM_ATTACH.invokeExact(socket, callback, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_attach failed", t);
        }
    }

    public static int streamAttachRaw(MemorySegment socket,
                                      MemorySegment callback) {
        try {
            return (int) MH_STREAM_ATTACH_RAW.invokeExact(socket, callback);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_attach_raw failed", t);
        }
    }

    public static int streamAttachLen32be(MemorySegment socket,
                                          MemorySegment callback) {
        try {
            return (int) MH_STREAM_ATTACH_LEN32BE.invokeExact(socket, callback);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_attach_len32be failed", t);
        }
    }

    public static int streamDetach(MemorySegment socket) {
        try {
            return (int) MH_STREAM_DETACH.invokeExact(socket);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_detach failed", t);
        }
    }

    public static int streamSend(MemorySegment socket, MemorySegment rid,
                                 MemorySegment payload, long len, int flags) {
        try {
            return (int) MH_STREAM_SEND.invokeExact(socket, rid, payload, len,
              flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_send failed", t);
        }
    }

    public static int streamSendMsg(MemorySegment socket, MemorySegment rid,
                                    MemorySegment msg, int flags) {
        try {
            return (int) MH_STREAM_SEND_MSG.invokeExact(socket, rid, msg, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_stream_send_msg failed", t);
        }
    }

    public static int socketPeerRoutingId(MemorySegment socket, int index,
                                          MemorySegment outRid) {
        try {
            return (int) MH_SOCKET_PEER_ROUTING_ID.invokeExact(socket, index,
              outRid);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_socket_peer_routing_id failed", t);
        }
    }

    public static int setSockOpt(MemorySegment socket, int option, MemorySegment value, long len) {
        try {
            return (int) MH_SETSOCKOPT.invokeExact(socket, option, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_setsockopt failed", t);
        }
    }

    public static int getSockOpt(MemorySegment socket, int option, MemorySegment value, MemorySegment len) {
        try {
            return (int) MH_GETSOCKOPT.invokeExact(socket, option, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_getsockopt failed", t);
        }
    }

    public static MemorySegment monitorOpen(MemorySegment socket, int events) {
        try {
            return (MemorySegment) MH_MONITOR_OPEN.invokeExact(socket, events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_socket_monitor_open failed", t);
        }
    }

    public static int monitor(MemorySegment socket, MemorySegment endpoint,
                              int events) {
        try {
            return (int) MH_MONITOR.invokeExact(socket, endpoint, events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_socket_monitor failed", t);
        }
    }

    public static MonitorEvent monitorRecv(MemorySegment socket, int flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment evt = arena.allocate(NativeLayouts.MONITOR_EVENT_LAYOUT);
            int rc = (int) MH_MONITOR_RECV.invokeExact(socket, evt, flags);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_monitor_recv");
            long event = evt.get(ValueLayout.JAVA_LONG, NativeLayouts.MONITOR_EVENT_OFFSET);
            long value = evt.get(ValueLayout.JAVA_LONG, NativeLayouts.MONITOR_VALUE_OFFSET);
            int routingSize = evt.get(ValueLayout.JAVA_BYTE, NativeLayouts.MONITOR_ROUTING_OFFSET) & 0xFF;
            byte[] routing = new byte[routingSize];
            if (routingSize > 0) {
                MemorySegment.copy(evt, NativeLayouts.MONITOR_ROUTING_OFFSET + 1,
                    MemorySegment.ofArray(routing), 0, routingSize);
            }
            String local = NativeHelpers.fromCString(evt.asSlice(NativeLayouts.MONITOR_LOCAL_OFFSET, 256), 256);
            String remote = NativeHelpers.fromCString(evt.asSlice(NativeLayouts.MONITOR_REMOTE_OFFSET, 256), 256);
            return new MonitorEvent(event, value, routing, local, remote);
        } catch (ZlinkException ex) {
            throw ex;
        } catch (Throwable t) {
            throw new RuntimeException("monitor recv failed", t);
        }
    }

    public static int errno() {
        try {
            return (int) MH_ERRNO.invokeExact();
        } catch (Throwable t) {
            throw new RuntimeException("zlink_errno failed", t);
        }
    }

    public static String strerror(int errnum) {
        try {
            MemorySegment cstr = (MemorySegment) MH_STRERROR.invokeExact(errnum);
            if (cstr == null || cstr.address() == 0)
                return "";
            return cstr.reinterpret(1024).getString(0);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_strerror failed", t);
        }
    }

    public static int has(MemorySegment capability) {
        try {
            return (int) MH_HAS.invokeExact(capability);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_has failed", t);
        }
    }

    public static void sleep(int seconds) {
        try {
            MH_SLEEP.invokeExact(seconds);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_sleep failed", t);
        }
    }

    public static int pollRaw(MemorySegment items, int count, int timeoutMs) {
        if (items == null || items.address() == 0 || count <= 0)
            return 0;
        try {
            return (int) MH_POLL.invokeExact(items, count, (long) timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("poll failed", t);
        }
    }

    public static MemorySegment pollerNew() {
        try {
            return (MemorySegment) MH_POLLER_NEW.invokeExact();
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_new failed", t);
        }
    }

    public static int pollerDestroy(MemorySegment pollerPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, pollerPtr);
            return (int) MH_POLLER_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_destroy failed", t);
        }
    }

    public static int pollerSize(MemorySegment poller) {
        try {
            return (int) MH_POLLER_SIZE.invokeExact(poller);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_size failed", t);
        }
    }

    public static int pollerAdd(MemorySegment poller, MemorySegment socket,
                                MemorySegment userData, int events) {
        try {
            return (int) MH_POLLER_ADD.invokeExact(poller, socket, userData,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add failed", t);
        }
    }

    public static int pollerAddSpotSub(MemorySegment poller,
                                        MemorySegment spotSub,
                                        MemorySegment userData,
                                        int events) {
        try {
            return (int) MH_POLLER_ADD_SPOT_SUB.invokeExact(poller, spotSub,
                userData, (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add_spot_sub failed", t);
        }
    }

    public static int pollerAddSpotPub(MemorySegment poller,
                                        MemorySegment spotPub,
                                        MemorySegment userData,
                                        int events) {
        try {
            return (int) MH_POLLER_ADD_SPOT_PUB.invokeExact(poller, spotPub,
                userData, (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add_spot_pub failed", t);
        }
    }

    public static int pollerAddGateway(MemorySegment poller,
                                        MemorySegment gateway,
                                        MemorySegment userData,
                                        int events) {
        try {
            return (int) MH_POLLER_ADD_GATEWAY.invokeExact(poller, gateway,
                userData, (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add_gateway failed", t);
        }
    }

    public static int pollerAddReceiver(MemorySegment poller,
                                         MemorySegment receiver,
                                         MemorySegment userData,
                                         int events) {
        try {
            return (int) MH_POLLER_ADD_RECEIVER.invokeExact(poller, receiver,
                userData, (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add_receiver failed", t);
        }
    }

    public static int pollerAddFd(MemorySegment poller, int fd,
                                  MemorySegment userData, int events) {
        try {
            return (int) MH_POLLER_ADD_FD.invokeExact(poller, fd, userData,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_add_fd failed", t);
        }
    }

    public static int pollerModify(MemorySegment poller, MemorySegment socket,
                                   int events) {
        try {
            return (int) MH_POLLER_MODIFY.invokeExact(poller, socket,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_modify failed", t);
        }
    }

    public static int pollerModifySpotSub(MemorySegment poller,
                                           MemorySegment spotSub,
                                           int events) {
        try {
            return (int) MH_POLLER_MODIFY_SPOT_SUB.invokeExact(poller, spotSub,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_modify_spot_sub failed",
              t);
        }
    }

    public static int pollerModifySpotPub(MemorySegment poller,
                                           MemorySegment spotPub,
                                           int events) {
        try {
            return (int) MH_POLLER_MODIFY_SPOT_PUB.invokeExact(poller, spotPub,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_modify_spot_pub failed",
              t);
        }
    }

    public static int pollerModifyGateway(MemorySegment poller,
                                           MemorySegment gateway,
                                           int events) {
        try {
            return (int) MH_POLLER_MODIFY_GATEWAY.invokeExact(poller, gateway,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_modify_gateway failed", t);
        }
    }

    public static int pollerModifyReceiver(MemorySegment poller,
                                            MemorySegment receiver,
                                            int events) {
        try {
            return (int) MH_POLLER_MODIFY_RECEIVER.invokeExact(poller,
                receiver, (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_modify_receiver failed",
              t);
        }
    }

    public static int pollerModifyFd(MemorySegment poller, int fd, int events) {
        try {
            return (int) MH_POLLER_MODIFY_FD.invokeExact(poller, fd,
                (short) events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_modify_fd failed", t);
        }
    }

    public static int pollerRemove(MemorySegment poller, MemorySegment socket) {
        try {
            return (int) MH_POLLER_REMOVE.invokeExact(poller, socket);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove failed", t);
        }
    }

    public static int pollerRemoveSpotSub(MemorySegment poller,
                                           MemorySegment spotSub) {
        try {
            return (int) MH_POLLER_REMOVE_SPOT_SUB.invokeExact(poller, spotSub);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove_spot_sub failed",
              t);
        }
    }

    public static int pollerRemoveSpotPub(MemorySegment poller,
                                           MemorySegment spotPub) {
        try {
            return (int) MH_POLLER_REMOVE_SPOT_PUB.invokeExact(poller, spotPub);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove_spot_pub failed",
              t);
        }
    }

    public static int pollerRemoveGateway(MemorySegment poller,
                                           MemorySegment gateway) {
        try {
            return (int) MH_POLLER_REMOVE_GATEWAY.invokeExact(poller, gateway);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove_gateway failed", t);
        }
    }

    public static int pollerRemoveReceiver(MemorySegment poller,
                                            MemorySegment receiver) {
        try {
            return (int) MH_POLLER_REMOVE_RECEIVER.invokeExact(poller,
                receiver);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove_receiver failed",
              t);
        }
    }

    public static int pollerRemoveFd(MemorySegment poller, int fd) {
        try {
            return (int) MH_POLLER_REMOVE_FD.invokeExact(poller, fd);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_remove_fd failed", t);
        }
    }

    public static int pollerWaitAll(MemorySegment poller, MemorySegment events,
                                    int count, int timeoutMs) {
        if (events == null || events.address() == 0 || count <= 0)
            return 0;
        try {
            return (int) MH_POLLER_WAIT_ALL.invokeExact(poller, events, count,
                (long) timeoutMs);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_poller_wait_all failed", t);
        }
    }

    public static MemorySegment registryNew(MemorySegment ctx) {
        try {
            return (MemorySegment) MH_REG_NEW.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_new failed", t);
        }
    }

    public static int registrySetEndpoints(MemorySegment reg, MemorySegment pub, MemorySegment router) {
        try {
            return (int) MH_REG_SET_EP.invokeExact(reg, pub, router);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_set_endpoints failed", t);
        }
    }

    public static int registrySetId(MemorySegment reg, int id) {
        try {
            return (int) MH_REG_SET_ID.invokeExact(reg, id);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_set_id failed", t);
        }
    }

    public static int registryAddPeer(MemorySegment reg, MemorySegment peer) {
        try {
            return (int) MH_REG_ADD_PEER.invokeExact(reg, peer);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_add_peer failed", t);
        }
    }

    public static int registrySetHeartbeat(MemorySegment reg, int interval, int timeout) {
        try {
            return (int) MH_REG_SET_HB.invokeExact(reg, interval, timeout);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_set_heartbeat failed", t);
        }
    }

    public static int registrySetBroadcastInterval(MemorySegment reg, int interval) {
        try {
            return (int) MH_REG_SET_BCAST.invokeExact(reg, interval);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_set_broadcast_interval failed", t);
        }
    }

    public static int registryStart(MemorySegment reg) {
        try {
            return (int) MH_REG_START.invokeExact(reg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_start failed", t);
        }
    }

    public static int registryDestroy(MemorySegment regPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, regPtr);
            return (int) MH_REG_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_destroy failed", t);
        }
    }

    public static MemorySegment discoveryNew(MemorySegment ctx, short serviceType) {
        try {
            return (MemorySegment) MH_DISC_NEW.invokeExact(ctx, serviceType);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_new_typed failed", t);
        }
    }

    public static int discoveryConnectRegistry(MemorySegment disc, MemorySegment pub) {
        try {
            return (int) MH_DISC_CONNECT.invokeExact(disc, pub);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_connect_registry failed", t);
        }
    }

    public static int discoverySubscribe(MemorySegment disc, MemorySegment service) {
        try {
            return (int) MH_DISC_SUB.invokeExact(disc, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_subscribe failed", t);
        }
    }

    public static int discoveryUnsubscribe(MemorySegment disc, MemorySegment service) {
        try {
            return (int) MH_DISC_UNSUB.invokeExact(disc, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_unsubscribe failed", t);
        }
    }

    public static int discoveryGetProviders(MemorySegment disc, MemorySegment service, MemorySegment providers, MemorySegment count) {
        try {
            return (int) MH_DISC_GET.invokeExact(disc, service, providers, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_get_receivers failed", t);
        }
    }

    public static int discoveryProviderCount(MemorySegment disc, MemorySegment service) {
        try {
            return (int) MH_DISC_COUNT.invokeExact(disc, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_receiver_count failed", t);
        }
    }

    public static int discoveryServiceAvailable(MemorySegment disc, MemorySegment service) {
        try {
            return (int) MH_DISC_AVAIL.invokeExact(disc, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_service_available failed", t);
        }
    }

    public static int discoveryDestroy(MemorySegment discPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, discPtr);
            return (int) MH_DISC_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_destroy failed", t);
        }
    }

    public static MemorySegment gatewayNew(MemorySegment ctx, MemorySegment disc, MemorySegment routingId) {
        try {
            return (MemorySegment) MH_GATEWAY_NEW.invokeExact(ctx, disc, routingId);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_new failed", t);
        }
    }

    public static int gatewaySend(MemorySegment gw, MemorySegment service, MemorySegment parts, long count, int flags) {
        try {
            return (int) MH_GATEWAY_SEND.invokeExact(gw, service, parts, count, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_send failed", t);
        }
    }

    public static int gatewaySendRid(MemorySegment gw, MemorySegment service,
                                     MemorySegment routingId,
                                     MemorySegment parts, long count,
                                     int flags) {
        try {
            return (int) MH_GATEWAY_SEND_RID.invokeExact(gw, service, routingId,
              parts, count, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_send_rid failed", t);
        }
    }

    public static int gatewaySendRidBytes(MemorySegment gw, MemorySegment service,
                                          MemorySegment routingId,
                                          MemorySegment data, long size,
                                          int flags) {
        try {
            return (int) MH_GATEWAY_SEND_RID_BYTES.invokeExact(gw, service,
              routingId, data, size, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_send_rid_bytes failed", t);
        }
    }

    public static int gatewayRecv(MemorySegment gw, MemorySegment partsPtr, MemorySegment count, int flags, MemorySegment serviceOut) {
        try {
            return (int) MH_GATEWAY_RECV.invokeExact(gw, partsPtr, count, flags, serviceOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_recv failed", t);
        }
    }

    public static int gatewaySetLbStrategy(MemorySegment gw, MemorySegment service, int strategy) {
        try {
            return (int) MH_GATEWAY_LB.invokeExact(gw, service, strategy);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_set_lb_strategy failed", t);
        }
    }

    public static int gatewaySetTlsClient(MemorySegment gw, MemorySegment ca, MemorySegment host, int trust) {
        try {
            return (int) MH_GATEWAY_TLS.invokeExact(gw, ca, host, trust);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_set_tls_client failed", t);
        }
    }

    public static int gatewayConnectionCount(MemorySegment gw, MemorySegment service) {
        try {
            return (int) MH_GATEWAY_COUNT.invokeExact(gw, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_connection_count failed", t);
        }
    }

    public static int gatewayRouterPeers(MemorySegment gw, MemorySegment peers,
                                         MemorySegment count) {
        try {
            return (int) MH_GATEWAY_ROUTER_PEERS.invokeExact(gw, peers, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_router_peers failed", t);
        }
    }

    public static int gatewaySetOption(MemorySegment gw, int option,
                                       MemorySegment value, long len) {
        try {
            return (int) MH_GATEWAY_SET_OPTION.invokeExact(gw, option, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_set_option failed", t);
        }
    }

    public static int gatewayDestroy(MemorySegment gwPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, gwPtr);
            return (int) MH_GATEWAY_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_destroy failed", t);
        }
    }

    public static MemorySegment providerNew(MemorySegment ctx, MemorySegment routingId) {
        try {
            return (MemorySegment) MH_PROVIDER_NEW.invokeExact(ctx, routingId);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_new failed", t);
        }
    }

    public static int providerBind(MemorySegment p, MemorySegment ep) {
        try {
            return (int) MH_PROVIDER_BIND.invokeExact(p, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_bind failed", t);
        }
    }

    public static int providerConnectRegistry(MemorySegment p, MemorySegment ep) {
        try {
            return (int) MH_PROVIDER_CONN.invokeExact(p, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_connect_registry failed", t);
        }
    }

    public static int providerRegister(MemorySegment p, MemorySegment service, MemorySegment ep, int weight) {
        try {
            return (int) MH_PROVIDER_REG.invokeExact(p, service, ep, weight);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_register failed", t);
        }
    }

    public static int providerUpdateWeight(MemorySegment p, MemorySegment service, int weight) {
        try {
            return (int) MH_PROVIDER_UPD.invokeExact(p, service, weight);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_update_weight failed", t);
        }
    }

    public static int providerUnregister(MemorySegment p, MemorySegment service) {
        try {
            return (int) MH_PROVIDER_UNREG.invokeExact(p, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_unregister failed", t);
        }
    }

    public static int providerRegisterResult(MemorySegment p, MemorySegment service, MemorySegment status, MemorySegment resolved, MemorySegment error) {
        try {
            return (int) MH_PROVIDER_RESULT.invokeExact(p, service, status, resolved, error);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_register_result failed", t);
        }
    }

    public static int providerSetTlsServer(MemorySegment p, MemorySegment cert, MemorySegment key) {
        try {
            return (int) MH_PROVIDER_TLS.invokeExact(p, cert, key);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_set_tls_server failed", t);
        }
    }

    public static int providerRecv(MemorySegment p, MemorySegment parts,
                                   MemorySegment partCount, int flags,
                                   MemorySegment routingId) {
        try {
            return (int) MH_PROVIDER_RECV.invokeExact(p, parts, partCount, flags,
              routingId);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_recv failed", t);
        }
    }

    public static int providerLastEndpoint(MemorySegment p,
                                           MemorySegment endpoint,
                                           MemorySegment size) {
        try {
            return (int) MH_PROVIDER_LAST_ENDPOINT.invokeExact(p, endpoint, size);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_last_endpoint failed", t);
        }
    }

    public static int providerPeerInfo(MemorySegment p, MemorySegment routingId,
                                       MemorySegment info) {
        try {
            return (int) MH_PROVIDER_PEER_INFO.invokeExact(p, routingId, info);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_peer_info failed", t);
        }
    }

    public static int providerRouterPeers(MemorySegment p, MemorySegment peers,
                                          MemorySegment count) {
        try {
            return (int) MH_PROVIDER_ROUTER_PEERS.invokeExact(p, peers, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_router_peers failed", t);
        }
    }

    public static int providerDestroy(MemorySegment pPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, pPtr);
            return (int) MH_PROVIDER_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_destroy failed", t);
        }
    }

    public static int providerSetOption(MemorySegment p, int option,
                                        MemorySegment value, long len) {
        try {
            return (int) MH_PROVIDER_SET_OPTION.invokeExact(p, option, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_set_option failed", t);
        }
    }

    public static int registrySetSockOpt(MemorySegment r, int role, int option, MemorySegment value, long len) {
        try {
            return (int) MH_REGISTRY_SETSOCKOPT.invokeExact(r, role, option, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_setsockopt failed", t);
        }
    }

    public static int providerSetRoutingId(MemorySegment p, MemorySegment value,
                                           long len) {
        try {
            return (int) MH_PROVIDER_SET_ROUTING_ID.invokeExact(p, value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_set_routing_id failed", t);
        }
    }

    public static int providerRoutingId(MemorySegment p, MemorySegment routingId) {
        try {
            return (int) MH_PROVIDER_ROUTING_ID.invokeExact(p, routingId);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_receiver_routing_id failed", t);
        }
    }

    public static MemorySegment spotNodeNew(MemorySegment ctx) {
        try {
            return (MemorySegment) MH_SPOT_NODE_NEW.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_new failed", t);
        }
    }

    public static int spotNodeDestroy(MemorySegment nodePtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, nodePtr);
            return (int) MH_SPOT_NODE_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_destroy failed", t);
        }
    }

    public static int spotNodeBind(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_BIND.invokeExact(node, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_bind failed", t);
        }
    }

    public static int spotNodeConnectPeer(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_CONN_PEER.invokeExact(node, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_connect_peer_pub failed", t);
        }
    }

    public static int spotNodeDisconnectPeer(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_DISC_PEER.invokeExact(node, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_disconnect_peer_pub failed", t);
        }
    }

    public static int spotNodeRegister(MemorySegment node, MemorySegment service, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_REG.invokeExact(node, service, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_register failed", t);
        }
    }

    public static int spotNodeUnregister(MemorySegment node, MemorySegment service) {
        try {
            return (int) MH_SPOT_NODE_UNREG.invokeExact(node, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_unregister failed", t);
        }
    }

    public static int spotNodeSetDiscovery(MemorySegment node, MemorySegment disc, MemorySegment service) {
        try {
            return (int) MH_SPOT_NODE_SET_DISC.invokeExact(node, disc, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_discovery failed", t);
        }
    }

    public static int spotNodeSetTlsServer(MemorySegment node, MemorySegment cert, MemorySegment key) {
        try {
            return (int) MH_SPOT_NODE_TLS_SRV.invokeExact(node, cert, key);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_tls_server failed", t);
        }
    }

    public static int spotNodeSetTlsClient(MemorySegment node, MemorySegment ca, MemorySegment host, int trust) {
        try {
            return (int) MH_SPOT_NODE_TLS_CLI.invokeExact(node, ca, host, trust);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_tls_client failed", t);
        }
    }

    public static int spotNodeSetPubOption(MemorySegment node, int option,
                                           MemorySegment value, long len) {
        try {
            return (int) MH_SPOT_NODE_SET_PUB_OPTION.invokeExact(node, option,
              value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_pub_option failed", t);
        }
    }

    public static int spotNodeSetSubOption(MemorySegment node, int option,
                                           MemorySegment value, long len) {
        try {
            return (int) MH_SPOT_NODE_SET_SUB_OPTION.invokeExact(node, option,
              value, len);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_set_sub_option failed", t);
        }
    }

    public static MemorySegment spotNodeDefaultPub(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_NODE_DEFAULT_PUB.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_default_pub failed",
              t);
        }
    }

    public static MemorySegment spotNodeDefaultSub(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_NODE_DEFAULT_SUB.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_default_sub failed",
              t);
        }
    }

    public static int spotPubPeers(MemorySegment pub, MemorySegment peers,
                                   MemorySegment count) {
        try {
            return (int) MH_SPOT_PUB_PEERS.invokeExact(pub, peers, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_pub_peers failed", t);
        }
    }

    public static int spotSubPeers(MemorySegment sub, MemorySegment peers,
                                   MemorySegment count) {
        try {
            return (int) MH_SPOT_SUB_PEERS.invokeExact(sub, peers, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_peers failed", t);
        }
    }

    public static MemorySegment spotPubNew(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_PUB_NEW.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_pub_new failed", t);
        }
    }

    public static int spotPubDestroy(MemorySegment pubPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, pubPtr);
            return (int) MH_SPOT_PUB_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_pub_destroy failed", t);
        }
    }

    public static int spotPubPublish(MemorySegment pub, MemorySegment topic, MemorySegment parts, long count, int flags) {
        try {
            return (int) MH_SPOT_PUB_PUBLISH.invokeExact(pub, topic, parts, count, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_pub_publish failed", t);
        }
    }

    public static MemorySegment spotSubNew(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_SUB_NEW.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_new failed", t);
        }
    }

    public static int spotSubDestroy(MemorySegment subPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, subPtr);
            return (int) MH_SPOT_SUB_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_destroy failed", t);
        }
    }

    public static int spotSubSubscribe(MemorySegment sub, MemorySegment topic) {
        try {
            return (int) MH_SPOT_SUB_SUBSCRIBE.invokeExact(sub, topic);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_subscribe failed", t);
        }
    }

    public static int spotSubSubscribePattern(MemorySegment sub, MemorySegment pattern) {
        try {
            return (int) MH_SPOT_SUB_SUBSCRIBE_PATTERN.invokeExact(sub, pattern);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_subscribe_pattern failed", t);
        }
    }

    public static int spotSubUnsubscribe(MemorySegment sub, MemorySegment topic) {
        try {
            return (int) MH_SPOT_SUB_UNSUBSCRIBE.invokeExact(sub, topic);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_unsubscribe failed", t);
        }
    }

    public static int spotSubRecv(MemorySegment sub, MemorySegment partsPtr, MemorySegment count, int flags, MemorySegment topicOut, MemorySegment topicLen) {
        try {
            return (int) MH_SPOT_SUB_RECV.invokeExact(sub, partsPtr, count, flags, topicOut, topicLen);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_recv failed", t);
        }
    }

}
