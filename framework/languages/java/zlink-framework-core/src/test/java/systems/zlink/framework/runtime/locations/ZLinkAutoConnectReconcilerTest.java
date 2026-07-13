package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkPeerLocationResolver;

final class ZLinkAutoConnectReconcilerTest {
    @Test
    void markDrainingRenewsTypedMarkerWithoutDisconnectingPeers() {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRuntime runtime = runtime(store, "local-owner", "local-node");
        ZLinkPeerLocation localRow = peer(
            ZLinkLocationRole.ROUTER,
            RoutingId.from("local-node"),
            "inproc://local",
            "local-owner");
        RecordingExecutor executor = new RecordingExecutor();
        ZLinkAutoConnectReconciler reconciler = new ZLinkAutoConnectReconciler(
            new ZLinkAutoConnectPlanner.Local(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.ROUTER,
                RoutingId.from("local-node"),
                "inproc://local"),
            localRow,
            runtime,
            resolver(store),
            executor,
            options());

        reconciler.tick().toCompletableFuture().join();
        reconciler.markDraining().toCompletableFuture().join();

        ZLinkPeerLocation stored = findPeer(store, new ZLinkPeerLocationKey(
            ZLinkLocationAutoConnectType.CLIENT_SERVER,
            "orders",
            ZLinkLocationRole.ROUTER,
            RoutingId.from("local-node"),
            "inproc://local"));
        assertTrue(stored.draining());
        assertEquals(List.of(), executor.disconnected);

        reconciler.shutdown().toCompletableFuture().join();
        runtime.close();
    }

    @Test
    void advertiseOnlyCapabilityPublishesAndRemovesPeerRow() {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRuntime runtime = runtime(store, "local-owner", "local-node");
        ZLinkStoreLocationResolvers resolver = resolver(store);
        ZLinkPeerLocation localRow = peer(
            ZLinkLocationRole.ROUTER,
            RoutingId.from("local-node"),
            "inproc://local",
            "local-owner");
        ZLinkAutoConnectReconciler reconciler = new ZLinkAutoConnectReconciler(
            new ZLinkAutoConnectPlanner.Local(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.ROUTER,
                RoutingId.from("local-node"),
                "inproc://local"),
            localRow,
            runtime,
            resolver,
            ZLinkAutoConnectExecutor.NONE,
            options());

        reconciler.tick().toCompletableFuture().join();

        ZLinkPeerLocation stored = findPeer(store, new ZLinkPeerLocationKey(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.ROUTER,
                RoutingId.from("local-node"),
                "inproc://local"));
        assertEquals("local-owner", stored.ownerId());

        reconciler.shutdown().toCompletableFuture().join();

        assertNull(findPeer(store, new ZLinkPeerLocationKey(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.ROUTER,
                RoutingId.from("local-node"),
                "inproc://local")));
        runtime.close();
    }

    @Test
    void rejectedStartupClaimIsRetriedOnNextTick() {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRuntime runtime = runtime(store, "local-owner", "local-node");
        ZLinkPeerLocation localRow = peer(
            ZLinkLocationRole.ROUTER,
            RoutingId.from("local-node"),
            "inproc://local",
            "local-owner");
        store.renewOwnerLease("remote-owner", RoutingId.from("remote-node"), Duration.ofSeconds(30))
            .toCompletableFuture()
            .join();
        var remoteWrite = store.updatePeer(
                peer(
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("local-node"),
                    "inproc://local",
                    "remote-owner"),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .join();
        ZLinkAutoConnectReconciler reconciler = new ZLinkAutoConnectReconciler(
            new ZLinkAutoConnectPlanner.Local(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.ROUTER,
                RoutingId.from("local-node"),
                "inproc://local"),
            localRow,
            runtime,
            resolver(store),
            ZLinkAutoConnectExecutor.NONE,
            options());

        reconciler.tick().toCompletableFuture().join();
        assertEquals("remote-owner", findPeer(store, new ZLinkPeerLocationKey(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.ROUTER,
                RoutingId.from("local-node"),
                "inproc://local"))
            .ownerId());

        store.removePeer(
                new ZLinkPeerLocationKey(
                    ZLinkLocationAutoConnectType.CLIENT_SERVER,
                    "orders",
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("local-node"),
                    "inproc://local"),
                new ZLinkLocationOwnerToken("remote-owner", remoteWrite.generation()))
            .toCompletableFuture()
            .join();
        reconciler.tick().toCompletableFuture().join();

        assertEquals("local-owner", findPeer(store, new ZLinkPeerLocationKey(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.ROUTER,
                RoutingId.from("local-node"),
                "inproc://local"))
            .ownerId());
        reconciler.shutdown().toCompletableFuture().join();
        runtime.close();
    }

    @Test
    void dialingCapabilityConnectsLivePeerAndDisconnectsRemovedPeer() {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRuntime runtime = runtime(store, "local-owner", "local-node");
        store.renewOwnerLease("remote-owner", RoutingId.from("remote-node"), Duration.ofSeconds(30))
            .toCompletableFuture()
            .join();
        var remoteWrite = store.updatePeer(
                peer(
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("remote-node"),
                    "inproc://remote",
                    "remote-owner"),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .join();
        assertEquals(ZLinkLocationWriteStatus.STORED, remoteWrite.status());
        RecordingExecutor executor = new RecordingExecutor();
        ZLinkAutoConnectReconciler reconciler = new ZLinkAutoConnectReconciler(
            new ZLinkAutoConnectPlanner.Local(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.DEALER,
                RoutingId.from("local-node"),
                ""),
            null,
            runtime,
            resolver(store),
            executor,
            options());

        reconciler.tick().toCompletableFuture().join();

        assertEquals(List.of("inproc://remote"), executor.connected);

        store.removePeer(
                new ZLinkPeerLocationKey(
                    ZLinkLocationAutoConnectType.CLIENT_SERVER,
                    "orders",
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("remote-node"),
                    "inproc://remote"),
                new ZLinkLocationOwnerToken("remote-owner", remoteWrite.generation()))
            .toCompletableFuture()
            .join();
        reconciler.tick().toCompletableFuture().join();

        assertEquals(List.of("inproc://remote"), executor.disconnected);
        runtime.close();
    }

    @Test
    void dialingCapabilityConvergesWhenProviderStartsAfterConsumer() {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRuntime consumerRuntime = runtime(store, "consumer-owner", "consumer-node");
        RecordingExecutor executor = new RecordingExecutor();
        ZLinkAutoConnectReconciler consumer = new ZLinkAutoConnectReconciler(
            new ZLinkAutoConnectPlanner.Local(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.DEALER,
                RoutingId.from("consumer-node"),
                ""),
            null,
            consumerRuntime,
            resolver(store),
            executor,
            options());

        // A consumer started before any provider remains valid and keeps reconciling.
        consumer.tick().toCompletableFuture().join();
        assertEquals(List.of(), executor.connected);

        store.renewOwnerLease("provider-owner", RoutingId.from("provider-node"), Duration.ofSeconds(30))
            .toCompletableFuture()
            .join();
        store.updatePeer(
                peer(
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("provider-node"),
                    "inproc://provider",
                    "provider-owner"),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .join();

        // The next internal reconciliation discovers the late provider without application action.
        consumer.tick().toCompletableFuture().join();
        assertEquals(List.of("inproc://provider"), executor.connected);

        consumer.shutdown().toCompletableFuture().join();
        consumerRuntime.close();
    }

    @Test
    void samePeerIdentityWithNewOwnerReplacesActiveConnection() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRuntime runtime = runtime(store, "local-owner", "local-node");
        store.renewOwnerLease("remote-owner-1", RoutingId.from("remote-node"), Duration.ofSeconds(30))
            .toCompletableFuture()
            .join();
        ZLinkPeerLocation first = peer(
            ZLinkLocationRole.ROUTER,
            RoutingId.from("remote-node"),
            "inproc://remote",
            "remote-owner-1");
        var firstWrite = store.updatePeer(first, ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .join();
        RecordingExecutor executor = new RecordingExecutor();
        ZLinkAutoConnectReconciler reconciler = new ZLinkAutoConnectReconciler(
            new ZLinkAutoConnectPlanner.Local(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.DEALER,
                RoutingId.from("local-node"),
                ""),
            null,
            runtime,
            resolver(store),
            executor,
            options());

        reconciler.tick().toCompletableFuture().join();
        store.removePeer(
                new ZLinkPeerLocationKey(
                    ZLinkLocationAutoConnectType.CLIENT_SERVER,
                    "orders",
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("remote-node"),
                    "inproc://remote"),
                new ZLinkLocationOwnerToken("remote-owner-1", firstWrite.generation()))
            .toCompletableFuture()
            .join();
        store.renewOwnerLease("remote-owner-2", RoutingId.from("remote-node"), Duration.ofSeconds(30))
            .toCompletableFuture()
            .join();
        store.updatePeer(
                peer(
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("remote-node"),
                    "inproc://remote",
                    "remote-owner-2"),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .join();
        Thread.sleep(options().pollingInterval().toMillis() + 10);

        reconciler.tick().toCompletableFuture().join();

        assertEquals(List.of("inproc://remote", "inproc://remote"), executor.connected);
        assertEquals(List.of("inproc://remote"), executor.disconnected);
        runtime.close();
    }

    @Test
    void manualNonInitiatorTracksOwnerReplacementWithoutOwningInitialConnect() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRuntime runtime = runtime(store, "local-owner", "play-b");
        store.renewOwnerLease("remote-owner-1", RoutingId.from("play-a"), Duration.ofSeconds(30))
            .toCompletableFuture().join();
        ZLinkPeerLocation first = routePeer("play-a", "inproc://play-a", "remote-owner-1");
        var firstWrite = store.updatePeer(first, ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture().join();
        RecordingExecutor executor = new RecordingExecutor(true);
        ZLinkAutoConnectReconciler reconciler = new ZLinkAutoConnectReconciler(
            new ZLinkAutoConnectPlanner.Local(
                ZLinkLocationAutoConnectType.ROUTE_MESH,
                "route",
                ZLinkLocationRole.ROUTER,
                RoutingId.from("play-b"),
                "inproc://play-b"),
            null,
            runtime,
            resolver(store),
            executor,
            options());

        reconciler.tick().toCompletableFuture().join();
        assertEquals(List.of(), executor.connected);
        store.removePeer(
                new ZLinkPeerLocationKey(
                    ZLinkLocationAutoConnectType.ROUTE_MESH,
                    "route",
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("play-a"),
                    "inproc://play-a"),
                new ZLinkLocationOwnerToken("remote-owner-1", firstWrite.generation()))
            .toCompletableFuture().join();
        Thread.sleep(options().pollingInterval().toMillis() + 10);
        reconciler.tick().toCompletableFuture().join();
        assertEquals(List.of(), executor.disconnected);
        assertEquals(List.of(), executor.connected);
        store.renewOwnerLease("remote-owner-2", RoutingId.from("play-a"), Duration.ofSeconds(30))
            .toCompletableFuture().join();
        var secondWrite = store.updatePeer(
                routePeer("play-a", "inproc://play-a", "remote-owner-2"),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture().join();
        Thread.sleep(options().pollingInterval().toMillis() + 10);

        reconciler.tick().toCompletableFuture().join();

        assertEquals(List.of("inproc://play-a"), executor.disconnected);
        assertEquals(List.of("inproc://play-a"), executor.connected);
        store.removePeer(
                new ZLinkPeerLocationKey(
                    ZLinkLocationAutoConnectType.ROUTE_MESH,
                    "route",
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("play-a"),
                    "inproc://play-a"),
                new ZLinkLocationOwnerToken("remote-owner-2", secondWrite.generation()))
            .toCompletableFuture().join();
        Thread.sleep(options().pollingInterval().toMillis() + 10);
        reconciler.tick().toCompletableFuture().join();
        assertEquals(List.of("inproc://play-a"), executor.disconnected);
        assertEquals(List.of("inproc://play-a"), executor.connected);
        runtime.close();
    }

    @Test
    void storeFailureKeepsExistingConnectionAndRecoveryDefersDisconnectDiff() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRuntime runtime = runtime(store, "local-owner", "local-node");
        store.renewOwnerLease("remote-owner", RoutingId.from("remote-node"), Duration.ofSeconds(30))
            .toCompletableFuture()
            .join();
        var remoteWrite = store.updatePeer(
                peer(
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("remote-node"),
                    "inproc://remote",
                    "remote-owner"),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .join();
        RecordingExecutor executor = new RecordingExecutor();
        FlakyPeerResolver resolver = new FlakyPeerResolver(store);
        ZLinkLocationOptions options = options();
        ZLinkAutoConnectReconciler reconciler = new ZLinkAutoConnectReconciler(
            new ZLinkAutoConnectPlanner.Local(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.DEALER,
                RoutingId.from("local-node"),
                ""),
            null,
            runtime,
            resolver,
            executor,
            options);

        reconciler.tick().toCompletableFuture().join();
        assertEquals(List.of("inproc://remote"), executor.connected);

        resolver.fail = true;
        store.removePeer(
                new ZLinkPeerLocationKey(
                    ZLinkLocationAutoConnectType.CLIENT_SERVER,
                    "orders",
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("remote-node"),
                    "inproc://remote"),
                new ZLinkLocationOwnerToken("remote-owner", remoteWrite.generation()))
            .toCompletableFuture()
            .join();

        reconciler.tick().toCompletableFuture().join();
        assertEquals(List.of(), executor.disconnected);

        resolver.fail = false;
        reconciler.tick().toCompletableFuture().join();
        assertEquals(List.of(), executor.disconnected);

        Thread.sleep(options.heartbeatInterval().toMillis() + 10);
        reconciler.tick().toCompletableFuture().join();
        assertEquals(List.of("inproc://remote"), executor.disconnected);
        runtime.close();
    }

    private static ZLinkLocationRuntime runtime(
        ZLinkInMemoryLocationStore store,
        String ownerId,
        String nodeRid) {
        ZLinkLocationRuntime runtime = new ZLinkLocationRuntime(
            store,
            ownerId,
            Duration.ofSeconds(30),
            Duration.ofMillis(50));
        runtime.start(RoutingId.from(nodeRid)).toCompletableFuture().join();
        return runtime;
    }

    private static ZLinkStoreLocationResolvers resolver(ZLinkInMemoryLocationStore store) {
        return new ZLinkStoreLocationResolvers(
            ZLinkRegisteredLocationStores.fromUnified(store),
            options());
    }

    private static ZLinkPeerLocation findPeer(
        ZLinkInMemoryLocationStore store,
        ZLinkPeerLocationKey key) {
        return store.listPeerLocations(new systems.zlink.framework.locations.ZLinkPeerLocationFilter(
                key.autoConnectType(),
                key.meshName(),
                key.role(),
                key.nodeRid(),
                key.endpoint()))
            .toCompletableFuture()
            .join()
            .stream()
            .findFirst()
            .orElse(null);
    }

    private static ZLinkLocationOptions options() {
        ZLinkLocationOptions options = new ZLinkLocationOptions();
        options.setHeartbeatInterval(Duration.ofMillis(50));
        options.setPollingInterval(Duration.ofMillis(10));
        options.setOwnerLeaseTtl(Duration.ofSeconds(30));
        return options;
    }

    private static ZLinkPeerLocation peer(
        ZLinkLocationRole role,
        RoutingId nodeRid,
        String endpoint,
        String ownerId) {
        return new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.CLIENT_SERVER,
            "orders",
            nodeRid,
            role,
            endpoint,
            100,
            false,
            0,
            null,
            null,
            ownerId,
            0,
            Instant.EPOCH);
    }

    private static ZLinkPeerLocation routePeer(
        String nodeRid,
        String endpoint,
        String ownerId) {
        return new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "route",
            RoutingId.from(nodeRid),
            ZLinkLocationRole.ROUTER,
            endpoint,
            100,
            false,
            0,
            null,
            null,
            ownerId,
            0,
            Instant.EPOCH);
    }

    private static final class RecordingExecutor implements ZLinkAutoConnectExecutor {
        final List<String> connected = new ArrayList<>();
        final List<String> disconnected = new ArrayList<>();
        final boolean manual;

        RecordingExecutor() {
            this(false);
        }

        RecordingExecutor(boolean manual) {
            this.manual = manual;
        }

        @Override
        public boolean isManual(ZLinkAutoConnectPlanner.Target target) {
            return manual;
        }

        @Override
        public void connect(ZLinkAutoConnectPlanner.Target target) {
            connected.add(target.endpoint());
        }

        @Override
        public void disconnect(ZLinkAutoConnectPlanner.Target target) {
            disconnected.add(target.endpoint());
        }
    }

    private static final class FlakyPeerResolver implements ZLinkPeerLocationResolver {
        private final ZLinkStoreLocationResolvers delegate;
        boolean fail;

        FlakyPeerResolver(ZLinkInMemoryLocationStore store) {
            this.delegate = resolver(store);
        }

        @Override
        public CompletionStage<List<ZLinkPeerLocation>> listLivePeers(
            ZLinkPeerLocationFilter filter) {
            if (fail) {
                return CompletableFuture.failedFuture(new IllegalStateException("store unavailable"));
            }
            return delegate.listLivePeers(filter);
        }
    }
}
