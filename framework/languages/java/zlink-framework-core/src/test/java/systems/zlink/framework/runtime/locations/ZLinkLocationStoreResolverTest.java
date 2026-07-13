package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.time.Instant;
import java.util.ArrayDeque;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationFilter;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationChangeStampScope;
import systems.zlink.framework.locations.ZLinkLocationChangeStampStore;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteResult;
import systems.zlink.framework.locations.ZLinkOwnerLeaseRenewal;
import systems.zlink.framework.locations.ZLinkOwnerLeaseSnapshot;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationFilter;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationFilter;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;

class ZLinkLocationStoreResolverTest {
    @Test
    void disabledRegistrationResolvesToNull() {
        assertNull(ZLinkLocationStoreResolver.resolve(
            new ZLinkLocationRegistration(),
            ZLinkHandlerActivator.reflection()));
    }

    @Test
    void inMemoryRegistrationUsesOneStoreForAllRolesAndOptionalStampStore() {
        ZLinkLocationRegistration registration = new ZLinkLocationRegistration();
        registration.enableInMemoryStores();

        ZLinkRegisteredLocationStores stores = ZLinkLocationStoreResolver.resolve(
            registration,
            ZLinkHandlerActivator.reflection());

        assertNotNull(stores);
        assertSame(stores.peerStore(), stores.spotStore());
        assertSame(stores.peerStore(), stores.actorStore());
        assertSame(stores.peerStore(), stores.routeStore());
        assertSame(stores.peerStore(), stores.ownerLeaseStore());
        assertSame(stores.peerStore(), stores.unifiedStore());
        assertTrue(stores.changeStampStore() instanceof ZLinkLocationChangeStampStore);
    }

    @Test
    void explicitUnifiedStoreInstanceIsReusedForAllRoles() {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRegistration registration = new ZLinkLocationRegistration();
        registration.setStoreInstance(store);

        ZLinkRegisteredLocationStores stores = ZLinkLocationStoreResolver.resolve(
            registration,
            ZLinkHandlerActivator.reflection());

        assertSame(store, stores.peerStore());
        assertSame(store, stores.spotStore());
        assertSame(store, stores.actorStore());
        assertSame(store, stores.routeStore());
        assertSame(store, stores.ownerLeaseStore());
        assertSame(store, stores.unifiedStore());
    }

    @Test
    void explicitUnifiedStoreTypeIsSharedThroughEveryRole() {
        CountingLocationStore.created.set(0);
        CountingLocationStore store = new CountingLocationStore();
        ZLinkLocationRegistration registration = new ZLinkLocationRegistration();
        registration.setStoreInstance(store);

        ZLinkRegisteredLocationStores stores = ZLinkLocationStoreResolver.resolve(
            registration,
            ZLinkHandlerActivator.reflection());

        assertEquals(1, CountingLocationStore.created.get());
        assertSame(store, stores.unifiedStore());
        assertSame(stores.peerStore(), stores.spotStore());
        assertSame(stores.peerStore(), stores.actorStore());
        assertSame(stores.peerStore(), stores.routeStore());
        assertSame(stores.peerStore(), stores.ownerLeaseStore());
        assertSame(stores.peerStore(), stores.unifiedStore());
    }

    @Test
    void ownerLeaseRefreshTimeDoesNotMoveBackwardWhenStoreClockRegresses() {
        DescendingLeaseClockStore store = new DescendingLeaseClockStore(
            Instant.parse("2026-07-13T14:04:23.422Z"),
            Instant.parse("2026-07-13T14:04:23.231Z"));
        try (ZLinkLocationRuntime runtime = new ZLinkLocationRuntime(
                store,
                "owner-a",
                Duration.ofMinutes(1),
                Duration.ofDays(1))) {
            runtime.start(RoutingId.from("node-a")).toCompletableFuture().join();
            Instant before = runtime.ownerLeaseRenewedAt();

            runtime.renewOwnerLeaseOnce().toCompletableFuture().join();

            assertTrue(runtime.ownerLeaseRenewedAt().isAfter(before));
        }
    }

    public static class CountingLocationStore implements ZLinkLocationStore {
        static final AtomicInteger created = new AtomicInteger();

        public CountingLocationStore() {
            created.incrementAndGet();
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> updatePeer(
            ZLinkPeerLocation peer,
            ZLinkLocationWriteIntent intent) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> removePeer(
            ZLinkPeerLocationKey key,
            ZLinkLocationOwnerToken owner) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<List<ZLinkPeerLocation>> listPeerLocations(ZLinkPeerLocationFilter filter) {
            return CompletableFuture.completedFuture(List.of());
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> updateSpot(
            ZLinkSpotLocation spot,
            ZLinkLocationWriteIntent intent) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> removeSpot(
            ZLinkSpotLocationKey key,
            ZLinkLocationOwnerToken owner) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkSpotLocation> resolveSpot(ZLinkSpotLocationKey key) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocations(
            ZLinkSpotLocationFilter filter,
            ZLinkPageRequest page) {
            return CompletableFuture.completedFuture(new ZLinkLocationPage<>(List.of(), null));
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> updateActor(
            ZLinkActorLocation actor,
            ZLinkLocationWriteIntent intent) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> removeActor(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkActorLocation> resolveActor(ZLinkActorLocationKey key) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocations(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page) {
            return CompletableFuture.completedFuture(new ZLinkLocationPage<>(List.of(), null));
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> updateRoute(
            ZLinkRouteLocation route,
            ZLinkLocationWriteIntent intent) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> removeRoute(
            ZLinkRouteLocationKey key,
            ZLinkLocationOwnerToken owner) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkRouteLocation> resolveRoute(ZLinkRouteLocationKey key) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocations(
            ZLinkRouteLocationFilter filter,
            ZLinkPageRequest page) {
            return CompletableFuture.completedFuture(new ZLinkLocationPage<>(List.of(), null));
        }

        @Override
        public CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLease(
            String ownerId,
            RoutingId nodeRid,
            Duration leaseTtl) {
            return CompletableFuture.failedFuture(new UnsupportedOperationException("write not supported"));
        }

        @Override
        public CompletionStage<Boolean> removeOwnerLease(String ownerId) {
            return CompletableFuture.failedFuture(new UnsupportedOperationException("write not supported"));
        }

        @Override
        public CompletionStage<Long> removeAllByOwner(String ownerId) {
            return CompletableFuture.completedFuture(0L);
        }

        @Override
        public CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeases() {
            return CompletableFuture.completedFuture(new ZLinkOwnerLeaseSnapshot(List.of(), java.time.Instant.EPOCH));
        }

        private CompletionStage<ZLinkLocationWriteResult> unsupportedWrite() {
            return CompletableFuture.failedFuture(new UnsupportedOperationException("write not supported"));
        }
    }

    private static final class DescendingLeaseClockStore extends CountingLocationStore {
        private final ArrayDeque<Instant> storeTimes;

        private DescendingLeaseClockStore(Instant... storeTimes) {
            this.storeTimes = new ArrayDeque<>(List.of(storeTimes));
        }

        @Override
        public CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLease(
            String ownerId,
            RoutingId nodeRid,
            Duration leaseTtl) {
            Instant storeNow = storeTimes.removeFirst();
            return CompletableFuture.completedFuture(
                new ZLinkOwnerLeaseRenewal(storeNow.plus(leaseTtl), storeNow));
        }
    }
}
