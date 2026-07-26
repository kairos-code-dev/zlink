package systems.zlink.framework.runtime.internal.locations;

import static org.junit.jupiter.api.Assertions.*;

import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.zip.CRC32C;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.*;

final class ZLinkAggregateRelocationCoordinatorTest {
    private static final ZLinkStoreCancellation NEVER = () -> false;

    @Test
    void targetCanStageAfterPrepareAndBeforeAtomicPublication() {
        FakeAuthorityStore authority = new FakeAuthorityStore();
        FakeRelocationStore relocation = new FakeRelocationStore();
        ZLinkAggregateRelocationCoordinator coordinator =
            new ZLinkAggregateRelocationCoordinator(authority, relocation);

        var prepared = coordinator.prepare(request(), NEVER)
            .toCompletableFuture().join();
        assertEquals(1, authority.prepareCount);
        assertEquals(0, authority.commitCount,
            "target factory and Restore staging precede publication");
        assertArrayEquals(
            authority.prepared.inventoryDigest(),
            prepared.inventoryDigest());
        assertEquals(2, authority.prepared.participants().size());

        var published = coordinator.commit(prepared, NEVER)
            .toCompletableFuture().join();
        assertEquals(1, authority.commitCount);
        assertEquals(prepared.fence(), published.fence());
        assertEquals(2, relocation.renewCount,
            "one chunk and the manifest are renewed");
    }

    @Test
    void prepareConflictDeletesOnlyTheUnpublishedRoot() {
        FakeAuthorityStore authority = new FakeAuthorityStore();
        authority.prepareResult = new ZLinkAggregateConflict();
        FakeRelocationStore relocation = new FakeRelocationStore();
        ZLinkAggregateRelocationCoordinator coordinator =
            new ZLinkAggregateRelocationCoordinator(authority, relocation);

        var failure = assertThrows(
            java.util.concurrent.CompletionException.class,
            () -> coordinator.prepare(request(), NEVER)
                .toCompletableFuture().join());
        assertInstanceOf(
            ZLinkAggregateRelocationCoordinator.AuthorityConflictException.class,
            failure.getCause());
        assertEquals(2, relocation.deleteCount,
            "prepare conflict removes the chunk and manifest");
        assertEquals(0, authority.commitCount);
    }

    @Test
    void explicitAbortReleasesPreparedAggregateBeforeDeletingRoot() {
        FakeAuthorityStore authority = new FakeAuthorityStore();
        FakeRelocationStore relocation = new FakeRelocationStore();
        ZLinkAggregateRelocationCoordinator coordinator =
            new ZLinkAggregateRelocationCoordinator(authority, relocation);
        var prepared = coordinator.prepare(request(), NEVER)
            .toCompletableFuture().join();

        coordinator.abort(prepared).toCompletableFuture().join();

        assertEquals(1, authority.abortCount);
        assertEquals(2, relocation.deleteCount,
            "abort removes the chunk and manifest");
    }

    @Test
    void sourceCleanupPublishesNextGenerationWithPreservedOwnership() {
        FakeAuthorityStore authority = new FakeAuthorityStore();
        FakeRelocationStore relocation = new FakeRelocationStore();
        var coordinator = new ZLinkAggregateRelocationCoordinator(
            authority,
            relocation);
        var activated = coordinator.commit(
                coordinator.prepare(request(), NEVER)
                    .toCompletableFuture().join(),
                NEVER)
            .toCompletableFuture().join();

        var completed = coordinator.completeSourceCleanup(
                activated,
                new byte[] {4, 5, 6},
                NEVER)
            .toCompletableFuture().join();

        assertEquals(8, completed.fence().aggregateGeneration());
        assertEquals(2, authority.commitCount);
        assertTrue(authority.prepared.participants().stream().allMatch(
            participant -> participant.ownerTransition()
                == ZLinkAuthorityGenerationTransition.PRESERVE));
        assertEquals(
            new ZLinkPlacementCapacityBundle(0, 0, Optional.empty()),
            authority.prepared.capacityBundle());
        assertEquals(2, relocation.deleteCount,
            "completion removes the previous chunk and manifest after commit");
    }

    @Test
    void authorityPayloadRoundTripsDotNetGuidAndUnsignedChecksum() {
        UUID aggregateId = UUID.fromString(
            "00112233-4455-6677-8899-aabbccddeeff");
        var payload = new ZLinkRelocationAuthorityPayloadCodec.Payload(
            "root-a",
            0xfedcba98L,
            aggregateId,
            7,
            new byte[32],
            "owner-a",
            9,
            new byte[] {1, 2, 3});

        var decoded = ZLinkRelocationAuthorityPayloadCodec.decode(
            ZLinkRelocationAuthorityPayloadCodec.encode(payload));

        assertNotNull(decoded);
        assertEquals(aggregateId, decoded.aggregateId());
        assertEquals(0xfedcba98L, decoded.checksumCrc32c());
        assertArrayEquals(new byte[] {1, 2, 3}, decoded.applicationPayload());
    }

    @Test
    void treeReadRejectsCorruptChunkBeforeTargetStaging() {
        FakeRelocationStore relocation = new FakeRelocationStore();
        byte[] logicalRoot = new byte[] {9, 8, 7, 6};
        byte[] inventoryDigest = new byte[32];
        var stored = ZLinkRelocationTreeStore.put(
                relocation,
                logicalRoot,
                inventoryDigest,
                Duration.ofHours(24),
                NEVER)
            .toCompletableFuture().join();
        var read = ZLinkRelocationTreeStore.read(
                relocation,
                stored.root().reference(),
                stored.root().checksumCrc32c(),
                NEVER)
            .toCompletableFuture().join();
        assertArrayEquals(logicalRoot, read.logicalRoot());
        assertArrayEquals(inventoryDigest, read.inventoryDigest());

        relocation.corruptChunk();
        var failure = assertThrows(
            java.util.concurrent.CompletionException.class,
            () -> ZLinkRelocationTreeStore.read(
                    relocation,
                    stored.root().reference(),
                    stored.root().checksumCrc32c(),
                    NEVER)
                .toCompletableFuture().join());
        assertInstanceOf(
            ZLinkRelocationTreeStore.DataLostException.class,
            failure.getCause());
    }

    private static ZLinkAggregateRelocationCoordinator.Request request() {
        return new ZLinkAggregateRelocationCoordinator.Request(
            UUID.fromString("00112233-4455-6677-8899-aabbccddeeff"),
            7,
            List.of(
                participant("spot:room-a", ZLinkPlacementObjectKind.USER_SPOT),
                participant("actor:user-a", ZLinkPlacementObjectKind.ACTOR)),
            new byte[] {9, 8, 7},
            new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.from("node-b")),
            4,
            new ZLinkPlacementCapacityBundle(
                1,
                1,
                Optional.of(new ZLinkSpotTypeCapacityDelta(
                    ZLinkPlacementObjectKind.USER_SPOT,
                    "RoomSpot",
                    1))),
            new ZLinkLocationOwnerToken("owner-b", 12));
    }

    private static ZLinkAggregateRelocationCoordinator.Participant participant(
        String key,
        ZLinkPlacementObjectKind kind) {
        return new ZLinkAggregateRelocationCoordinator.Participant(
            key,
            kind,
            3,
            5,
            "version-1",
            ZLinkAuthorityGenerationTransition.NEW_OWNER,
            new byte[] {1},
            new byte[] {2});
    }

    private static final class FakeRelocationStore
        implements ZLinkRelocationStore {
        private final Map<String, byte[]> values = new ConcurrentHashMap<>();
        private final AtomicInteger nextReference = new AtomicInteger();
        private int renewCount;
        private int deleteCount;

        @Override
        public CompletionStage<ZLinkRelocationStored> put(
            byte[] payload,
            Duration retention,
            ZLinkStoreCancellation cancellation) {
            String reference = "reference-" + nextReference.incrementAndGet();
            values.put(reference, payload.clone());
            return CompletableFuture.completedFuture(new ZLinkRelocationStored(
                reference,
                checksum(payload),
                Instant.now().plus(retention),
                Instant.now()));
        }

        @Override
        public CompletionStage<ZLinkRelocationReadResult> get(
            String reference,
            ZLinkStoreCancellation cancellation) {
            byte[] payload = values.get(reference);
            return CompletableFuture.completedFuture(payload == null
                ? new ZLinkRelocationMissing()
                : new ZLinkRelocationFound(payload));
        }

        @Override
        public CompletionStage<ZLinkRelocationRenewResult> renew(
            String reference,
            Duration retention,
            ZLinkStoreCancellation cancellation) {
            renewCount++;
            return CompletableFuture.completedFuture(
                new ZLinkRelocationRenewed(
                    Instant.now().plus(retention),
                    Instant.now()));
        }

        @Override
        public CompletionStage<ZLinkRelocationDeleteResult> delete(
            String reference,
            ZLinkStoreCancellation cancellation) {
            deleteCount++;
            return CompletableFuture.completedFuture(
                values.remove(reference) == null
                    ? ZLinkRelocationDeleteResult.MISSING
                    : ZLinkRelocationDeleteResult.DELETED);
        }

        private static long checksum(byte[] payload) {
            CRC32C checksum = new CRC32C();
            checksum.update(payload);
            return checksum.getValue();
        }

        private void corruptChunk() {
            values.replaceAll((reference, bytes) -> {
                byte[] copy = bytes.clone();
                if (copy.length >= 4 && copy[0] == 'Z' && copy[3] == 'C') {
                    copy[copy.length - 1] ^= 1;
                }
                return copy;
            });
        }
    }

    private static final class FakeAuthorityStore implements ZLinkAuthorityStore {
        private ZLinkAggregatePrepareRequest prepared;
        private ZLinkAggregatePrepareResult prepareResult;
        private int prepareCount;
        private int commitCount;
        private int abortCount;
        private final Map<String, ZLinkAuthoritySnapshot> rows =
            new ConcurrentHashMap<>();

        @Override
        public CompletionStage<ZLinkAggregatePrepareResult> prepareAggregate(
            ZLinkAggregatePrepareRequest request,
            ZLinkStoreCancellation cancellation) {
            prepareCount++;
            prepared = request;
            return CompletableFuture.completedFuture(prepareResult != null
                ? prepareResult
                : new ZLinkAggregatePrepared(new ZLinkAggregateFence(
                    request.aggregateId(),
                    request.aggregateGeneration())));
        }

        @Override
        public CompletionStage<ZLinkAggregateCommitResult> commitAggregate(
            ZLinkAggregateFence fence,
            ZLinkStoreCancellation cancellation) {
            commitCount++;
            for (ZLinkAggregateParticipant participant :
                prepared.participants()) {
                ZLinkAggregateRelocationCoordinator.Participant source =
                    request().participants().stream()
                        .filter(value -> value.authorityKey().equals(
                            participant.authorityKey()))
                        .findFirst()
                        .orElseThrow();
                ZLinkPlacementCapacityBundle capacity =
                    source.objectKind() == ZLinkPlacementObjectKind.ACTOR
                        ? ZLinkPlacementCapacityBundle.actor(1)
                        : ZLinkPlacementCapacityBundle.spot(
                            source.objectKind(), "RoomSpot", 1);
                rows.put(participant.authorityKey(),
                    new ZLinkAuthoritySnapshot(
                        "committed-" + commitCount + "-"
                            + participant.authorityKey(),
                        participant.authorityPayload(),
                        source.objectGeneration(),
                        source.authorityOwnerGeneration() + 1,
                        prepared.targetOwner().ownerId(),
                        prepared.targetOwner().leaseGeneration(),
                        new ZLinkPlacementAllocation(
                            ZLinkPlacementAllocationState.ACTIVE,
                            source.objectKind(),
                            "RoomSpot",
                            prepared.targetDescriptor(),
                            prepared.targetDescriptorLifecycleGeneration(),
                            capacity),
                        Instant.now()));
            }
            return CompletableFuture.completedFuture(
                ZLinkAggregateCommitResult.COMMITTED);
        }

        @Override
        public CompletionStage<ZLinkAggregateAbortResult> abortAggregate(
            ZLinkAggregateFence fence,
            ZLinkStoreCancellation cancellation) {
            abortCount++;
            return CompletableFuture.completedFuture(
                ZLinkAggregateAbortResult.ABORTED);
        }

        @Override
        public CompletionStage<ZLinkAuthorityReadResult> read(
            String key, ZLinkStoreCancellation cancellation) {
            ZLinkAuthoritySnapshot row = rows.get(key);
            return CompletableFuture.completedFuture(row == null
                ? new ZLinkAuthorityMissing(Instant.now())
                : row);
        }

        @Override
        public CompletionStage<ZLinkAuthorityWriteResult> compareExchange(
            String key, ZLinkAuthorityExpectation expectation,
            ZLinkAuthorityMutation mutation, ZLinkStoreCancellation cancellation) {
            return unsupported();
        }

        @Override
        public CompletionStage<ZLinkAuthorityScanResult> list(
            String prefix, Optional<ZLinkAuthorityScanCursor> cursor, int limit,
            ZLinkStoreCancellation cancellation) {
            return unsupported();
        }

        @Override
        public CompletionStage<ZLinkObjectReserveResult> reserve(
            ZLinkObjectReservationRequest request,
            ZLinkStoreCancellation cancellation) {
            return unsupported();
        }

        @Override
        public CompletionStage<ZLinkObjectCommitResult> commit(
            ZLinkObjectReservation reservation, byte[] payload,
            ZLinkStoreCancellation cancellation) {
            return unsupported();
        }

        @Override
        public CompletionStage<ZLinkObjectCommitResult> commit(
            ZLinkObjectReservation reservation, byte[] payload,
            ZLinkCreationOperationTerminal terminal,
            ZLinkStoreCancellation cancellation) {
            return unsupported();
        }

        @Override
        public CompletionStage<ZLinkObjectRejectResult> reject(
            ZLinkObjectReservation reservation,
            ZLinkCreationOperationTerminal terminal,
            ZLinkStoreCancellation cancellation) {
            return unsupported();
        }

        @Override
        public CompletionStage<ZLinkObjectAbortResult> abort(
            ZLinkObjectReservation reservation,
            ZLinkStoreCancellation cancellation) {
            return unsupported();
        }

        @Override
        public CompletionStage<ZLinkObjectAbortResult> abort(
            ZLinkObjectReservation reservation,
            ZLinkCreationOperationTerminal terminal,
            ZLinkStoreCancellation cancellation) {
            return unsupported();
        }

        @Override
        public CompletionStage<ZLinkCreationTerminalReadResult>
            readCreationTerminal(
                ZLinkCreationOperationIdentity operation,
                ZLinkStoreCancellation cancellation) {
            return unsupported();
        }

        @Override
        public CompletionStage<ZLinkRelocationCapacityReserveResult>
            reserveRelocationCapacity(
                ZLinkRelocationCapacityReservationRequest request,
                ZLinkStoreCancellation cancellation) {
            return unsupported();
        }

        @Override
        public CompletionStage<ZLinkRelocationCapacityAbortResult>
            abortRelocationCapacity(
                ZLinkRelocationCapacityFence fence,
                ZLinkStoreCancellation cancellation) {
            return unsupported();
        }

        private static <T> CompletionStage<T> unsupported() {
            return CompletableFuture.failedFuture(
                new UnsupportedOperationException());
        }
    }
}
