package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

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

        reconciler.tickAsync().toCompletableFuture().join();

        ZLinkPeerLocation stored = findPeer(store, new ZLinkPeerLocationKey(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.ROUTER,
                RoutingId.from("local-node"),
                "inproc://local"));
        assertEquals("local-owner", stored.ownerId());

        reconciler.shutdownAsync().toCompletableFuture().join();

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
        store.renewOwnerLeaseAsync("remote-owner", RoutingId.from("remote-node"), Duration.ofSeconds(30))
            .toCompletableFuture()
            .join();
        var remoteWrite = store.updatePeerAsync(
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

        reconciler.tickAsync().toCompletableFuture().join();
        assertEquals("remote-owner", findPeer(store, new ZLinkPeerLocationKey(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.ROUTER,
                RoutingId.from("local-node"),
                "inproc://local"))
            .ownerId());

        store.removePeerAsync(
                new ZLinkPeerLocationKey(
                    ZLinkLocationAutoConnectType.CLIENT_SERVER,
                    "orders",
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("local-node"),
                    "inproc://local"),
                new ZLinkLocationOwnerToken("remote-owner", remoteWrite.generation()))
            .toCompletableFuture()
            .join();
        reconciler.tickAsync().toCompletableFuture().join();

        assertEquals("local-owner", findPeer(store, new ZLinkPeerLocationKey(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                "orders",
                ZLinkLocationRole.ROUTER,
                RoutingId.from("local-node"),
                "inproc://local"))
            .ownerId());
        reconciler.shutdownAsync().toCompletableFuture().join();
        runtime.close();
    }

    @Test
    void dialingCapabilityConnectsLivePeerAndDisconnectsRemovedPeer() {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRuntime runtime = runtime(store, "local-owner", "local-node");
        store.renewOwnerLeaseAsync("remote-owner", RoutingId.from("remote-node"), Duration.ofSeconds(30))
            .toCompletableFuture()
            .join();
        var remoteWrite = store.updatePeerAsync(
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

        reconciler.tickAsync().toCompletableFuture().join();

        assertEquals(List.of("inproc://remote"), executor.connected);

        store.removePeerAsync(
                new ZLinkPeerLocationKey(
                    ZLinkLocationAutoConnectType.CLIENT_SERVER,
                    "orders",
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("remote-node"),
                    "inproc://remote"),
                new ZLinkLocationOwnerToken("remote-owner", remoteWrite.generation()))
            .toCompletableFuture()
            .join();
        reconciler.tickAsync().toCompletableFuture().join();

        assertEquals(List.of("inproc://remote"), executor.disconnected);
        runtime.close();
    }

    @Test
    void storeFailureKeepsExistingConnectionAndRecoveryDefersDisconnectDiff() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRuntime runtime = runtime(store, "local-owner", "local-node");
        store.renewOwnerLeaseAsync("remote-owner", RoutingId.from("remote-node"), Duration.ofSeconds(30))
            .toCompletableFuture()
            .join();
        var remoteWrite = store.updatePeerAsync(
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

        reconciler.tickAsync().toCompletableFuture().join();
        assertEquals(List.of("inproc://remote"), executor.connected);

        resolver.fail = true;
        store.removePeerAsync(
                new ZLinkPeerLocationKey(
                    ZLinkLocationAutoConnectType.CLIENT_SERVER,
                    "orders",
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("remote-node"),
                    "inproc://remote"),
                new ZLinkLocationOwnerToken("remote-owner", remoteWrite.generation()))
            .toCompletableFuture()
            .join();

        reconciler.tickAsync().toCompletableFuture().join();
        assertEquals(List.of(), executor.disconnected);

        resolver.fail = false;
        reconciler.tickAsync().toCompletableFuture().join();
        assertEquals(List.of(), executor.disconnected);

        Thread.sleep(options.heartbeatInterval().toMillis() + 10);
        reconciler.tickAsync().toCompletableFuture().join();
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
        runtime.startAsync(RoutingId.from(nodeRid)).toCompletableFuture().join();
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
        return store.listPeerLocationsAsync(new systems.zlink.framework.locations.ZLinkPeerLocationFilter(
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
        public CompletionStage<List<ZLinkPeerLocation>> listLivePeersAsync(
            ZLinkPeerLocationFilter filter) {
            if (fail) {
                return CompletableFuture.failedFuture(new IllegalStateException("store unavailable"));
            }
            return delegate.listLivePeersAsync(filter);
        }
    }
}
