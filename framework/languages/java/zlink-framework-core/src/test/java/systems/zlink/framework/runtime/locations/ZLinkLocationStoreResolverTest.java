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
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteResult;
import systems.zlink.framework.runtime.internal.locations.ZLinkOwnerLeaseRenewal;
import systems.zlink.framework.runtime.internal.locations.ZLinkOwnerLeaseSnapshot;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.testing.ZLinkLocationStoreTestAdapter;
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
        registration.setStoreInstance(new ZLinkInMemoryLocationStore());

        ZLinkRegisteredLocationStores stores = ZLinkLocationStoreResolver.resolve(
            registration,
            ZLinkHandlerActivator.reflection());

        assertNotNull(stores);
        assertSame(stores.peerStore(), stores.spotStore());
        assertSame(stores.peerStore(), stores.actorStore());
        assertSame(stores.peerStore(), stores.routeStore());
        assertSame(stores.peerStore(), stores.ownerLeaseStore());
        assertSame(stores.peerStore(), stores.unifiedStore());
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

    public static class CountingLocationStore extends ZLinkLocationStoreTestAdapter {
        static final AtomicInteger created = new AtomicInteger();
        private final ZLinkInMemoryAuthorityStore authority =
            new ZLinkInMemoryAuthorityStore(
                java.time.Clock.systemUTC(),
                ignored -> true);

        public CountingLocationStore() {
            created.incrementAndGet();
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityReadResult> read(
            String key,
            systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.read(key, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityWriteResult>
            compareExchange(
                String key,
                systems.zlink.framework.locations.ZLinkAuthorityExpectation expectation,
                systems.zlink.framework.locations.ZLinkAuthorityMutation mutation,
                systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.compareExchange(key, expectation, mutation, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityScanResult> list(
            String prefix,
            java.util.Optional<systems.zlink.framework.locations.ZLinkAuthorityScanCursor> cursor,
            int limit,
            systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.list(prefix, cursor, limit, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkObjectReserveResult> reserve(
            systems.zlink.framework.locations.ZLinkObjectReservationRequest request,
            systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.reserve(request, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkObjectCommitResult> commit(
            systems.zlink.framework.locations.ZLinkObjectReservation reservation,
            byte[] readyPayload,
            systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.commit(reservation, readyPayload, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkObjectCommitResult> commit(
            systems.zlink.framework.locations.ZLinkObjectReservation reservation,
            byte[] readyPayload,
            systems.zlink.framework.locations.ZLinkCreationOperationTerminal terminal,
            systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.commit(
                reservation,
                readyPayload,
                terminal,
                cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkObjectRejectResult> reject(
            systems.zlink.framework.locations.ZLinkObjectReservation reservation,
            systems.zlink.framework.locations.ZLinkCreationOperationTerminal terminal,
            systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.reject(reservation, terminal, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkObjectAbortResult> abort(
            systems.zlink.framework.locations.ZLinkObjectReservation reservation,
            systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.abort(reservation, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkObjectAbortResult> abort(
            systems.zlink.framework.locations.ZLinkObjectReservation reservation,
            systems.zlink.framework.locations.ZLinkCreationOperationTerminal terminal,
            systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.abort(reservation, terminal, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkCreationTerminalReadResult>
            readCreationTerminal(
                systems.zlink.framework.locations.ZLinkCreationOperationIdentity operation,
                systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.readCreationTerminal(operation, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult>
            reserveRelocationCapacity(
                systems.zlink.framework.locations.ZLinkRelocationCapacityReservationRequest request,
                systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.reserveRelocationCapacity(request, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult>
            abortRelocationCapacity(
                systems.zlink.framework.locations.ZLinkRelocationCapacityFence fence,
                systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.abortRelocationCapacity(fence, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkAggregatePrepareResult>
            prepareAggregate(
                systems.zlink.framework.locations.ZLinkAggregatePrepareRequest request,
                systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.prepareAggregate(request, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkAggregateCommitResult>
            commitAggregate(
                systems.zlink.framework.locations.ZLinkAggregateFence fence,
                systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.commitAggregate(fence, cancellation);
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkAggregateAbortResult>
            abortAggregate(
                systems.zlink.framework.locations.ZLinkAggregateFence fence,
                systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
            return authority.abortAggregate(fence, cancellation);
        }

        @Override
        public CompletionStage<ZLinkLocationWriteResult> updateMeshNode(
            systems.zlink.framework.locations.ZLinkMeshNodeDescriptor
                descriptor,
            ZLinkLocationWriteIntent intent) {
            return unsupportedWrite();
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations
            .ZLinkLocationWriteStatus> removeMeshNode(
                systems.zlink.framework.locations
                    .ZLinkMeshNodeDescriptorKey key,
                ZLinkLocationOwnerToken owner) {
            return CompletableFuture.completedFuture(
                systems.zlink.framework.locations
                    .ZLinkLocationWriteStatus.IGNORED_STALE);
        }

        @Override
        public CompletionStage<ZLinkLocationPage<
            systems.zlink.framework.locations
                .ZLinkMeshNodeDescriptor>> listMeshNodes(
                    String meshName,
                    ZLinkPageRequest page) {
            return CompletableFuture.completedFuture(
                new ZLinkLocationPage<>(List.of(), null));
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult>
            claimOwnerLease(
            String ownerId,
            Duration leaseTtl) {
            return CompletableFuture.completedFuture(
                new systems.zlink.framework.locations.ZLinkOwnerLeaseClaimed(
                    new ZLinkLocationOwnerToken(ownerId, 1L),
                    java.time.Instant.EPOCH.plus(leaseTtl),
                    java.time.Instant.EPOCH));
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult>
            readOwnerLease(String ownerId) {
            return CompletableFuture.completedFuture(
                new systems.zlink.framework.locations.ZLinkOwnerLeaseMissing());
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult>
            renewOwnerLease(
                ZLinkLocationOwnerToken token,
                Duration leaseTtl) {
            return CompletableFuture.completedFuture(
                new systems.zlink.framework.locations.ZLinkOwnerLeaseRenewed(
                    java.time.Instant.EPOCH.plus(leaseTtl),
                    java.time.Instant.EPOCH));
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult>
            releaseOwnerLease(ZLinkLocationOwnerToken token) {
            return CompletableFuture.completedFuture(
                systems.zlink.framework.locations
                    .ZLinkOwnerLeaseReleaseResult.RELEASED);
        }

        @Override
        public CompletionStage<Long> removeAllByOwner(
            ZLinkLocationOwnerToken owner) {
            return CompletableFuture.completedFuture(0L);
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
        public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult>
            claimOwnerLease(
            String ownerId,
            Duration leaseTtl) {
            Instant storeNow = storeTimes.removeFirst();
            return CompletableFuture.completedFuture(
                new systems.zlink.framework.locations.ZLinkOwnerLeaseClaimed(
                    new ZLinkLocationOwnerToken(ownerId, 1L),
                    storeNow.plus(leaseTtl),
                    storeNow));
        }

        @Override
        public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult>
            renewOwnerLease(
                ZLinkLocationOwnerToken token,
                Duration leaseTtl) {
            Instant storeNow = storeTimes.removeFirst();
            return CompletableFuture.completedFuture(
                new systems.zlink.framework.locations.ZLinkOwnerLeaseRenewed(
                    storeNow.plus(leaseTtl),
                    storeNow));
        }
    }
}
