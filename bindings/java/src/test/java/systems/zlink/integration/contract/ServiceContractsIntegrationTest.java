package systems.zlink.integration.contract;

import systems.zlink.TestSupport;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.sockets.PairSocket;
import systems.zlink.contracts.service.registry.Registry;
import systems.zlink.contracts.service.registry.RegistryQueryClient;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import org.junit.jupiter.api.Test;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.TimeUnit;
import java.time.Duration;
import java.time.Instant;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ServiceContractsIntegrationTest {
    @Test
    void discoveryRegistryAndSpotNodeExposeCanonicalSnapshots() {
        TestSupport.assumeNative();

        String registryPub = TestSupport.tcpEndpoint();
        String registryRouter = TestSupport.tcpEndpoint();

        try (Context ctx = Zlink.createContext();
            Registry registry = ctx.createRegistry();
             Discovery discovery = ctx.createDiscovery(AutoConnectType.CLIENT_SERVER, "svc-alpha");
             SpotNode node = ctx.createSpotNode();
             RegistryQueryClient queryClient = ctx.createRegistryQueryClient()) {
            registry.bind(registryPub, registryRouter);
            queryClient.connect(registryRouter);
            discovery.connectRegistry(registryRouter);
            discovery.setValue(7L);
            String cert = Path.of("tests/certs/server.crt").toAbsolutePath().toString();
            String key = Path.of("tests/certs/server.key").toAbsolutePath().toString();
            String ca = Path.of("tests/certs/ca.crt").toAbsolutePath().toString();
            registry.setTlsServer(cert, key, true);
            registry.setTlsClient(ca, "localhost", true);
            discovery.setTlsClient(ca, "localhost", true);

            assertEquals(7L, discovery.getValue());
            assertTrue(discovery.memberPeers().isEmpty());

            assertEquals(0, registry.status().topologyEntryCount());
            assertTrue(registry.serviceSummary().isEmpty());
            assertTrue(registry.topology().isEmpty());
            assertTrue(registry.memberPeers("svc-alpha").isEmpty());
            assertTrue(queryClient.topology().isEmpty());

            assertEquals(0, node.status().configuredPeerCount());
            assertTrue(node.peers().isEmpty());
            assertTrue(node.subjects().isEmpty());
        }
    }

    @Test
    void monitorStatusExposesCanonicalMonitorState() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             PairSocket socket = ctx.createPairSocket();
             var monitor = socket.monitorOpen()) {
            assertTrue(monitor.status().sndPendingMsgs() >= 0L);
        }

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot spot = node.createSpot()) {
            spot.setSubscription("svc-topic");
            assertEquals(0, node.status().connectedPeerCount());
            assertTrue(node.subjects().stream()
                .anyMatch(entry -> "svc-topic".equals(entry.subject())));
        }
    }

    @Test
    void spotNodeWrappedHandleExposesCanonicalServiceHandle() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot subscriber = node.createSpot()) {
            subscriber.setSubscription("perf-topic");
            assertTrue(node.subjects().stream()
                .anyMatch(entry -> "perf-topic".equals(entry.subject())));
        }
    }

    @Test
    void spotDispatchEventRunsOnManagedJavaThread() throws Exception {
        TestSupport.assumeNative();

        AtomicReference<Thread> callbackThread = new AtomicReference<>();
        AtomicReference<Throwable> callbackError = new AtomicReference<>();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot subscriber = node.createSpot()) {
            subscriber.setSubscription("spot-callback-topic");

            subscriber.setDispatchHandler(info -> {
                try {
                    callbackThread.set(Thread.currentThread());
                } catch (Throwable t) {
                    callbackError.set(t);
                }
            });

            assertNull(callbackError.get(), "callback raised: "
                + callbackError.get());
            assertTrue(node.subjects().stream()
                .anyMatch(entry -> "spot-callback-topic".equals(entry.subject())));
            assertNull(callbackThread.get());
        }
    }

    private static void awaitCondition(Check check, String label) {
        Instant deadline = Instant.now().plusMillis(TestSupport.DEFAULT_TIMEOUT_MS);
        while (Instant.now().isBefore(deadline)) {
            if (check.ready()) {
                return;
            }
            Thread.onSpinWait();
        }
        throw new IllegalStateException(label + " timed out");
    }

    @FunctionalInterface
    private interface Check {
        boolean ready();
    }
}
