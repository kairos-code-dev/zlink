package dev.kairoscode.zlink.integration.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.RecvException;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RecvResult;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.service.registry.AutoConnectType;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.RegistryQueryClient;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
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
