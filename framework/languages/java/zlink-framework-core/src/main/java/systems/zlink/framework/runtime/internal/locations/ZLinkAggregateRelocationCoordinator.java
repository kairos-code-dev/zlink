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
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.*;
import systems.zlink.framework.runtime.internal.locations.*;
import systems.zlink.framework.runtime.internal.service.ZLinkServiceRelocationWireCodec;

/**
 * Prepares an immutable relocation root and publishes all participant routes
 * through one bounded Location Store aggregate commit.
 */
public final class ZLinkAggregateRelocationCoordinator {
    private static final Duration RETENTION = Duration.ofHours(24);
    private static final ZLinkStoreCancellation NEVER_CANCELLED = () -> false;

    private final ZLinkLocationRepository authorityStore;
    private final ZLinkRelocationStore relocationStore;

    public ZLinkAggregateRelocationCoordinator(
        ZLinkLocationRepository authorityStore,
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
        return stageRoot(request, cancellation).thenCompose(staged ->
            prepareAuthority(
                staged.request(),
                staged.inventoryDigest(),
                staged.stored(),
                cancellation,
                null));
    }

    /** Stores and verifies an initial factory/Restore root without publishing
     * or preparing Location authority. */
    public CompletionStage<StagedRoot> stageRoot(
        Request request,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(cancellation, "cancellation");
        return stageRoot(
            request,
            inventoryDigest(request.participants()),
            cancellation);
    }

    private CompletionStage<StagedRoot> stageRoot(
        Request request,
        byte[] digest,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(request, "request");
        byte[] expectedDigest = Objects.requireNonNull(digest, "digest").clone();
        byte[] root = request.root();
        return ZLinkRelocationTreeStore.put(
                relocationStore,
                root,
                expectedDigest,
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
                    return CompletableFuture.completedFuture(new StagedRoot(
                        request,
                        tree.root(),
                        expectedDigest));
                }));
    }

    public CompletionStage<Void> discardStagedRoot(StagedRoot staged) {
        Objects.requireNonNull(staged, "staged");
        return deleteOrphan(staged.stored().reference());
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
                ZLinkCanonicalRelocationAuthorityStateCodec.Published payload =
                    ZLinkCanonicalRelocationAuthorityStateCodec.decode(
                        snapshot.payload());
                if (payload == null
                    || !payload.aggregateId().equals(fence.aggregateId())
                    || payload.aggregateGeneration()
                        != fence.aggregateGeneration()
                    || !payload.reference().equals(reference)
                    || payload.checksumCrc32c() != checksumCrc32c
                    || !payload.targetOwnerId().equals(targetOwner.ownerId())
                    || payload.targetOwnerLeaseGeneration()
                        != targetOwner.leaseGeneration()) {
                    return failed(new RelocationDataLostException(
                        "published relocation authority has a different fence: "
                            + authorityKey));
                }
                return CompletableFuture.completedFuture(null);
            });
    }

    /**
     * Resolves the authority-selected final root and verifies every staged
     * participant before target publication. The initial staging root is not
     * an authority reference and is never accepted here.
     */
    public CompletionStage<PublishedRoot> readPublishedAggregate(
        List<ExpectedParticipant> participants,
        ZLinkAggregateFence fence,
        ZLinkLocationOwnerToken targetOwner,
        byte[] inventoryDigest,
        ZLinkStoreCancellation cancellation) {
        List<ExpectedParticipant> expected = List.copyOf(
            Objects.requireNonNull(participants, "participants"));
        if (expected.isEmpty()) {
            throw new IllegalArgumentException(
                "published aggregate participants are required");
        }
        Objects.requireNonNull(fence, "fence");
        Objects.requireNonNull(targetOwner, "targetOwner");
        byte[] digest = Objects.requireNonNull(
            inventoryDigest, "inventoryDigest").clone();
        Objects.requireNonNull(cancellation, "cancellation");
        var publication = new java.util.concurrent.atomic.AtomicReference<
            ZLinkCanonicalRelocationAuthorityStateCodec.Published>();
        Map<String, Long> ownerGenerations = new LinkedHashMap<>();
        CompletionStage<Void> reads = CompletableFuture.completedFuture(null);
        for (ExpectedParticipant participant : expected) {
            reads = reads.thenCompose(ignored -> authorityStore.read(
                    participant.authorityKey(),
                    cancellation)
                .thenCompose(read -> {
                    if (!(read instanceof ZLinkAuthoritySnapshot snapshot)) {
                        return failed(new RelocationDataLostException(
                            "published relocation participant is missing: "
                                + participant.authorityKey()));
                    }
                    ZLinkCanonicalRelocationAuthorityStateCodec.Published payload =
                        ZLinkCanonicalRelocationAuthorityStateCodec.decode(
                            snapshot.payload());
                    ZLinkCanonicalRelocationAuthorityStateCodec.Published first =
                        publication.get();
                    boolean ownerGenerationMatches = ownerGenerationAdvanced(
                        participant.sourceAuthorityOwnerGeneration(),
                        snapshot.authorityOwnerGeneration());
                    if (payload == null
                        || snapshot.objectGeneration()
                            != participant.objectGeneration()
                        || !ownerGenerationMatches
                        || !snapshot.ownerId().equals(targetOwner.ownerId())
                        || snapshot.ownerLeaseGeneration()
                            != targetOwner.leaseGeneration()
                        || !payload.aggregateId().equals(fence.aggregateId())
                        || payload.aggregateGeneration()
                            != fence.aggregateGeneration()
                        || !payload.targetOwnerId().equals(
                            targetOwner.ownerId())
                        || payload.targetOwnerLeaseGeneration()
                            != targetOwner.leaseGeneration()
                        || first != null && (!payload.reference().equals(
                                first.reference())
                            || payload.checksumCrc32c()
                                != first.checksumCrc32c())) {
                        return failed(new RelocationDataLostException(
                            "published relocation participant has a different fence: "
                                + participant.authorityKey()));
                    }
                    ownerGenerations.put(
                        participant.authorityKey(),
                        snapshot.authorityOwnerGeneration());
                    publication.compareAndSet(null, payload);
                    return CompletableFuture.completedFuture(null);
                }));
        }
        return reads.thenCompose(ignored -> {
            var selected = publication.get();
            return readRoot(
                    selected.reference(),
                    selected.checksumCrc32c(),
                    cancellation)
                .thenCompose(root -> Arrays.equals(
                        root.inventoryDigest(), digest)
                    ? CompletableFuture.completedFuture(new PublishedRoot(
                        selected.reference(),
                        selected.checksumCrc32c(),
                        root.payload(),
                        root.inventoryDigest(),
                        ownerGenerations))
                    : failed(new RelocationDataLostException(
                        "published relocation root inventory differs")));
        });
    }

    public record ExpectedParticipant(
        String authorityKey,
        long objectGeneration,
        long sourceAuthorityOwnerGeneration) {
        public ExpectedParticipant {
            if (authorityKey == null || authorityKey.isBlank()
                || objectGeneration <= 0
                || sourceAuthorityOwnerGeneration <= 0) {
                throw new IllegalArgumentException(
                    "published participant fence is invalid");
            }
        }
    }

    public record PublishedRoot(
        String reference,
        long checksumCrc32c,
        byte[] payload,
        byte[] inventoryDigest,
        Map<String, Long> targetOwnerGenerations) {
        public PublishedRoot {
            Objects.requireNonNull(reference, "reference");
            payload = Objects.requireNonNull(payload, "payload").clone();
            inventoryDigest = Objects.requireNonNull(
                inventoryDigest, "inventoryDigest").clone();
            targetOwnerGenerations = immutableOwnerGenerations(
                targetOwnerGenerations);
        }

        public long targetOwnerGeneration(String authorityKey) {
            return ownerGeneration(targetOwnerGenerations, authorityKey);
        }

        @Override public byte[] payload() { return payload.clone(); }
        @Override public byte[] inventoryDigest() {
            return inventoryDigest.clone();
        }
    }

    /** Verifies the post-cleanup aggregate before target admission opens. */
    public CompletionStage<Void> verifyCompletedAggregate(
        List<ExpectedParticipant> participants,
        ZLinkAggregateFence activatedFence,
        ZLinkLocationOwnerToken targetOwner,
        ZLinkStoreCancellation cancellation) {
        List<ExpectedParticipant> expected = List.copyOf(
            Objects.requireNonNull(participants, "participants"));
        Objects.requireNonNull(activatedFence, "activatedFence");
        Objects.requireNonNull(targetOwner, "targetOwner");
        Objects.requireNonNull(cancellation, "cancellation");
        if (expected.isEmpty()
            || activatedFence.aggregateGeneration() == Long.MAX_VALUE) {
            throw new IllegalArgumentException(
                "completed aggregate participant fence is invalid");
        }
        var shared = new java.util.concurrent.atomic.AtomicReference<
            ZLinkCanonicalRelocationAuthorityStateCodec.Published>();
        CompletionStage<Void> reads = CompletableFuture.completedFuture(null);
        for (ExpectedParticipant participant : expected) {
            reads = reads.thenCompose(ignored -> authorityStore.read(
                    participant.authorityKey(),
                    cancellation)
                .thenCompose(read -> {
                    if (!(read instanceof ZLinkAuthoritySnapshot snapshot)) {
                        return failed(new RelocationDataLostException(
                            "completed relocation participant is missing: "
                                + participant.authorityKey()));
                    }
                    var payload = ZLinkCanonicalRelocationAuthorityStateCodec
                        .decode(snapshot.payload());
                    var first = shared.get();
                    boolean ownerGenerationMatches = ownerGenerationAdvanced(
                        participant.sourceAuthorityOwnerGeneration(),
                        snapshot.authorityOwnerGeneration());
                    if (payload == null
                        || !payload.sourceCleanupCompleted()
                        || snapshot.objectGeneration()
                            != participant.objectGeneration()
                        || !ownerGenerationMatches
                        || !snapshot.ownerId().equals(targetOwner.ownerId())
                        || snapshot.ownerLeaseGeneration()
                            != targetOwner.leaseGeneration()
                        || !payload.aggregateId().equals(
                            activatedFence.aggregateId())
                        || !payload.targetOwnerId().equals(
                            targetOwner.ownerId())
                        || payload.targetOwnerLeaseGeneration()
                            != targetOwner.leaseGeneration()
                        || first != null
                            && (!payload.reference().equals(first.reference())
                                || payload.checksumCrc32c()
                                    != first.checksumCrc32c())) {
                        return failed(new RelocationDataLostException(
                            "completed relocation participant has a different fence: "
                                + participant.authorityKey()));
                    }
                    shared.compareAndSet(null, payload);
                    return CompletableFuture.completedFuture(null);
                }));
        }
        return reads.thenCompose(ignored -> {
            var publication = shared.get();
            return readRoot(
                    publication.reference(),
                    publication.checksumCrc32c(),
                    cancellation)
                .thenCompose(root -> {
                    var canonical = ZLinkServiceRelocationEnvelopeCodec.decode(
                        root.payload());
                    return canonical.recoveryReleaseEligible()
                        ? CompletableFuture.completedFuture(null)
                        : failed(new RelocationDataLostException(
                            "completed relocation still has pending replies"));
                });
        });
    }

    /** Replaces the completed relocation publication with the participants'
     * steady application authority payloads in one Location Store aggregate
     * commit. Ownership and both object generations are preserved. */
    public CompletionStage<Void> normalizeCompletedAggregate(
        List<ExpectedParticipant> participants,
        ZLinkAggregateFence activatedFence,
        ZLinkLocationOwnerToken targetOwner,
        ZLinkStoreCancellation cancellation) {
        List<ExpectedParticipant> expected = List.copyOf(
            Objects.requireNonNull(participants, "participants"));
        Objects.requireNonNull(activatedFence, "activatedFence");
        Objects.requireNonNull(targetOwner, "targetOwner");
        Objects.requireNonNull(cancellation, "cancellation");
        if (expected.isEmpty()
            || activatedFence.aggregateGeneration() > Long.MAX_VALUE - 2) {
            throw new IllegalArgumentException(
                "completed aggregate participant fence is invalid");
        }
        long completedGeneration = activatedFence.aggregateGeneration() + 1;
        long steadyGeneration = completedGeneration + 1;
        List<Participant> steady = new ArrayList<>();
        var targetDescriptor = new java.util.concurrent.atomic.AtomicReference<
            ZLinkMeshNodeDescriptorKey>();
        var targetDescriptorGeneration = new java.util.concurrent.atomic
            .AtomicLong();
        var alreadySteady = new java.util.concurrent.atomic.AtomicInteger();
        CompletionStage<Void> reads = CompletableFuture.completedFuture(null);
        for (ExpectedParticipant participant : expected) {
            reads = reads.thenCompose(ignored -> authorityStore.read(
                    participant.authorityKey(), cancellation)
                .thenCompose(read -> collectSteadyParticipant(
                    participant,
                    activatedFence,
                    completedGeneration,
                    targetOwner,
                    read,
                    steady,
                    targetDescriptor,
                    targetDescriptorGeneration,
                    alreadySteady)));
        }
        return reads.thenCompose(ignored -> {
            if (alreadySteady.get() == expected.size()) {
                return CompletableFuture.completedFuture(null);
            }
            if (alreadySteady.get() != 0 || steady.size() != expected.size()) {
                return failed(new RelocationDataLostException(
                    "completed relocation normalization is partially visible"));
            }
            List<Participant> canonicalSteady = canonical(steady);
            List<ZLinkAggregateParticipant> mutations = canonicalSteady.stream()
                .map(value -> new ZLinkAggregateParticipant(
                    value.authorityKey(),
                    value.expectedStoreVersion(),
                    ZLinkAuthorityGenerationTransition.PRESERVE,
                    value.applicationAuthorityPayload(),
                    new byte[0]))
                .toList();
            var request = new ZLinkAggregatePrepareRequest(
                activatedFence.aggregateId(),
                steadyGeneration,
                mutations,
                inventoryDigest(canonicalSteady),
                targetDescriptor.get(),
                targetDescriptorGeneration.get(),
                new ZLinkPlacementCapacityBundle(
                    0, 0, java.util.Optional.empty()),
                targetOwner);
            var fence = new ZLinkAggregateFence(
                activatedFence.aggregateId(), steadyGeneration);
            return authorityStore.prepareAggregate(request, cancellation)
                .thenCompose(result -> {
                    boolean prepared = result
                            instanceof ZLinkAggregatePrepared newlyPrepared
                            && newlyPrepared.fence().equals(fence)
                        || result instanceof ZLinkAggregateAlreadyPrepared existing
                            && existing.fence().equals(fence);
                    if (prepared) {
                        return authorityStore.commitAggregate(
                            fence, cancellation);
                    }
                    return failed(new RelocationDataLostException(
                        "steady authority normalization prepare conflicted"));
                })
                .thenCompose(result ->
                    result == ZLinkAggregateCommitResult.COMMITTED
                        || result == ZLinkAggregateCommitResult.ALREADY_COMMITTED
                        ? CompletableFuture.completedFuture(null)
                        : failed(new RelocationDataLostException(
                            "steady authority normalization commit conflicted")));
        });
    }

    /** Stores one immutable replay successor and atomically replaces every
     * participant authority pointer while preserving ownership generations. */
    public CompletionStage<CanonicalProgress> updateCanonicalReplay(
        List<ExpectedParticipant> participants,
        ZLinkLocationOwnerToken targetOwner,
        java.util.function.UnaryOperator<
            ZLinkServiceRelocationEnvelopeCodec.Envelope> update,
        ZLinkStoreCancellation cancellation) {
        List<ExpectedParticipant> expected = List.copyOf(
            Objects.requireNonNull(participants, "participants"));
        Objects.requireNonNull(targetOwner, "targetOwner");
        Objects.requireNonNull(update, "update");
        Objects.requireNonNull(cancellation, "cancellation");
        if (expected.isEmpty()) {
            throw new IllegalArgumentException(
                "canonical replay participants are required");
        }
        List<ZLinkAuthoritySnapshot> snapshots = new ArrayList<>();
        var shared = new java.util.concurrent.atomic.AtomicReference<
            ZLinkCanonicalRelocationAuthorityStateCodec.Published>();
        CompletionStage<Void> reads = CompletableFuture.completedFuture(null);
        for (ExpectedParticipant participant : expected) {
            reads = reads.thenCompose(ignored -> authorityStore.read(
                    participant.authorityKey(), cancellation)
                .thenCompose(result -> {
                    if (!(result instanceof ZLinkAuthoritySnapshot snapshot)
                        || snapshot.objectGeneration()
                            != participant.objectGeneration()
                        || !ownerGenerationAdvanced(
                            participant.sourceAuthorityOwnerGeneration(),
                            snapshot.authorityOwnerGeneration())
                        || !snapshot.ownerId().equals(targetOwner.ownerId())
                        || snapshot.ownerLeaseGeneration()
                            != targetOwner.leaseGeneration()) {
                        return failed(new RelocationDataLostException(
                            "canonical replay authority owner differs: "
                                + participant.authorityKey()));
                    }
                    var publication =
                        ZLinkCanonicalRelocationAuthorityStateCodec.decode(
                            snapshot.payload());
                    var first = shared.get();
                    if (publication == null
                        || !publication.targetOwnerId().equals(
                            targetOwner.ownerId())
                        || publication.targetOwnerLeaseGeneration()
                            != targetOwner.leaseGeneration()
                        || first != null
                            && (!publication.reference().equals(
                                    first.reference())
                                || publication.checksumCrc32c()
                                    != first.checksumCrc32c()
                                || !publication.aggregateId().equals(
                                    first.aggregateId()))) {
                        return failed(new RelocationDataLostException(
                            "canonical replay authority pointers differ"));
                    }
                    shared.compareAndSet(null, publication);
                    snapshots.add(snapshot);
                    return CompletableFuture.completedFuture(null);
                }));
        }
        return reads.thenCompose(ignored -> {
            var publication = shared.get();
            return readRoot(
                    publication.reference(),
                    publication.checksumCrc32c(),
                    cancellation)
                .thenCompose(stored -> {
                    var current = ZLinkServiceRelocationEnvelopeCodec.decode(
                        stored.payload());
                    var successor = Objects.requireNonNull(
                        update.apply(current), "canonical replay successor");
                    if (java.util.Arrays.equals(
                        successor.canonicalBytes(), current.canonicalBytes())) {
                        return CompletableFuture.completedFuture(
                            new CanonicalProgress(
                                current,
                                snapshots.stream()
                                    .map(ZLinkAuthoritySnapshot::storeVersion)
                                    .toList(),
                                snapshots.stream()
                                    .map(ZLinkAuthoritySnapshot
                                        ::authorityOwnerGeneration)
                                    .toList()));
                    }
                    List<Participant> mutations = new ArrayList<>();
                    for (int index = 0; index < expected.size(); index++) {
                        var participant = expected.get(index);
                        var snapshot = snapshots.get(index);
                        mutations.add(new Participant(
                            participant.authorityKey(),
                            snapshot.allocation().objectKind(),
                            snapshot.objectGeneration(),
                            snapshot.authorityOwnerGeneration(),
                            snapshot.storeVersion(),
                            ZLinkAuthorityGenerationTransition.PRESERVE,
                            snapshot.payload(),
                            new byte[0]));
                    }
                    var allocation = snapshots.getFirst().allocation();
                    var request = new Request(
                        publication.aggregateId(),
                        publication.aggregateGeneration(),
                        mutations,
                        successor.canonicalBytes(),
                        allocation.descriptor(),
                        allocation.descriptorLifecycleGeneration(),
                        new ZLinkPlacementCapacityBundle(
                            0, 0, java.util.Optional.empty()),
                        targetOwner);
                    return stageRoot(
                            request,
                            stored.inventoryDigest(),
                            cancellation)
                        .thenCompose(staged -> updateCanonicalAuthorities(
                            mutations,
                            request,
                            staged.stored(),
                            successor,
                            publication.sourceCleanupCompleted(),
                            cancellation));
                });
        });
    }

    /**
     * Applies a post-commit root or replay transition with the ordinary
     * StoreVersion CAS. The bounded aggregate commit is reserved for the
     * initial ownership, membership and capacity transition.
     */
    private CompletionStage<CanonicalProgress> updateCanonicalAuthorities(
        List<Participant> participants,
        Request request,
        ZLinkRelocationStored stored,
        ZLinkServiceRelocationEnvelopeCodec.Envelope root,
        boolean sourceCleanupCompleted,
        ZLinkStoreCancellation cancellation) {
        List<String> versions = new ArrayList<>();
        List<Long> ownerGenerations = new ArrayList<>();
        CompletionStage<Void> writes = CompletableFuture.completedFuture(null);
        for (Participant participant : participants) {
            byte[] payload =
                ZLinkCanonicalRelocationAuthorityStateCodec.publish(
                    participant.applicationAuthorityPayload(),
                    request,
                    ZLinkAuthorityGenerationTransition.PRESERVE,
                    stored,
                    sourceCleanupCompleted);
            writes = writes.thenCompose(ignored ->
                authorityStore.compareExchange(
                        participant.authorityKey(),
                        new ZLinkAuthorityExpectFound(
                            participant.expectedStoreVersion()),
                        new ZLinkAuthorityPut(
                            payload,
                            ZLinkAuthorityGenerationTransition.PRESERVE,
                            java.util.Optional.empty(),
                            java.util.Optional.empty()),
                        cancellation)
                    .thenCompose(result -> {
                        if (!(result instanceof ZLinkAuthorityStored value)) {
                            return failed(new RelocationDataLostException(
                                "post-commit canonical authority CAS conflicted: "
                                    + participant.authorityKey()));
                        }
                        versions.add(value.storeVersion());
                        ownerGenerations.add(
                            value.authorityOwnerGeneration());
                        return CompletableFuture.completedFuture(null);
                    }));
        }
        return writes.thenApply(ignored -> new CanonicalProgress(
            root, versions, ownerGenerations));
    }

    /** Reads the exact completion published by command 33's authority fence. */
    public CompletionStage<ZLinkServiceRelocationEnvelopeCodec.Completion>
        readCanonicalReply(
            String authorityKey,
            long objectGeneration,
            String sourceOwnerId,
            long sourceOwnerLeaseGeneration,
            RoutingId sourceNodeRid,
            long sourceNodeGeneration,
            ZLinkServiceRelocationWireCodec.ReplyRelay relay,
            ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(authorityKey, "authorityKey");
        Objects.requireNonNull(sourceOwnerId, "sourceOwnerId");
        Objects.requireNonNull(sourceNodeRid, "sourceNodeRid");
        Objects.requireNonNull(relay, "relay");
        Objects.requireNonNull(cancellation, "cancellation");
        return authorityStore.read(authorityKey, cancellation)
            .thenCompose(result -> {
                if (!(result instanceof ZLinkAuthoritySnapshot snapshot)
                    || snapshot.objectGeneration() != objectGeneration
                    || snapshot.authorityOwnerGeneration()
                        != relay.targetAttemptGeneration()
                    || !snapshot.storeVersion().equals(
                        relay.coordinator()
                            .expectedAuthorityStoreVersion())
                    || !snapshot.ownerId().equals(
                        relay.coordinator().ownerId())
                    || snapshot.ownerLeaseGeneration()
                        != relay.coordinator().leaseGeneration()
                    || !snapshot.allocation().descriptor().rid().equals(
                        relay.coordinator().nodeRid())
                    || snapshot.allocation()
                        .descriptorLifecycleGeneration()
                        != relay.coordinator().nodeGeneration()) {
                    return failed(new RelocationDataLostException(
                        "canonical reply authority fence differs"));
                }
                var publication =
                    ZLinkCanonicalRelocationAuthorityStateCodec.decode(
                        snapshot.payload());
                UUID relocation = new UUID(
                    relay.relocation().high(), relay.relocation().low());
                if (publication == null
                    || !publication.aggregateId().equals(relocation)
                    || !publication.targetOwnerId().equals(
                        relay.coordinator().ownerId())
                    || publication.targetOwnerLeaseGeneration()
                        != relay.coordinator().leaseGeneration()) {
                    return failed(new RelocationDataLostException(
                        "canonical reply publication fence differs"));
                }
                return readRoot(
                        publication.reference(),
                        publication.checksumCrc32c(),
                        cancellation)
                    .thenCompose(root -> {
                        var envelope =
                            ZLinkServiceRelocationEnvelopeCodec.decode(
                                root.payload());
                        return envelope.terminalCompletions().stream()
                            .filter(value ->
                                value.operationHigh()
                                    == relay.operation().high()
                                && value.operationLow()
                                    == relay.operation().low()
                                && value.sourceOwnerId().equals(sourceOwnerId)
                                && value.sourceOwnerLeaseGeneration()
                                    == sourceOwnerLeaseGeneration
                                && value.sourceNodeRid().equals(
                                    sourceNodeRid.toString())
                                && value.sourceNodeGeneration()
                                    == sourceNodeGeneration
                                && value.participantId()
                                    == relay.participantId()
                                && value.sequence() == relay.sequence()
                                && value.terminalResult()
                                    == relay.terminalResult()
                                && value.failureCode()
                                    == relay.failureCode())
                            .findFirst()
                            .<CompletionStage<ZLinkServiceRelocationEnvelopeCodec.Completion>>map(
                                CompletableFuture::completedFuture)
                            .orElseGet(() -> failed(
                                new RelocationDataLostException(
                                    "canonical reply completion is absent")));
                    });
            });
    }

    private CompletionStage<Void> collectSteadyParticipant(
        ExpectedParticipant participant,
        ZLinkAggregateFence activatedFence,
        long completedGeneration,
        ZLinkLocationOwnerToken targetOwner,
        ZLinkAuthorityReadResult read,
        List<Participant> steady,
        java.util.concurrent.atomic.AtomicReference<ZLinkMeshNodeDescriptorKey>
            targetDescriptor,
        java.util.concurrent.atomic.AtomicLong targetDescriptorGeneration,
        java.util.concurrent.atomic.AtomicInteger alreadySteady) {
        if (!(read instanceof ZLinkAuthoritySnapshot snapshot)
            || !matchesSteadyOwner(snapshot, participant, targetOwner)) {
            return failed(new RelocationDataLostException(
                "completed relocation participant has a different owner fence: "
                    + participant.authorityKey()));
        }
        var publication = ZLinkCanonicalRelocationAuthorityStateCodec.decode(
            snapshot.payload());
        if (publication == null) {
            alreadySteady.incrementAndGet();
            return CompletableFuture.completedFuture(null);
        }
        if (!matchesCompletedParticipant(
            snapshot,
            publication,
            participant,
            activatedFence,
            completedGeneration,
            targetOwner)) {
            return failed(new RelocationDataLostException(
                "completed relocation participant has a different normalization fence: "
                    + participant.authorityKey()));
        }
        ZLinkMeshNodeDescriptorKey descriptor = snapshot.allocation()
            .descriptor();
        ZLinkMeshNodeDescriptorKey selected = targetDescriptor.get();
        if (selected == null) {
            targetDescriptor.set(descriptor);
            targetDescriptorGeneration.set(
                snapshot.allocation().descriptorLifecycleGeneration());
        } else if (!selected.equals(descriptor)
            || targetDescriptorGeneration.get()
                != snapshot.allocation().descriptorLifecycleGeneration()) {
            return failed(new RelocationDataLostException(
                "completed relocation participants have different target descriptors"));
        }
        steady.add(new Participant(
            participant.authorityKey(),
            snapshot.allocation().objectKind(),
            snapshot.objectGeneration(),
            snapshot.authorityOwnerGeneration(),
            snapshot.storeVersion(),
            ZLinkAuthorityGenerationTransition.PRESERVE,
            publication.applicationPayload(),
            new byte[0]));
        return CompletableFuture.completedFuture(null);
    }

    private static boolean matchesCompletedParticipant(
        ZLinkAuthoritySnapshot snapshot,
        ZLinkCanonicalRelocationAuthorityStateCodec.Published publication,
        ExpectedParticipant participant,
        ZLinkAggregateFence activatedFence,
        long completedGeneration,
        ZLinkLocationOwnerToken targetOwner) {
        return publication != null
            && publication.sourceCleanupCompleted()
            && snapshot.objectGeneration() == participant.objectGeneration()
            && ownerGenerationAdvanced(
                participant.sourceAuthorityOwnerGeneration(),
                snapshot.authorityOwnerGeneration())
            && snapshot.ownerId().equals(targetOwner.ownerId())
            && snapshot.ownerLeaseGeneration() == targetOwner.leaseGeneration()
            && publication.aggregateId().equals(activatedFence.aggregateId())
            && publication.targetOwnerId().equals(targetOwner.ownerId())
            && publication.targetOwnerLeaseGeneration()
                == targetOwner.leaseGeneration();
    }

    private static boolean matchesSteadyOwner(
        ZLinkAuthoritySnapshot snapshot,
        ExpectedParticipant participant,
        ZLinkLocationOwnerToken targetOwner) {
        return snapshot.objectGeneration() == participant.objectGeneration()
            && ownerGenerationAdvanced(
                participant.sourceAuthorityOwnerGeneration(),
                snapshot.authorityOwnerGeneration())
            && snapshot.ownerId().equals(targetOwner.ownerId())
            && snapshot.ownerLeaseGeneration() == targetOwner.leaseGeneration();
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

    public record CanonicalProgress(
        ZLinkServiceRelocationEnvelopeCodec.Envelope root,
        List<String> participantStoreVersions,
        List<Long> participantOwnerGenerations) {
        public CanonicalProgress {
            Objects.requireNonNull(root, "root");
            participantStoreVersions = List.copyOf(
                Objects.requireNonNull(
                    participantStoreVersions,
                    "participantStoreVersions"));
            participantOwnerGenerations = List.copyOf(Objects.requireNonNull(
                participantOwnerGenerations,
                "participantOwnerGenerations"));
            if (participantStoreVersions.isEmpty()) {
                throw new IllegalArgumentException(
                    "canonical progress requires participant versions");
            }
            if (participantOwnerGenerations.size()
                != participantStoreVersions.size()
                || participantOwnerGenerations.stream().anyMatch(
                    value -> value == null || value <= 0)) {
                throw new IllegalArgumentException(
                    "canonical progress owner generations are invalid");
            }
        }

        public String storeVersion(long participantId) {
            if (participantId == 0
                || Long.compareUnsigned(
                    participantId,
                    Integer.toUnsignedLong(participantStoreVersions.size())) > 0) {
                throw new IllegalArgumentException(
                    "participantId is outside the canonical inventory");
            }
            return participantStoreVersions.get((int) participantId - 1);
        }

        public long ownerGeneration(long participantId) {
            if (participantId == 0
                || Long.compareUnsigned(
                    participantId,
                    Integer.toUnsignedLong(
                        participantOwnerGenerations.size())) > 0) {
                throw new IllegalArgumentException(
                    "participantId is outside the canonical inventory");
            }
            return participantOwnerGenerations.get((int) participantId - 1);
        }
    }

    public record StagedRoot(
        Request request,
        ZLinkRelocationStored stored,
        byte[] inventoryDigest) {
        public StagedRoot {
            Objects.requireNonNull(request, "request");
            Objects.requireNonNull(stored, "stored");
            inventoryDigest = Objects.requireNonNull(
                inventoryDigest, "inventoryDigest").clone();
        }

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
        var sharedPublication = new java.util.concurrent.atomic.AtomicReference<
            ZLinkCanonicalRelocationAuthorityStateCodec.Published>();
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
                    ZLinkCanonicalRelocationAuthorityStateCodec.Published current =
                        ZLinkCanonicalRelocationAuthorityStateCodec.decode(
                            snapshot.payload());
                    var first = sharedPublication.get();
                    if (current == null
                        || !current.aggregateId().equals(
                            published.fence().aggregateId())
                        || !current.targetOwnerId().equals(
                            published.request().targetOwner().ownerId())
                        || current.targetOwnerLeaseGeneration()
                            != published.request().targetOwner()
                                .leaseGeneration()
                        || first != null
                            && (!current.reference().equals(first.reference())
                                || current.checksumCrc32c()
                                    != first.checksumCrc32c()
                                || current.aggregateGeneration()
                                    != first.aggregateGeneration())) {
                        return failed(new RelocationDataLostException(
                            "relocation authority has a different completion fence: "
                                + original.authorityKey()));
                    }
                    sharedPublication.compareAndSet(null, current);
                    completedParticipants.add(new Participant(
                        original.authorityKey(),
                        original.objectKind(),
                        snapshot.objectGeneration(),
                        snapshot.authorityOwnerGeneration(),
                        snapshot.storeVersion(),
                        ZLinkAuthorityGenerationTransition.PRESERVE,
                        snapshot.payload(),
                        new byte[0]));
                    return CompletableFuture.completedFuture(null);
                }));
        }
        return reads.thenCompose(ignored -> {
            var current = sharedPublication.get();
            boolean advanced = !current.reference().equals(
                    published.stored().reference())
                || current.checksumCrc32c()
                    != published.stored().checksumCrc32c();
            CompletionStage<byte[]> root = advanced
                ? readRoot(
                        current.reference(),
                        current.checksumCrc32c(),
                        cancellation)
                    .thenApply(Root::payload)
                : CompletableFuture.completedFuture(completedRoot.clone());
            return root.thenCompose(authoritativeRoot -> {
                long generation = advanced
                    ? canonicalMutationGeneration(authoritativeRoot, true)
                    : Math.incrementExact(
                        published.request().aggregateGeneration());
                Request completion = new Request(
                    published.request().aggregateId(),
                    generation,
                    completedParticipants,
                    authoritativeRoot,
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
                            current.reference())
                        .thenApply(deleted -> completed));
            });
        });
    }

    private CompletionStage<Prepared> prepareAuthority(
        Request request,
        byte[] digest,
        ZLinkRelocationStored stored,
        ZLinkStoreCancellation cancellation) {
        return prepareAuthority(
            request, digest, stored, cancellation, null);
    }

    private CompletionStage<Prepared> prepareAuthority(
        Request request,
        byte[] digest,
        ZLinkRelocationStored stored,
        ZLinkStoreCancellation cancellation,
        Boolean sourceCleanupOverride) {
        List<ZLinkAggregateParticipant> mutations = new ArrayList<>();
        boolean sourceCleanupCompleted = sourceCleanupOverride != null
            ? sourceCleanupOverride
            : request.participants().stream()
                .allMatch(value -> value.ownerTransition()
                    == ZLinkAuthorityGenerationTransition.PRESERVE)
                && request.capacityBundle().actorSlots() == 0
                && request.capacityBundle().spotSlots() == 0;
        for (Participant participant : canonical(request.participants())) {
            byte[] authorityPayload =
                ZLinkCanonicalRelocationAuthorityStateCodec.publish(
                    participant.applicationAuthorityPayload(),
                    request,
                    participant.ownerTransition(),
                    stored,
                    sourceCleanupCompleted);
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
                    return readPublishedOwnerGenerations(prepared, cancellation)
                        .thenApply(generations -> new Published(
                            prepared.fence(),
                            prepared.stored(),
                            prepared.request(),
                            prepared.inventoryDigest(),
                            generations));
                }
                Throwable original = attempt.failure() == null
                    ? new AuthorityConflictException(attempt.result())
                    : unwrap(attempt.failure());
                return isPublished(prepared).thenCompose(published -> published
                    ? readPublishedOwnerGenerations(prepared, cancellation)
                        .thenApply(generations -> new Published(
                            prepared.fence(),
                            prepared.stored(),
                            prepared.request(),
                            prepared.inventoryDigest(),
                            generations))
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
                        ZLinkCanonicalRelocationAuthorityStateCodec.Published payload =
                            ZLinkCanonicalRelocationAuthorityStateCodec.decode(
                                snapshot.payload());
                        return payload != null
                            && payload.reference().equals(
                                prepared.stored().reference())
                            && payload.checksumCrc32c()
                                == prepared.stored().checksumCrc32c()
                            && payload.aggregateId().equals(
                                prepared.fence().aggregateId())
                            && payload.aggregateGeneration()
                                == prepared.fence().aggregateGeneration();
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
        byte[] inventoryDigest,
        Map<String, Long> targetOwnerGenerations) {
        public Published {
            Objects.requireNonNull(request, "request");
            inventoryDigest = inventoryDigest.clone();
            targetOwnerGenerations = immutableOwnerGenerations(
                targetOwnerGenerations);
            if (!targetOwnerGenerations.keySet().equals(
                request.participants().stream()
                    .map(Participant::authorityKey)
                    .collect(java.util.stream.Collectors.toSet()))) {
                throw new IllegalArgumentException(
                    "published owner generations do not match participants");
            }
        }

        public long targetOwnerGeneration(String authorityKey) {
            return ownerGeneration(targetOwnerGenerations, authorityKey);
        }

        @Override
        public byte[] inventoryDigest() {
            return inventoryDigest.clone();
        }
    }

    /** Reads provider-issued owner generations after a target commit. */
    public CompletionStage<Map<String, Long>> readTargetOwnerGenerations(
        List<ExpectedParticipant> participants,
        ZLinkAggregateFence fence,
        ZLinkLocationOwnerToken targetOwner,
        ZLinkStoreCancellation cancellation) {
        List<ExpectedParticipant> expected = List.copyOf(
            Objects.requireNonNull(participants, "participants"));
        Objects.requireNonNull(fence, "fence");
        Objects.requireNonNull(targetOwner, "targetOwner");
        Objects.requireNonNull(cancellation, "cancellation");
        if (expected.isEmpty()) {
            throw new IllegalArgumentException(
                "target owner generations require participants");
        }
        Map<String, Long> generations = new LinkedHashMap<>();
        CompletionStage<Void> reads = CompletableFuture.completedFuture(null);
        for (ExpectedParticipant participant : expected) {
            reads = reads.thenCompose(ignored -> authorityStore.read(
                    participant.authorityKey(), cancellation)
                .thenCompose(read -> {
                    var publication = read instanceof ZLinkAuthoritySnapshot snapshot
                        ? ZLinkCanonicalRelocationAuthorityStateCodec.decode(
                            snapshot.payload())
                        : null;
                    if (!(read instanceof ZLinkAuthoritySnapshot snapshot)
                        || snapshot.objectGeneration()
                            != participant.objectGeneration()
                        || !ownerGenerationAdvanced(
                            participant.sourceAuthorityOwnerGeneration(),
                            snapshot.authorityOwnerGeneration())
                        || !snapshot.ownerId().equals(targetOwner.ownerId())
                        || snapshot.ownerLeaseGeneration()
                            != targetOwner.leaseGeneration()
                        || publication == null
                        || !publication.aggregateId().equals(fence.aggregateId())
                        || publication.aggregateGeneration()
                            != fence.aggregateGeneration()
                        || !publication.targetOwnerId().equals(
                            targetOwner.ownerId())
                        || publication.targetOwnerLeaseGeneration()
                            != targetOwner.leaseGeneration()
                        || !publication.targetNodeRid().equals(
                            snapshot.allocation().descriptor().rid())
                        || publication.targetNodeGeneration()
                            != snapshot.allocation()
                                .descriptorLifecycleGeneration()) {
                        return failed(new RelocationDataLostException(
                            "target owner generation differs: "
                                + participant.authorityKey()));
                    }
                    generations.put(
                        participant.authorityKey(),
                        snapshot.authorityOwnerGeneration());
                    return CompletableFuture.completedFuture(null);
                }));
        }
        return reads.thenApply(ignored -> Map.copyOf(generations));
    }

    private CompletionStage<Map<String, Long>> readPublishedOwnerGenerations(
        Prepared prepared,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(cancellation, "cancellation");
        Map<String, Long> generations = new LinkedHashMap<>();
        CompletionStage<Void> reads = CompletableFuture.completedFuture(null);
        for (Participant participant : prepared.request().participants()) {
            reads = reads.thenCompose(ignored -> authorityStore.read(
                    participant.authorityKey(), cancellation)
                .thenCompose(read -> {
                    if (!(read instanceof ZLinkAuthoritySnapshot snapshot)
                        || snapshot.objectGeneration()
                            != participant.objectGeneration()
                        || !ownerGenerationMatches(
                            participant, snapshot.authorityOwnerGeneration())
                        || !snapshot.ownerId().equals(
                            prepared.request().targetOwner().ownerId())
                        || snapshot.ownerLeaseGeneration()
                            != prepared.request().targetOwner()
                                .leaseGeneration()) {
                        return failed(new RelocationDataLostException(
                            "committed relocation owner generation differs: "
                                + participant.authorityKey()));
                    }
                    var payload = ZLinkCanonicalRelocationAuthorityStateCodec
                        .decode(snapshot.payload());
                    if (payload == null
                        || !payload.reference().equals(
                            prepared.stored().reference())
                        || payload.checksumCrc32c()
                            != prepared.stored().checksumCrc32c()
                        || !payload.aggregateId().equals(
                            prepared.fence().aggregateId())
                        || payload.aggregateGeneration()
                            != prepared.fence().aggregateGeneration()
                        || !payload.targetOwnerId().equals(
                            prepared.request().targetOwner().ownerId())
                        || payload.targetOwnerLeaseGeneration()
                            != prepared.request().targetOwner()
                                .leaseGeneration()) {
                        return failed(new RelocationDataLostException(
                            "committed relocation publication differs: "
                                + participant.authorityKey()));
                    }
                    generations.put(
                        participant.authorityKey(),
                        snapshot.authorityOwnerGeneration());
                    return CompletableFuture.completedFuture(null);
                }));
        }
        return reads.thenApply(ignored -> Map.copyOf(generations));
    }

    private static boolean ownerGenerationMatches(
        Participant participant,
        long actualGeneration) {
        return participant.ownerTransition()
                == ZLinkAuthorityGenerationTransition.PRESERVE
            ? actualGeneration == participant.authorityOwnerGeneration()
            : ownerGenerationAdvanced(
                participant.authorityOwnerGeneration(), actualGeneration);
    }

    private static boolean ownerGenerationAdvanced(
        long sourceGeneration,
        long targetGeneration) {
        return sourceGeneration != Long.MAX_VALUE
            && targetGeneration > sourceGeneration;
    }

    private static Map<String, Long> immutableOwnerGenerations(
        Map<String, Long> generations) {
        Objects.requireNonNull(generations, "targetOwnerGenerations");
        if (generations.isEmpty()
            || generations.entrySet().stream().anyMatch(entry ->
                entry.getKey() == null || entry.getKey().isBlank()
                    || entry.getValue() == null || entry.getValue() <= 0)) {
            throw new IllegalArgumentException(
                "target owner generations are invalid");
        }
        return Map.copyOf(generations);
    }

    private static long ownerGeneration(
        Map<String, Long> generations,
        String authorityKey) {
        Long generation = generations.get(
            Objects.requireNonNull(authorityKey, "authorityKey"));
        if (generation == null) {
            throw new IllegalArgumentException(
                "authority key is absent from owner generations: "
                    + authorityKey);
        }
        return generation;
    }

    public static final class AuthorityConflictException
        extends RuntimeException {
        private final Object result;

        public AuthorityConflictException(Object result) {
            super("aggregate relocation authority operation was rejected: "
                + (result == null ? "null" : result.getClass().getSimpleName()));
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
