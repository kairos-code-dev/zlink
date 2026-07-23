package systems.zlink.framework.runtime.channels;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.reflect.Proxy;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.locations.ZLinkClientServerServerDescriptor;
import systems.zlink.framework.locations.ZLinkClientServerLocationStore;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationWriteResult;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.runtime.backend.ZLinkBackendDealerSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.backend.ZLinkChannelBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestCallback;
import systems.zlink.framework.runtime.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendRouterSocket;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;

final class ZLinkClientServerM6ARuntimeTest {
    @Test
    void serviceWirePreservesOpaqueNonUtf8RoutingIdBytes() {
        RoutingId rid = RoutingId.from(new byte[] {
            0x00, (byte) 0xff, (byte) 0x80, 0x41
        });
        ZLinkClientServerServerDescriptor descriptor =
            descriptor("orders", rid, 7, 11, "tcp://127.0.0.1:7001", 25);

        byte[] encoded =
            ZLinkClientServerServiceWire.encodeAdmit(
                descriptor, 1024 * 1024);
        ZLinkClientServerServiceWire.Admit decoded =
            (ZLinkClientServerServiceWire.Admit)
                ZLinkClientServerServiceWire.decode(encoded);

        assertArrayEquals(
            rid.toBytes(), decoded.admission().serverRid().toBytes());
        assertEquals(7, decoded.admission().lifecycleGeneration());
        assertEquals(11, decoded.admission().descriptorRevision());
    }

    @Test
    void lifecycleConnectionIdUsesCanonicalRawRoutingIdBytes() {
        ZLinkClientServerServerDescriptor textRid =
            descriptor(
                "orders",
                RoutingId.from("1234"),
                9,
                1,
                "tcp://127.0.0.1:7001",
                50);
        ZLinkClientServerServerDescriptor integerRid =
            descriptor(
                "orders",
                RoutingId.from(1234L),
                9,
                1,
                "tcp://127.0.0.1:7001",
                50);

        assertEquals(
            textRid.serverRid().toString(),
            integerRid.serverRid().toString());
        assertNotEquals(
            ZLinkClientServerLocationRuntime.connectionId(textRid),
            ZLinkClientServerLocationRuntime.connectionId(integerRid));
    }

    @Test
    void weightedSelectionUsesOnlyAdmittedServingConnections() {
        ZLinkChannelSocketRegistry sockets =
            new ZLinkChannelSocketRegistry();
        ZLinkBackendDealerSocket first = dealer("first");
        ZLinkBackendDealerSocket second = dealer("second");
        ZLinkClientServerServerDescriptor firstDescriptor =
            descriptor(
                "orders", RoutingId.from("first"), 1, 1,
                "tcp://127.0.0.1:7001", 1);
        ZLinkClientServerServerDescriptor secondDescriptor =
            descriptor(
                "orders", RoutingId.from("second"), 1, 1,
                "tcp://127.0.0.1:7002", 3);
        sockets.addClientServerConnection("first", firstDescriptor, first);
        sockets.addClientServerConnection("second", secondDescriptor, second);

        assertNull(sockets.clientForOutbound("orders"));
        sockets.admitClientServerConnection("first", firstDescriptor);
        sockets.admitClientServerConnection("second", secondDescriptor);

        assertSame(first, sockets.clientForOutbound("orders"));
        assertSame(second, sockets.clientForOutbound("orders"));
        assertSame(second, sockets.clientForOutbound("orders"));
        assertSame(second, sockets.clientForOutbound("orders"));
        assertSame(first, sockets.clientForOutbound("orders"));
    }

    @Test
    void lateOldLifecycleRemovalCannotRemoveNewReadyConnection() {
        ZLinkChannelSocketRegistry sockets =
            new ZLinkChannelSocketRegistry();
        RoutingId rid = RoutingId.from(new byte[] {
            0x01, (byte) 0xf0, 0x02
        });
        ZLinkBackendDealerSocket oldDealer = dealer("old");
        ZLinkBackendDealerSocket newDealer = dealer("new");
        ZLinkClientServerServerDescriptor oldDescriptor =
            descriptor(
                "orders", rid, 10, 1,
                "tcp://127.0.0.1:7001", 100);
        ZLinkClientServerServerDescriptor newDescriptor =
            descriptor(
                "orders", rid, 11, 1,
                "tcp://127.0.0.1:7001", 100);
        sockets.addClientServerConnection(
            "orders/old", oldDescriptor, oldDealer);
        sockets.admitClientServerConnection(
            "orders/old", oldDescriptor);
        sockets.addClientServerConnection(
            "orders/new", newDescriptor, newDealer);

        assertSame(oldDealer, sockets.clientForOutbound("orders"));
        sockets.admitClientServerConnection(
            "orders/new", newDescriptor);
        sockets.removeClientServerConnection("orders/old");

        assertSame(newDealer, sockets.clientForOutbound("orders"));
    }

    @Test
    void reservedHelloIsConsumedBeforeApplicationDispatch() {
        ZLinkChannelSocketRegistry sockets =
            new ZLinkChannelSocketRegistry();
        ZLinkClientServerServerDescriptor descriptor =
            descriptor(
                "orders", RoutingId.from("server"), 5, 2,
                "tcp://127.0.0.1:7001", 80);
        sockets.setClientServerServerDescriptor("orders", descriptor);
        byte[] hello = ZLinkClientServerServiceWire.encodeHello(
            new ZLinkClientServerServiceWire.Hello(
                "orders", "default", 4096));
        List<Message> reply = new ArrayList<>();
        Message request = Message.from(hello);
        ZLinkBackendReceived received = new ZLinkBackendReceived(
            ZLinkBackendRequestResult.OK,
            Optional.of(RoutingId.from("client")),
            Optional.empty(),
            Optional.of(1L),
            List.of(request),
            parts -> {
                for (Message part : parts) {
                    reply.add(Message.from(part));
                }
            },
            () -> { });

        assertTrue(sockets.tryHandleClientServerControl(
            "orders", router(), received));
        assertEquals(1, reply.size());
        try (Message response = reply.get(0)) {
            ZLinkClientServerServiceWire.Admit admit =
                (ZLinkClientServerServiceWire.Admit)
                    ZLinkClientServerServiceWire.decode(
                        response.toByteArray());
            assertEquals(
                descriptor.serverRid(), admit.admission().serverRid());
        }
    }

    @Test
    void storeDiscoveredConnectionBecomesReadyOnlyAfterExactAdmission() {
        ZLinkClientServerServerDescriptor descriptor =
            descriptor(
                "orders",
                RoutingId.from(new byte[] {
                    0x05, (byte) 0xff, 0x22
                }),
                8,
                3,
                "tcp://127.0.0.1:7010",
                70);
        ZLinkChannelSocketRegistry sockets =
            new ZLinkChannelSocketRegistry();
        ZLinkBackendDealerSocket dealer =
            admittingDealer("automatic", descriptor);
        ZLinkChannelBackendAdapter adapter =
            (ZLinkChannelBackendAdapter) Proxy.newProxyInstance(
                ZLinkChannelBackendAdapter.class.getClassLoader(),
                new Class<?>[] {ZLinkChannelBackendAdapter.class},
                (proxy, method, arguments) -> switch (method.getName()) {
                    case "createDealerSocket" -> dealer;
                    default -> throw new UnsupportedOperationException(
                        method.getName());
                });
        ZLinkBackendAdapterProvider provider =
            (ZLinkBackendAdapterProvider) Proxy.newProxyInstance(
                ZLinkBackendAdapterProvider.class.getClassLoader(),
                new Class<?>[] {ZLinkBackendAdapterProvider.class},
                (proxy, method, arguments) -> switch (method.getName()) {
                    case "createChannelAdapter" -> adapter;
                    default -> throw new UnsupportedOperationException(
                        method.getName());
                });
        ZLinkClientServerLocationStore store =
            new SingleDescriptorStore(descriptor);
        ZLinkBackendContext context =
            (ZLinkBackendContext) Proxy.newProxyInstance(
                ZLinkBackendContext.class.getClassLoader(),
                new Class<?>[] {ZLinkBackendContext.class},
                (proxy, method, arguments) -> switch (method.getName()) {
                    case "name" -> "context";
                    case "close", "shutdown" -> null;
                    default -> throw new UnsupportedOperationException(
                        method.getName());
                });
        ZLinkClientServerLocationRuntime runtime =
            new ZLinkClientServerLocationRuntime(
                store,
                () -> new ZLinkLocationOwnerToken("owner", 1),
                provider,
                context,
                new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)),
                sockets,
                Duration.ofHours(1),
                100);
        ZLinkChannelRuntime.AutoConnectSurface client =
            new ZLinkChannelRuntime.AutoConnectSurface(
                systems.zlink.framework.locations
                    .ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                systems.zlink.framework.locations.ZLinkLocationRole.DEALER,
                RoutingId.from("client"),
                "",
                100,
                null,
                List.of());

        runtime.start(List.of(client)).toCompletableFuture().join();

        assertSame(dealer, sockets.clientForOutbound("orders"));
        runtime.stop().toCompletableFuture().join();
    }

    private static ZLinkClientServerServerDescriptor descriptor(
        String channelName,
        RoutingId rid,
        long lifecycle,
        long revision,
        String endpoint,
        int weight) {
        return new ZLinkClientServerServerDescriptor(
            channelName,
            rid,
            lifecycle,
            revision,
            endpoint,
            weight,
            ZLinkFrameworkRuntimeState.SERVING,
            "default",
            "owner",
            1,
            Instant.EPOCH);
    }

    private static ZLinkBackendDealerSocket dealer(String name) {
        return (ZLinkBackendDealerSocket) Proxy.newProxyInstance(
            ZLinkBackendDealerSocket.class.getClassLoader(),
            new Class<?>[] {ZLinkBackendDealerSocket.class},
            (proxy, method, arguments) -> switch (method.getName()) {
                case "name" -> name;
                case "equals" -> proxy == arguments[0];
                case "hashCode" -> System.identityHashCode(proxy);
                case "close", "connect", "disconnect", "bind",
                    "setChannelName" -> null;
                case "send", "request" -> false;
                case "recv" -> null;
                default -> throw new UnsupportedOperationException(
                    method.getName());
            });
    }

    private static ZLinkBackendDealerSocket admittingDealer(
        String name,
        ZLinkClientServerServerDescriptor descriptor) {
        return (ZLinkBackendDealerSocket) Proxy.newProxyInstance(
            ZLinkBackendDealerSocket.class.getClassLoader(),
            new Class<?>[] {ZLinkBackendDealerSocket.class},
            (proxy, method, arguments) -> switch (method.getName()) {
                case "name" -> name;
                case "equals" -> proxy == arguments[0];
                case "hashCode" -> System.identityHashCode(proxy);
                case "close", "connect", "disconnect", "bind",
                    "setChannelName" -> null;
                case "request" -> {
                    ZLinkBackendRequestCallback callback =
                        (ZLinkBackendRequestCallback) arguments[1];
                    Message response = Message.from(
                        ZLinkClientServerServiceWire.encodeAdmit(
                            descriptor, Integer.MAX_VALUE));
                    callback.handle(new ZLinkBackendReceived(
                        ZLinkBackendRequestResult.OK,
                        Optional.empty(),
                        Optional.empty(),
                        Optional.empty(),
                        List.of(response)));
                    yield true;
                }
                case "send" -> true;
                case "recv" -> null;
                default -> throw new UnsupportedOperationException(
                    method.getName());
            });
    }

    private static final class SingleDescriptorStore
        implements ZLinkClientServerLocationStore {
        private final ZLinkClientServerServerDescriptor descriptor;

        private SingleDescriptorStore(
            ZLinkClientServerServerDescriptor descriptor) {
            this.descriptor = descriptor;
        }

        @Override
        public java.util.concurrent.CompletionStage<ZLinkLocationWriteResult>
            updateClientServer(
                ZLinkClientServerServerDescriptor value,
                systems.zlink.framework.locations.ZLinkLocationWriteIntent intent) {
            return CompletableFuture.completedFuture(
                ZLinkLocationWriteResult.stored(
                    value.leaseGeneration(), Instant.now()));
        }

        @Override
        public java.util.concurrent.CompletionStage<ZLinkLocationWriteStatus>
            removeClientServer(
                systems.zlink.framework.locations
                    .ZLinkClientServerServerDescriptorKey key,
                ZLinkLocationOwnerToken owner) {
            return CompletableFuture.completedFuture(
                ZLinkLocationWriteStatus.STORED);
        }

        @Override
        public java.util.concurrent.CompletionStage<
            ZLinkLocationPage<ZLinkClientServerServerDescriptor>>
            listClientServers(
                String channelName,
                systems.zlink.framework.locations.ZLinkPageRequest page) {
            return CompletableFuture.completedFuture(
                new ZLinkLocationPage<>(List.of(descriptor), null));
        }
    }

    private static ZLinkBackendRouterSocket router() {
        return (ZLinkBackendRouterSocket) Proxy.newProxyInstance(
            ZLinkBackendRouterSocket.class.getClassLoader(),
            new Class<?>[] {ZLinkBackendRouterSocket.class},
            (proxy, method, arguments) -> switch (method.getName()) {
                case "name" -> "router";
                case "maxMessageSize" -> 4096L;
                case "peerWeight" -> 100;
                case "lastEndpoint" -> "tcp://127.0.0.1:7001";
                case "close", "connect", "disconnect", "bind",
                    "setChannelName", "setRoutingId",
                    "setConnectRoutingId", "setProbe",
                    "setMaxMessageSize", "setPeerWeight", "reply" -> null;
                case "send", "request" -> false;
                case "recv" -> null;
                default -> throw new UnsupportedOperationException(
                    method.getName());
            });
    }
}
