package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;

import java.time.Instant;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.locations.ZLinkAggregateAbortResult;
import systems.zlink.framework.locations.ZLinkAggregateCommitResult;
import systems.zlink.framework.locations.ZLinkAggregateFence;
import systems.zlink.framework.locations.ZLinkAggregatePrepareRequest;
import systems.zlink.framework.locations.ZLinkAggregatePrepareResult;
import systems.zlink.framework.locations.ZLinkAuthorityExpectation;
import systems.zlink.framework.locations.ZLinkAuthorityMutation;
import systems.zlink.framework.locations.ZLinkAuthorityPut;
import systems.zlink.framework.locations.ZLinkAuthorityReadResult;
import systems.zlink.framework.locations.ZLinkAuthorityScanCursor;
import systems.zlink.framework.locations.ZLinkAuthorityScanResult;
import systems.zlink.framework.locations.ZLinkAuthoritySnapshot;
import systems.zlink.framework.locations.ZLinkAuthorityStore;
import systems.zlink.framework.locations.ZLinkAuthorityStored;
import systems.zlink.framework.locations.ZLinkAuthorityWriteResult;
import systems.zlink.framework.locations.ZLinkObjectAbortResult;
import systems.zlink.framework.locations.ZLinkObjectCommitResult;
import systems.zlink.framework.locations.ZLinkObjectReservation;
import systems.zlink.framework.locations.ZLinkObjectReservationRequest;
import systems.zlink.framework.locations.ZLinkObjectReserveResult;
import systems.zlink.framework.locations.ZLinkStoreCancellation;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;

final class ZLinkAuthorityContractTest {
    @Test
    void exactAuthorityStoreMethodsArePublic() throws Exception {
        assertEquals(
            CompletionStage.class,
            ZLinkAuthorityStore.class.getMethod(
                "read",
                String.class,
                ZLinkStoreCancellation.class).getReturnType());
        assertEquals(
            CompletionStage.class,
            ZLinkAuthorityStore.class.getMethod(
                "compareExchange",
                String.class,
                ZLinkAuthorityExpectation.class,
                ZLinkAuthorityMutation.class,
                ZLinkStoreCancellation.class).getReturnType());
        assertEquals(
            CompletionStage.class,
            ZLinkAuthorityStore.class.getMethod(
                "list",
                String.class,
                Optional.class,
                int.class,
                ZLinkStoreCancellation.class).getReturnType());
        assertEquals(
            CompletionStage.class,
            ZLinkAuthorityStore.class.getMethod(
                "prepareAggregate",
                ZLinkAggregatePrepareRequest.class,
                ZLinkStoreCancellation.class).getReturnType());
    }

    @Test
    void authorityPayloadRecordsDefensivelyCopyBytes() {
        byte[] payload = new byte[] {1, 2, 3};
        ZLinkAuthoritySnapshot snapshot = new ZLinkAuthoritySnapshot(
            "v1",
            payload,
            11,
            12,
            "owner",
            13,
            Instant.EPOCH);
        ZLinkAuthorityPut put = new ZLinkAuthorityPut(
            payload,
            systems.zlink.framework.locations
                .ZLinkAuthorityGenerationTransition.PRESERVE);
        ZLinkAuthorityStored stored = new ZLinkAuthorityStored(
            "v2",
            payload,
            11,
            12,
            Instant.EPOCH);

        payload[0] = 9;
        assertArrayEquals(new byte[] {1, 2, 3}, snapshot.payload());
        assertArrayEquals(new byte[] {1, 2, 3}, put.payload());
        assertArrayEquals(new byte[] {1, 2, 3}, stored.payload());
        snapshot.payload()[1] = 9;
        assertArrayEquals(new byte[] {1, 2, 3}, snapshot.payload());
    }

    @Test
    void registeredLocationCapabilityExposesTheSameAuthorityProvider() {
        ZLinkAuthorityStore authority = new ContractAuthorityStore();
        ZLinkRegisteredLocationStores stores =
            new ZLinkRegisteredLocationStores(
                null,
                null,
                null,
                null,
                null,
                authority,
                null,
                null,
                null);
        ZLinkHandlerActivator.MutableServices services =
            ZLinkHandlerActivator.services();

        stores.addTo(services);

        assertSame(authority, services.create(ZLinkAuthorityStore.class));
    }

    private static final class ContractAuthorityStore
        implements ZLinkAuthorityStore {
        @Override
        public CompletionStage<ZLinkAuthorityReadResult> read(
            String key,
            ZLinkStoreCancellation cancellation) {
            throw new AssertionError();
        }

        @Override
        public CompletionStage<ZLinkAuthorityWriteResult> compareExchange(
            String key,
            ZLinkAuthorityExpectation expectation,
            ZLinkAuthorityMutation mutation,
            ZLinkStoreCancellation cancellation) {
            throw new AssertionError();
        }

        @Override
        public CompletionStage<ZLinkAuthorityScanResult> list(
            String prefix,
            Optional<ZLinkAuthorityScanCursor> cursor,
            int limit,
            ZLinkStoreCancellation cancellation) {
            throw new AssertionError();
        }

        @Override
        public CompletionStage<ZLinkObjectReserveResult> reserve(
            ZLinkObjectReservationRequest request,
            ZLinkStoreCancellation cancellation) {
            throw new AssertionError();
        }

        @Override
        public CompletionStage<ZLinkObjectCommitResult> commit(
            ZLinkObjectReservation reservation,
            byte[] readyPayload,
            ZLinkStoreCancellation cancellation) {
            throw new AssertionError();
        }

        @Override
        public CompletionStage<ZLinkObjectAbortResult> abort(
            ZLinkObjectReservation reservation,
            ZLinkStoreCancellation cancellation) {
            throw new AssertionError();
        }

        @Override
        public CompletionStage<ZLinkAggregatePrepareResult> prepareAggregate(
            ZLinkAggregatePrepareRequest request,
            ZLinkStoreCancellation cancellation) {
            throw new AssertionError();
        }

        @Override
        public CompletionStage<ZLinkAggregateCommitResult> commitAggregate(
            ZLinkAggregateFence fence,
            ZLinkStoreCancellation cancellation) {
            throw new AssertionError();
        }

        @Override
        public CompletionStage<ZLinkAggregateAbortResult> abortAggregate(
            ZLinkAggregateFence fence,
            ZLinkStoreCancellation cancellation) {
            throw new AssertionError();
        }
    }
}
