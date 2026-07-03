package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
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
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;

class ZLinkLocationStoreResolverTest {
    @Test
    void disabledRegistrationResolvesToNull() {
        assertNull(ZLinkLocationStoreResolver.resolve(
            new ZLinkLocationRegistration(),
            ZLinkHandlerFactory.reflection()));
    }

    @Test
    void inMemoryRegistrationUsesOneStoreForAllRolesAndOptionalStampStore() {
        ZLinkLocationRegistration registration = new ZLinkLocationRegistration();
        registration.enableInMemoryStores();

        ZLinkRegisteredLocationStores stores = ZLinkLocationStoreResolver.resolve(
            registration,
            ZLinkHandlerFactory.reflection());

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
            ZLinkHandlerFactory.reflection());

        assertSame(store, stores.peerStore());
        assertSame(store, stores.spotStore());
        assertSame(store, stores.actorStore());
        assertSame(store, stores.routeStore());
        assertSame(store, stores.ownerLeaseStore());
        assertSame(store, stores.unifiedStore());
    }

    @Test
    void samePerRoleTypeIsConstructedOnceAndShared() {
        CountingLocationStore.created.set(0);
        ZLinkLocationRegistration registration = new ZLinkLocationRegistration();
        registration.setPeerStoreType(CountingLocationStore.class);
        registration.setSpotStoreType(CountingLocationStore.class);
        registration.setActorStoreType(CountingLocationStore.class);
        registration.setRouteStoreType(CountingLocationStore.class);
        registration.setOwnerLeaseStoreType(CountingLocationStore.class);

        ZLinkRegisteredLocationStores stores = ZLinkLocationStoreResolver.resolve(
            registration,
            ZLinkHandlerFactory.reflection());

        assertEquals(1, CountingLocationStore.created.get());
        assertSame(stores.peerStore(), stores.spotStore());
        assertSame(stores.peerStore(), stores.actorStore());
        assertSame(stores.peerStore(), stores.routeStore());
        assertSame(stores.peerStore(), stores.ownerLeaseStore());
        assertSame(stores.peerStore(), stores.unifiedStore());
    }

    public static final class CountingLocationStore implements ZLinkLocationStore {
        static final AtomicInteger created = new AtomicInteger();

        public CountingLocationStore() {
            created.incrementAndGet();
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> updatePeerAsync(
            ZLinkPeerLocation peer,
            ZLinkLocationWriteIntent intent) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> removePeerAsync(
            ZLinkPeerLocationKey key,
            ZLinkLocationOwnerToken owner) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<Long> removePeersByOwnerAsync(String ownerId) {
            return CompletableFuture.completedFuture(0L);
        }

        @Override
        public CompletionStage<List<ZLinkPeerLocation>> listPeersAsync(ZLinkPeerLocationFilter filter) {
            return CompletableFuture.completedFuture(List.of());
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> updateSpotAsync(
            ZLinkSpotLocation spot,
            ZLinkLocationWriteIntent intent) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> removeSpotAsync(
            ZLinkSpotLocationKey key,
            ZLinkLocationOwnerToken owner) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<Long> removeSpotsByOwnerAsync(String ownerId) {
            return CompletableFuture.completedFuture(0L);
        }

        @Override
        public CompletionStage<ZLinkSpotLocation> resolveSpotAsync(ZLinkSpotLocationKey key) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotsAsync(
            ZLinkSpotLocationFilter filter,
            ZLinkPageRequest page) {
            return CompletableFuture.completedFuture(new ZLinkLocationPage<>(List.of(), null));
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> updateActorAsync(
            ZLinkActorLocation actor,
            ZLinkLocationWriteIntent intent) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> removeActorAsync(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<Long> removeActorsByOwnerAsync(String ownerId) {
            return CompletableFuture.completedFuture(0L);
        }

        @Override
        public CompletionStage<ZLinkActorLocation> resolveActorAsync(ZLinkActorLocationKey key) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page) {
            return CompletableFuture.completedFuture(new ZLinkLocationPage<>(List.of(), null));
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> updateRouteAsync(
            ZLinkRouteLocation route,
            ZLinkLocationWriteIntent intent) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> removeRouteAsync(
            ZLinkRouteLocationKey key,
            ZLinkLocationOwnerToken owner) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<Long> removeRoutesByOwnerAsync(String ownerId) {
            return CompletableFuture.completedFuture(0L);
        }

        @Override
        public CompletionStage<ZLinkRouteLocation> resolveRouteAsync(ZLinkRouteLocationKey key) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRoutesAsync(
            ZLinkRouteLocationFilter filter,
            ZLinkPageRequest page) {
            return CompletableFuture.completedFuture(new ZLinkLocationPage<>(List.of(), null));
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> renewOwnerLeaseAsync(
            String ownerId,
            RoutingId nodeRid,
            Duration leaseTtl) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> removeOwnerLeaseAsync(String ownerId) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeasesAsync() {
            return CompletableFuture.completedFuture(new ZLinkOwnerLeaseSnapshot(List.of(), java.time.Instant.EPOCH));
        }

        private CompletionStage<ZLinkLocationWriteResult> unsupportedWrite() {
            return CompletableFuture.completedFuture(ZLinkLocationWriteResult.storeUnavailable());
        }
    }
}
