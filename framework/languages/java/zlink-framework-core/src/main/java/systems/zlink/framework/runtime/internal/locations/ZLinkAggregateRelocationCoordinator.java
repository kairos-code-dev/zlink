package systems.zlink.framework.runtime.internal.locations;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.locations.*;

/**
 * Prepares an immutable relocation root and publishes all participant routes
 * through one bounded Location Store aggregate commit.
 */
public final class ZLinkAggregateRelocationCoordinator {
    private static final Duration RETENTION = Duration.ofHours(24);
    private static final ZLinkStoreCancellation NEVER_CANCELLED = () -> false;

    private final ZLinkAuthorityStore authorityStore;
    private final ZLinkRelocationStore relocationStore;

    public ZLinkAggregateRelocationCoordinator(
        ZLinkAuthorityStore authorityStore,
        ZLinkRelocationStore relocationStore) {
        this.authorityStore = Objects.requireNonNull(
            authorityStore,
            "authorityStore");
        this.relocationStore = Objects.requireNonNull(
            relocationStore,
            "relocationStore");
    }

    public CompletionStage<Prepared> prepare(
        Request request,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(cancellation, "cancellation");
        byte[] digest = inventoryDigest(request.participants());
        byte[] root = request.root();
        return ZLinkRelocationTreeStore.put(
                relocationStore,
                root,
                digest,
                RETENTION,
                cancellation)
            .thenCompose(tree -> ZLinkRelocationTreeStore.read(
                    relocationStore,
                    tree.root().reference(),
                    tree.root().checksumCrc32c(),
                    cancellation)
                .thenCompose(read -> {
                    if (!Arrays.equals(read.logicalRoot(), root)
                        || !Arrays.equals(read.inventoryDigest(), digest)) {
                        return deleteOrphan(tree.root().reference()).thenCompose(
                            ignored -> failed(new RelocationDataLostException(
                                "relocation tree read-back did not preserve the root")));
                    }
                    return prepareAuthority(
                        request,
                        digest,
                        tree.root(),
                        cancellation);
                }));
    }

    public CompletionStage<Published> commit(
        Prepared prepared,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(prepared, "prepared");
        Objects.requireNonNull(cancellation, "cancellation");
        return ZLinkRelocationTreeStore.renew(
                relocationStore,
                prepared.stored().reference(),
                prepared.stored().checksumCrc32c(),
                RETENTION,
                cancellation)
            .thenCompose(ignored -> commitAuthority(prepared, cancellation));
    }

    /** Reads and verifies the immutable logical root selected by authority. */
    public CompletionStage<Root> readRoot(
        String reference,
        long checksumCrc32c,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(reference, "reference");
        Objects.requireNonNull(cancellation, "cancellation");
        return ZLinkRelocationTreeStore.read(
                relocationStore,
                reference,
                checksumCrc32c,
                cancellation)
            .thenApply(value -> new Root(
                value.logicalRoot(),
                value.inventoryDigest()));
    }

    /**
     * Verifies that Location authority has published the exact immutable root
     * and target owner before a target activation becomes visible.
     */
    public CompletionStage<Void> verifyPublishedRoot(
        String authorityKey,
        ZLinkAggregateFence fence,
        String reference,
        long checksumCrc32c,
        ZLinkLocationOwnerToken targetOwner,
        byte[] inventoryDigest,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(authorityKey, "authorityKey");
        Objects.requireNonNull(fence, "fence");
        Objects.requireNonNull(reference, "reference");
        Objects.requireNonNull(targetOwner, "targetOwner");
        byte[] expectedDigest = Objects.requireNonNull(
            inventoryDigest,
            "inventoryDigest").clone();
        Objects.requireNonNull(cancellation, "cancellation");
        return authorityStore.read(authorityKey, cancellation)
            .thenCompose(read -> {
                if (!(read instanceof ZLinkAuthoritySnapshot snapshot)) {
                    return failed(new RelocationDataLostException(
                        "published relocation authority is missing: "
                            + authorityKey));
                }
                ZLinkRelocationAuthorityPayloadCodec.Payload payload =
                    ZLinkRelocationAuthorityPayloadCodec.decode(
                        snapshot.payload());
                if (payload == null
                    || !payload.aggregateId().equals(fence.aggregateId())
                    || payload.aggregateGeneration()
                        != fence.aggregateGeneration()
                    || !payload.reference().equals(reference)
                    || payload.checksumCrc32c() != checksumCrc32c
                    || !payload.targetOwnerId().equals(targetOwner.ownerId())
                    || payload.targetOwnerLeaseGeneration()
                        != targetOwner.leaseGeneration()
                    || !Arrays.equals(
                        payload.inventoryDigest(),
                        expectedDigest)) {
                    return failed(new RelocationDataLostException(
                        "published relocation authority has a different fence: "
                            + authorityKey));
                }
                return CompletableFuture.completedFuture(null);
            });
    }

    public CompletionStage<Void> abort(Prepared prepared) {
        Objects.requireNonNull(prepared, "prepared");
        return authorityStore.abortAggregate(
                prepared.fence(),
                NEVER_CANCELLED)
            .thenCompose(result -> {
                if (result != ZLinkAggregateAbortResult.ABORTED
                    && result != ZLinkAggregateAbortResult.ALREADY_ABORTED) {
                    return failed(new IllegalStateException(
                        "aggregate relocation abort was rejected: " + result));
                }
                return deleteOrphan(prepared.stored().reference());
            });
    }

    public record Root(byte[] payload, byte[] inventoryDigest) {
        public Root {
            payload = Objects.requireNonNull(payload, "payload").clone();
            inventoryDigest = Objects.requireNonNull(
                inventoryDigest,
                "inventoryDigest").clone();
        }

        @Override public byte[] payload() { return payload.clone(); }
        @Override public byte[] inventoryDigest() {
            return inventoryDigest.clone();
        }
    }

    /**
     * Publishes the source-cleanup completion root without changing ownership,
     * generations, membership or capacity. The previous root is deleted only
     * after the completion aggregate is durably committed.
     */
    public CompletionStage<Published> completeSourceCleanup(
        Published published,
        byte[] completedRoot,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(published, "published");
        Objects.requireNonNull(completedRoot, "completedRoot");
        Objects.requireNonNull(cancellation, "cancellation");
        if (published.request().aggregateGeneration() == Long.MAX_VALUE) {
            return failed(new IllegalStateException(
                "aggregate generation is exhausted"));
        }
        List<Participant> completedParticipants = new ArrayList<>();
        CompletionStage<Void> reads = CompletableFuture.completedFuture(null);
        for (Participant original : canonical(
            published.request().participants())) {
            reads = reads.thenCompose(ignored -> authorityStore.read(
                    original.authorityKey(),
                    cancellation)
                .thenCompose(read -> {
                    if (!(read instanceof ZLinkAuthoritySnapshot snapshot)) {
                        return failed(new RelocationDataLostException(
                            "relocation authority disappeared before source cleanup: "
                                + original.authorityKey()));
                    }
                    ZLinkRelocationAuthorityPayloadCodec.Payload current =
                        ZLinkRelocationAuthorityPayloadCodec.decode(
                            snapshot.payload());
                    if (current == null
                        || !current.reference().equals(
                            published.stored().reference())
                        || current.checksumCrc32c()
                            != published.stored().checksumCrc32c()
                        || !current.aggregateId().equals(
                            published.fence().aggregateId())
                        || current.aggregateGeneration()
                            != published.fence().aggregateGeneration()
                        || !current.targetOwnerId().equals(
                            published.request().targetOwner().ownerId())
                        || current.targetOwnerLeaseGeneration()
                            != published.request().targetOwner()
                                .leaseGeneration()) {
                        return failed(new RelocationDataLostException(
                            "relocation authority has a different completion fence: "
                                + original.authorityKey()));
                    }
                    completedParticipants.add(new Participant(
                        original.authorityKey(),
                        original.objectKind(),
                        snapshot.objectGeneration(),
                        snapshot.authorityOwnerGeneration(),
                        snapshot.storeVersion(),
                        ZLinkAuthorityGenerationTransition.PRESERVE,
                        current.applicationPayload(),
                        new byte[0]));
                    return CompletableFuture.completedFuture(null);
                }));
        }
        return reads.thenCompose(ignored -> {
            Request completion = new Request(
                published.request().aggregateId(),
                published.request().aggregateGeneration() + 1,
                completedParticipants,
                completedRoot,
                published.request().targetDescriptor(),
                published.request().targetDescriptorLifecycleGeneration(),
                new ZLinkPlacementCapacityBundle(
                    0,
                    0,
                    java.util.Optional.empty()),
                published.request().targetOwner());
            return prepare(completion, cancellation)
                .thenCompose(prepared -> commit(prepared, cancellation))
                .thenCompose(completed -> deleteOrphan(
                        published.stored().reference())
                    .thenApply(deleted -> completed));
        });
    }

    private CompletionStage<Prepared> prepareAuthority(
        Request request,
        byte[] digest,
        ZLinkRelocationStored stored,
        ZLinkStoreCancellation cancellation) {
        List<ZLinkAggregateParticipant> mutations = new ArrayList<>();
        for (Participant participant : canonical(request.participants())) {
            byte[] authorityPayload = ZLinkRelocationAuthorityPayloadCodec.encode(
                new ZLinkRelocationAuthorityPayloadCodec.Payload(
                    stored.reference(),
                    stored.checksumCrc32c(),
                    request.aggregateId(),
                    request.aggregateGeneration(),
                    digest,
                    request.targetOwner().ownerId(),
                    request.targetOwner().leaseGeneration(),
                    participant.applicationAuthorityPayload()));
            mutations.add(new ZLinkAggregateParticipant(
                participant.authorityKey(),
                participant.expectedStoreVersion(),
                participant.ownerTransition(),
                authorityPayload,
                participant.membershipMutation()));
        }
        ZLinkAggregatePrepareRequest storeRequest =
            new ZLinkAggregatePrepareRequest(
                request.aggregateId(),
                request.aggregateGeneration(),
                mutations,
                digest,
                request.targetDescriptor(),
                request.targetDescriptorLifecycleGeneration(),
                request.capacityBundle(),
                request.targetOwner());
        ZLinkAggregateFence expectedFence = new ZLinkAggregateFence(
            request.aggregateId(),
            request.aggregateGeneration());
        CompletionStage<ZLinkAggregatePrepareResult> operation;
        try {
            operation = authorityStore.prepareAggregate(storeRequest, cancellation);
        } catch (RuntimeException failure) {
            operation = failed(failure);
        }
        return operation.handle((result, failure) -> new Attempt<>(result, failure))
            .thenCompose(attempt -> {
                if (attempt.failure() == null) {
                    ZLinkAggregateFence fence;
                    if (attempt.result() instanceof ZLinkAggregatePrepared value) {
                        fence = value.fence();
                    } else if (attempt.result()
                        instanceof ZLinkAggregateAlreadyPrepared value) {
                        fence = value.fence();
                    } else {
                        return deleteOrphan(stored.reference()).thenCompose(
                            ignored -> failed(new AuthorityConflictException(
                                attempt.result())));
                    }
                    return CompletableFuture.completedFuture(new Prepared(
                        fence,
                        stored,
                        request,
                        digest));
                }
                Throwable original = unwrap(attempt.failure());
                return abortAfterAmbiguousPrepare(
                        expectedFence,
                        stored.reference())
                    .thenCompose(safeToResume -> safeToResume
                        ? failed(original)
                        : failed(new PreparationOutcomeUnknownException(
                            "aggregate prepare outcome could not be reconciled",
                            original)));
            });
    }

    private CompletionStage<Published> commitAuthority(
        Prepared prepared,
        ZLinkStoreCancellation cancellation) {
        CompletionStage<ZLinkAggregateCommitResult> operation;
        try {
            operation = authorityStore.commitAggregate(
                prepared.fence(),
                cancellation);
        } catch (RuntimeException failure) {
            operation = failed(failure);
        }
        return operation.handle((result, failure) -> new Attempt<>(result, failure))
            .thenCompose(attempt -> {
                if (attempt.failure() == null
                    && (attempt.result() == ZLinkAggregateCommitResult.COMMITTED
                        || attempt.result()
                            == ZLinkAggregateCommitResult.ALREADY_COMMITTED)) {
                    return CompletableFuture.completedFuture(
                        new Published(
                            prepared.fence(),
                            prepared.stored(),
                            prepared.request(),
                            prepared.inventoryDigest()));
                }
                Throwable original = attempt.failure() == null
                    ? new AuthorityConflictException(attempt.result())
                    : unwrap(attempt.failure());
                return isPublished(prepared).thenCompose(published -> published
                    ? CompletableFuture.completedFuture(new Published(
                        prepared.fence(),
                        prepared.stored(),
                        prepared.request(),
                        prepared.inventoryDigest()))
                    : failed(original));
            });
    }

    private CompletionStage<Boolean> isPublished(Prepared prepared) {
        CompletionStage<Boolean> result =
            CompletableFuture.completedFuture(true);
        for (Participant participant : prepared.request().participants()) {
            result = result.thenCompose(matches -> {
                if (!matches) {
                    return CompletableFuture.completedFuture(false);
                }
                return authorityStore.read(
                        participant.authorityKey(),
                        NEVER_CANCELLED)
                    .handle((read, failure) -> {
                        if (failure != null
                            || !(read instanceof ZLinkAuthoritySnapshot snapshot)) {
                            return false;
                        }
                        ZLinkRelocationAuthorityPayloadCodec.Payload payload =
                            ZLinkRelocationAuthorityPayloadCodec.decode(
                                snapshot.payload());
                        return payload != null
                            && payload.reference().equals(
                                prepared.stored().reference())
                            && payload.checksumCrc32c()
                                == prepared.stored().checksumCrc32c()
                            && payload.aggregateId().equals(
                                prepared.fence().aggregateId())
                            && payload.aggregateGeneration()
                                == prepared.fence().aggregateGeneration()
                            && Arrays.equals(
                                payload.inventoryDigest(),
                                prepared.inventoryDigest());
                    });
            });
        }
        return result;
    }

    private CompletionStage<Boolean> abortAfterAmbiguousPrepare(
        ZLinkAggregateFence fence,
        String reference) {
        return authorityStore.abortAggregate(fence, NEVER_CANCELLED)
            .handle((result, failure) -> failure == null
                && (result == ZLinkAggregateAbortResult.ABORTED
                    || result == ZLinkAggregateAbortResult.ALREADY_ABORTED))
            .thenCompose(safeToDelete -> safeToDelete
                ? deleteOrphan(reference).thenApply(ignored -> true)
                : CompletableFuture.completedFuture(false));
    }

    private CompletionStage<Void> deleteOrphan(String reference) {
        return ZLinkRelocationTreeStore.delete(
                relocationStore,
                reference,
                NEVER_CANCELLED)
            .handle((ignored, failure) -> null);
    }

    private static List<Participant> canonical(List<Participant> participants) {
        return participants.stream()
            .sorted(Comparator.comparing(
                Participant::authorityKey,
                ZLinkAggregateRelocationCoordinator::compareUtf8))
            .toList();
    }

    private static byte[] inventoryDigest(List<Participant> participants) {
        Writer writer = new Writer();
        for (Participant participant : canonical(participants)) {
            writer.text32(participant.authorityKey());
            writer.u8(participant.objectKind().value());
            writer.i64(participant.objectGeneration());
            writer.i64(participant.authorityOwnerGeneration());
            writer.text32(participant.expectedStoreVersion());
            // Cross-language enum values are Preserve=1 and NewOwner=2.
            writer.u8(participant.ownerTransition().ordinal() + 1);
            writer.bytes32(participant.applicationAuthorityPayload());
            writer.bytes32(participant.membershipMutation());
        }
        try {
            return MessageDigest.getInstance("SHA-256")
                .digest(writer.toByteArray());
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException(impossible);
        }
    }

    private static int compareUtf8(String left, String right) {
        return Arrays.compareUnsigned(
            left.getBytes(StandardCharsets.UTF_8),
            right.getBytes(StandardCharsets.UTF_8));
    }

    private static Throwable unwrap(Throwable failure) {
        Throwable current = failure;
        while (current instanceof CompletionException
            && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    private static <T> CompletionStage<T> failed(Throwable failure) {
        return CompletableFuture.failedFuture(failure);
    }

    public record Participant(
        String authorityKey,
        ZLinkPlacementObjectKind objectKind,
        long objectGeneration,
        long authorityOwnerGeneration,
        String expectedStoreVersion,
        ZLinkAuthorityGenerationTransition ownerTransition,
        byte[] applicationAuthorityPayload,
        byte[] membershipMutation) {
        public Participant {
            if (authorityKey == null || authorityKey.isBlank()
                || expectedStoreVersion == null
                || expectedStoreVersion.isBlank()) {
                throw new IllegalArgumentException(
                    "participant key and StoreVersion are required");
            }
            Objects.requireNonNull(objectKind, "objectKind");
            Objects.requireNonNull(ownerTransition, "ownerTransition");
            if (objectGeneration <= 0 || authorityOwnerGeneration <= 0) {
                throw new IllegalArgumentException(
                    "participant generations must be positive");
            }
            applicationAuthorityPayload = Objects.requireNonNull(
                applicationAuthorityPayload,
                "applicationAuthorityPayload").clone();
            membershipMutation = Objects.requireNonNull(
                membershipMutation,
                "membershipMutation").clone();
        }

        @Override
        public byte[] applicationAuthorityPayload() {
            return applicationAuthorityPayload.clone();
        }

        @Override
        public byte[] membershipMutation() {
            return membershipMutation.clone();
        }
    }

    public record Request(
        UUID aggregateId,
        long aggregateGeneration,
        List<Participant> participants,
        byte[] root,
        ZLinkMeshNodeDescriptorKey targetDescriptor,
        long targetDescriptorLifecycleGeneration,
        ZLinkPlacementCapacityBundle capacityBundle,
        ZLinkLocationOwnerToken targetOwner) {
        public Request {
            Objects.requireNonNull(aggregateId, "aggregateId");
            if (aggregateId.equals(new UUID(0, 0))
                || aggregateGeneration <= 0
                || targetDescriptorLifecycleGeneration <= 0) {
                throw new IllegalArgumentException(
                    "aggregate and lifecycle generations must be positive");
            }
            participants = List.copyOf(
                Objects.requireNonNull(participants, "participants"));
            if (participants.isEmpty() || participants.size() > 1024) {
                throw new IllegalArgumentException(
                    "participants must contain 1..1024 entries");
            }
            Set<String> keys = new HashSet<>();
            for (Participant participant : participants) {
                if (!keys.add(participant.authorityKey())) {
                    throw new IllegalArgumentException(
                        "participant authority keys must be unique");
                }
            }
            root = Objects.requireNonNull(root, "root").clone();
            if (root.length == 0) {
                throw new IllegalArgumentException("root must not be empty");
            }
            Objects.requireNonNull(targetDescriptor, "targetDescriptor");
            Objects.requireNonNull(capacityBundle, "capacityBundle");
            Objects.requireNonNull(targetOwner, "targetOwner");
            if (targetOwner.ownerId() == null
                || targetOwner.ownerId().isBlank()
                || targetOwner.leaseGeneration() <= 0) {
                throw new IllegalArgumentException(
                    "target owner token is invalid");
            }
        }

        @Override
        public byte[] root() {
            return root.clone();
        }
    }

    public record Prepared(
        ZLinkAggregateFence fence,
        ZLinkRelocationStored stored,
        Request request,
        byte[] inventoryDigest) {
        public Prepared {
            inventoryDigest = inventoryDigest.clone();
        }

        @Override
        public byte[] inventoryDigest() {
            return inventoryDigest.clone();
        }
    }

    public record Published(
        ZLinkAggregateFence fence,
        ZLinkRelocationStored stored,
        Request request,
        byte[] inventoryDigest) {
        public Published {
            Objects.requireNonNull(request, "request");
            inventoryDigest = inventoryDigest.clone();
        }

        @Override
        public byte[] inventoryDigest() {
            return inventoryDigest.clone();
        }
    }

    public static final class AuthorityConflictException
        extends RuntimeException {
        private final Object result;

        public AuthorityConflictException(Object result) {
            super("aggregate relocation authority operation was rejected");
            this.result = result;
        }

        Object result() {
            return result;
        }
    }

    public static final class RelocationDataLostException
        extends RuntimeException {
        public RelocationDataLostException(String message) {
            super(message);
        }
    }

    /** The source seal must remain closed until recovery resolves prepare. */
    public static final class PreparationOutcomeUnknownException
        extends RuntimeException {
        public PreparationOutcomeUnknownException(
            String message,
            Throwable cause) {
            super(message, cause);
        }
    }

    private record Attempt<T>(T result, Throwable failure) {
    }

    private static final class Writer {
        private final ByteArrayOutputStream output = new ByteArrayOutputStream();

        void u8(int value) {
            output.write(value);
        }

        void i32(int value) {
            output.writeBytes(ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(value).array());
        }

        void i64(long value) {
            output.writeBytes(ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
                .putLong(value).array());
        }

        void text32(String value) {
            bytes32(value.getBytes(StandardCharsets.UTF_8));
        }

        void bytes32(byte[] value) {
            i32(value.length);
            output.writeBytes(value);
        }

        byte[] toByteArray() {
            return output.toByteArray();
        }
    }
}
