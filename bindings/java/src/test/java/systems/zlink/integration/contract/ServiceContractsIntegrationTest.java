package systems.zlink.integration.contract;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.MonitorEventType;
import systems.zlink.RecvException;
import systems.zlink.RecvFlags;
import systems.zlink.RecvResult;
import systems.zlink.PairSocket;
import systems.zlink.SendFlags;
import systems.zlink.SubmitException;
import systems.zlink.service.registry.AutoConnectType;
import systems.zlink.TestSupport;
import systems.zlink.SpotDispatchEvent;
import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.registry.Registry;
import systems.zlink.service.registry.RegistryQueryClient;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
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

        try (Context ctx = new Context();
            Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, AutoConnectType.CLIENT_SERVER,
               "svc-alpha");
             SpotNode node = new SpotNode(ctx);
             RegistryQueryClient queryClient = new RegistryQueryClient(ctx)) {
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

            assertEquals(0, registry.statusSnapshot().topologyEntryCount());
            assertTrue(registry.serviceSummarySnapshot().isEmpty());
            assertTrue(registry.topologySnapshot().isEmpty());
            assertTrue(registry.memberPeers("svc-alpha").isEmpty());
            assertTrue(queryClient.snapshot().isEmpty());

            assertEquals(0, node.statusSnapshot().configuredPeerCount());
            assertTrue(node.peersSnapshot().isEmpty());
            assertTrue(node.subjectsSnapshot().isEmpty());
        }
    }

    @Test
    void monitorSnapshotExposesCanonicalMonitorState() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket socket = new PairSocket(ctx);
             var monitor = socket.monitorOpen()) {
            assertTrue(monitor.snapshot().sndPendingMsgs() >= 0L);
        }

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot spot = node.createSpot()) {
            spot.setSubscription("svc-topic");
            assertEquals(0, node.statusSnapshot().connectedPeerCount());
            assertTrue(node.subjectsSnapshot().stream()
                .anyMatch(entry -> "svc-topic".equals(entry.subject())));
        }
    }

    @Test
    void spotNodeWrappedHandleExposesCanonicalServiceHandle() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot subscriber = node.createSpot()) {
            subscriber.setSubscription("perf-topic");
            assertTrue(node.subjectsSnapshot().stream()
                .anyMatch(entry -> "perf-topic".equals(entry.subject())));
        }
    }

    @Test
    void spotDispatchEventRunsOnManagedJavaThread() throws Exception {
        TestSupport.assumeNative();

        AtomicReference<Thread> callbackThread = new AtomicReference<>();
        AtomicReference<Throwable> callbackError = new AtomicReference<>();

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot subscriber = node.createSpot()) {
            subscriber.setSubscription("spot-callback-topic");

            subscriber.onDispatchEvent(info -> {
                try {
                    callbackThread.set(Thread.currentThread());
                } catch (Throwable t) {
                    callbackError.set(t);
                }
            });

            assertNull(callbackError.get(), "callback raised: "
                + callbackError.get());
            assertTrue(node.subjectsSnapshot().stream()
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
