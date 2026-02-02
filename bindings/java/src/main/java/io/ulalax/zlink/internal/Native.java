package io.ulalax.zlink.internal;

import io.ulalax.zlink.MonitorEvent;
import io.ulalax.zlink.Poller;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.util.ArrayList;
import java.util.List;

public final class Native {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP = LibraryLoader.lookup();

    private static MethodHandle downcall(String name, FunctionDescriptor fd) {
        return LINKER.downcallHandle(LOOKUP.find(name).orElseThrow(), fd);
    }

    private static final MethodHandle MH_VERSION = downcall("zlink_version",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_CTX_NEW = downcall("zlink_ctx_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS));
    private static final MethodHandle MH_CTX_TERM = downcall("zlink_ctx_term",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SOCKET = downcall("zlink_socket",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_CLOSE = downcall("zlink_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_BIND = downcall("zlink_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_CONNECT = downcall("zlink_connect",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SEND = downcall("zlink_send",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_RECV = downcall("zlink_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));

    private static final MethodHandle MH_POLL = downcall("zlink_poll",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG));

    private static final MethodHandle MH_MONITOR_OPEN = downcall("zlink_socket_monitor_open",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_MONITOR_RECV = downcall("zlink_monitor_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

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

    private static final MethodHandle MH_DISC_NEW = downcall("zlink_discovery_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_CONNECT = downcall("zlink_discovery_connect_registry",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_SUB = downcall("zlink_discovery_subscribe",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_UNSUB = downcall("zlink_discovery_unsubscribe",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_GET = downcall("zlink_discovery_get_providers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_COUNT = downcall("zlink_discovery_provider_count",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_AVAIL = downcall("zlink_discovery_service_available",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_DESTROY = downcall("zlink_discovery_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_GATEWAY_NEW = downcall("zlink_gateway_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_GATEWAY_SEND = downcall("zlink_gateway_send",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_GATEWAY_RECV = downcall("zlink_gateway_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_GATEWAY_LB = downcall("zlink_gateway_set_lb_strategy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_GATEWAY_TLS = downcall("zlink_gateway_set_tls_client",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_GATEWAY_COUNT = downcall("zlink_gateway_connection_count",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_GATEWAY_DESTROY = downcall("zlink_gateway_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_PROVIDER_NEW = downcall("zlink_provider_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_BIND = downcall("zlink_provider_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_CONN = downcall("zlink_provider_connect_registry",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_REG = downcall("zlink_provider_register",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_PROVIDER_UPD = downcall("zlink_provider_update_weight",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_PROVIDER_UNREG = downcall("zlink_provider_unregister",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_RESULT = downcall("zlink_provider_register_result",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_TLS = downcall("zlink_provider_set_tls_server",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_ROUTER = downcall("zlink_provider_router",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_PROVIDER_DESTROY = downcall("zlink_provider_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_SPOT_NODE_NEW = downcall("zlink_spot_node_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_DESTROY = downcall("zlink_spot_node_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_BIND = downcall("zlink_spot_node_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_CONN_REG = downcall("zlink_spot_node_connect_registry",
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
    private static final MethodHandle MH_SPOT_NODE_PUB = downcall("zlink_spot_node_pub_socket",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_NODE_SUB = downcall("zlink_spot_node_sub_socket",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private static final MethodHandle MH_SPOT_NEW = downcall("zlink_spot_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_DESTROY = downcall("zlink_spot_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_TOPIC_CREATE = downcall("zlink_spot_topic_create",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_TOPIC_DESTROY = downcall("zlink_spot_topic_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_PUBLISH = downcall("zlink_spot_publish",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_SPOT_SUB = downcall("zlink_spot_subscribe",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_PATTERN = downcall("zlink_spot_subscribe_pattern",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_UNSUB = downcall("zlink_spot_unsubscribe",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_RECV = downcall("zlink_spot_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_PUB = downcall("zlink_spot_pub_socket",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_SPOT_SUB_S = downcall("zlink_spot_sub_socket",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));

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

    public static MemorySegment monitorOpen(MemorySegment socket, int events) {
        try {
            return (MemorySegment) MH_MONITOR_OPEN.invokeExact(socket, events);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_socket_monitor_open failed", t);
        }
    }

    public static MonitorEvent monitorRecv(MemorySegment socket, int flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment evt = arena.allocate(NativeLayouts.MONITOR_EVENT_LAYOUT);
            int rc = (int) MH_MONITOR_RECV.invokeExact(socket, evt, flags);
            if (rc != 0)
                throw new RuntimeException("zlink_monitor_recv failed");
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
        } catch (Throwable t) {
            throw new RuntimeException("monitor recv failed", t);
        }
    }

    public static List<Poller.PollEvent> poll(List<Poller.PollItem> items, int timeoutMs) {
        if (items.isEmpty())
            return List.of();
        try (Arena arena = Arena.ofConfined()) {
            long itemSize = 24;
            MemorySegment arr = arena.allocate(itemSize * items.size());
            for (int i = 0; i < items.size(); i++) {
                Poller.PollItem it = items.get(i);
                long base = i * itemSize;
                MemorySegment sock = it.socket == null ? MemorySegment.NULL : it.socket.handle();
                arr.set(ValueLayout.ADDRESS, base, sock);
                arr.set(ValueLayout.JAVA_INT, base + 8, it.fd);
                arr.set(ValueLayout.JAVA_SHORT, base + 12, (short) it.events);
                arr.set(ValueLayout.JAVA_SHORT, base + 14, (short) 0);
            }
            int rc = (int) MH_POLL.invokeExact(arr, items.size(), (long) timeoutMs);
            if (rc < 0)
                throw new RuntimeException("zlink_poll failed");
            List<Poller.PollEvent> out = new ArrayList<>();
            for (int i = 0; i < items.size(); i++) {
                long base = i * itemSize;
                short revents = arr.get(ValueLayout.JAVA_SHORT, base + 14);
                if (revents != 0)
                    out.add(new Poller.PollEvent(items.get(i).socket, revents));
            }
            return out;
        } catch (Throwable t) {
            throw new RuntimeException("poll failed", t);
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
        try {
            return (int) MH_REG_DESTROY.invokeExact(regPtr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_destroy failed", t);
        }
    }

    public static MemorySegment discoveryNew(MemorySegment ctx) {
        try {
            return (MemorySegment) MH_DISC_NEW.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_new failed", t);
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
            throw new RuntimeException("zlink_discovery_get_providers failed", t);
        }
    }

    public static int discoveryProviderCount(MemorySegment disc, MemorySegment service) {
        try {
            return (int) MH_DISC_COUNT.invokeExact(disc, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_provider_count failed", t);
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
        try {
            return (int) MH_DISC_DESTROY.invokeExact(discPtr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_destroy failed", t);
        }
    }

    public static MemorySegment gatewayNew(MemorySegment ctx, MemorySegment disc) {
        try {
            return (MemorySegment) MH_GATEWAY_NEW.invokeExact(ctx, disc);
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

    public static int gatewayDestroy(MemorySegment gwPtr) {
        try {
            return (int) MH_GATEWAY_DESTROY.invokeExact(gwPtr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_gateway_destroy failed", t);
        }
    }

    public static MemorySegment providerNew(MemorySegment ctx) {
        try {
            return (MemorySegment) MH_PROVIDER_NEW.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_provider_new failed", t);
        }
    }

    public static int providerBind(MemorySegment p, MemorySegment ep) {
        try {
            return (int) MH_PROVIDER_BIND.invokeExact(p, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_provider_bind failed", t);
        }
    }

    public static int providerConnectRegistry(MemorySegment p, MemorySegment ep) {
        try {
            return (int) MH_PROVIDER_CONN.invokeExact(p, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_provider_connect_registry failed", t);
        }
    }

    public static int providerRegister(MemorySegment p, MemorySegment service, MemorySegment ep, int weight) {
        try {
            return (int) MH_PROVIDER_REG.invokeExact(p, service, ep, weight);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_provider_register failed", t);
        }
    }

    public static int providerUpdateWeight(MemorySegment p, MemorySegment service, int weight) {
        try {
            return (int) MH_PROVIDER_UPD.invokeExact(p, service, weight);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_provider_update_weight failed", t);
        }
    }

    public static int providerUnregister(MemorySegment p, MemorySegment service) {
        try {
            return (int) MH_PROVIDER_UNREG.invokeExact(p, service);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_provider_unregister failed", t);
        }
    }

    public static int providerRegisterResult(MemorySegment p, MemorySegment service, MemorySegment status, MemorySegment resolved, MemorySegment error) {
        try {
            return (int) MH_PROVIDER_RESULT.invokeExact(p, service, status, resolved, error);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_provider_register_result failed", t);
        }
    }

    public static int providerSetTlsServer(MemorySegment p, MemorySegment cert, MemorySegment key) {
        try {
            return (int) MH_PROVIDER_TLS.invokeExact(p, cert, key);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_provider_set_tls_server failed", t);
        }
    }

    public static MemorySegment providerRouter(MemorySegment p) {
        try {
            return (MemorySegment) MH_PROVIDER_ROUTER.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_provider_router failed", t);
        }
    }

    public static int providerDestroy(MemorySegment pPtr) {
        try {
            return (int) MH_PROVIDER_DESTROY.invokeExact(pPtr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_provider_destroy failed", t);
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
        try {
            return (int) MH_SPOT_NODE_DESTROY.invokeExact(nodePtr);
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

    public static int spotNodeConnectRegistry(MemorySegment node, MemorySegment ep) {
        try {
            return (int) MH_SPOT_NODE_CONN_REG.invokeExact(node, ep);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_connect_registry failed", t);
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

    public static MemorySegment spotNodePubSocket(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_NODE_PUB.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_pub_socket failed", t);
        }
    }

    public static MemorySegment spotNodeSubSocket(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_NODE_SUB.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_node_sub_socket failed", t);
        }
    }

    public static MemorySegment spotNew(MemorySegment node) {
        try {
            return (MemorySegment) MH_SPOT_NEW.invokeExact(node);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_new failed", t);
        }
    }

    public static int spotDestroy(MemorySegment spotPtr) {
        try {
            return (int) MH_SPOT_DESTROY.invokeExact(spotPtr);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_destroy failed", t);
        }
    }

    public static int spotTopicCreate(MemorySegment spot, MemorySegment topic, int mode) {
        try {
            return (int) MH_SPOT_TOPIC_CREATE.invokeExact(spot, topic, mode);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_topic_create failed", t);
        }
    }

    public static int spotTopicDestroy(MemorySegment spot, MemorySegment topic) {
        try {
            return (int) MH_SPOT_TOPIC_DESTROY.invokeExact(spot, topic);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_topic_destroy failed", t);
        }
    }

    public static int spotPublish(MemorySegment spot, MemorySegment topic, MemorySegment parts, long count, int flags) {
        try {
            return (int) MH_SPOT_PUBLISH.invokeExact(spot, topic, parts, count, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_publish failed", t);
        }
    }

    public static int spotSubscribe(MemorySegment spot, MemorySegment topic) {
        try {
            return (int) MH_SPOT_SUB.invokeExact(spot, topic);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_subscribe failed", t);
        }
    }

    public static int spotSubscribePattern(MemorySegment spot, MemorySegment pattern) {
        try {
            return (int) MH_SPOT_SUB_PATTERN.invokeExact(spot, pattern);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_subscribe_pattern failed", t);
        }
    }

    public static int spotUnsubscribe(MemorySegment spot, MemorySegment topic) {
        try {
            return (int) MH_SPOT_UNSUB.invokeExact(spot, topic);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_unsubscribe failed", t);
        }
    }

    public static int spotRecv(MemorySegment spot, MemorySegment partsPtr, MemorySegment count, int flags, MemorySegment topicOut, MemorySegment topicLen) {
        try {
            return (int) MH_SPOT_RECV.invokeExact(spot, partsPtr, count, flags, topicOut, topicLen);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_recv failed", t);
        }
    }

    public static MemorySegment spotPubSocket(MemorySegment spot) {
        try {
            return (MemorySegment) MH_SPOT_PUB.invokeExact(spot);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_pub_socket failed", t);
        }
    }

    public static MemorySegment spotSubSocket(MemorySegment spot) {
        try {
            return (MemorySegment) MH_SPOT_SUB_S.invokeExact(spot);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_spot_sub_socket failed", t);
        }
    }
}
