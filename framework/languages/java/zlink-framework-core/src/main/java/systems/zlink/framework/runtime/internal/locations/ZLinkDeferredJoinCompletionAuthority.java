package systems.zlink.framework.runtime.internal.locations;

import java.time.Duration;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkActorJoinOperationId;
import systems.zlink.framework.locations.*;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.locations.ZLinkAuthorityKeyCodec;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;

/**
 * Stores deferred Actor Join completion progress inside the Actor's canonical
 * relocation root. Every cursor update writes an immutable successor before
 * the Location authority CAS, then removes the root no longer referenced.
 */
public final class ZLinkDeferredJoinCompletionAuthority {
    private static final Duration RETENTION = Duration.ofHours(24);
    private static final ZLinkStoreCancellation NEVER = () -> false;
    private static final String PACKET = "ZLinkActorJoinAccepted";
    private static final String CONTENT_TYPE = "application/octet-stream";

    private final ZLinkLocationStore authority;
    private final ZLinkRelocationStore relocation;

    public ZLinkDeferredJoinCompletionAuthority(
        ZLinkLocationStore authority,
        ZLinkRelocationStore relocation) {
        this.authority = Objects.requireNonNull(authority, "authority");
        this.relocation = Objects.requireNonNull(relocation, "relocation");
    }

    public CompletionStage<Published> prepare(
        ZLinkActorJoinOperationId operationId,
        ZLinkBackendActorRef actor,
        byte[] rawReply) {
        return read(actor.actorId()).thenCompose(current -> {
            validateActor(current.root(), actor);
            var existing = find(
                current.root().terminalCompletions(), operationId);
            if (existing != null) {
                validateCompletion(existing, actor, rawReply);
                return CompletableFuture.completedFuture(
                    published(current, existing));
            }
            long participantId = onlyParticipant(current.root());
            long sequence = current.root().terminalCompletions().stream()
                .filter(value -> value.participantId() == participantId)
                .mapToLong(
                    ZLinkServiceRelocationEnvelopeCodec.Completion::sequence)
                .reduce(0L, (left, right) ->
                    Long.compareUnsigned(left, right) >= 0 ? left : right);
            if (sequence == -1L) {
                return CompletableFuture.failedFuture(
                    new CanonicalRootUnavailableException(
                        "deferred Join completion sequence is exhausted"));
            }
            var completion = new ZLinkServiceRelocationEnvelopeCodec.Completion(
                operationId.high(),
                operationId.low(),
                current.publication().sourceOwnerId(),
                current.publication().sourceOwnerLeaseGeneration(),
                current.publication().sourceNodeRid().toString(),
                current.publication().sourceNodeGeneration(),
                participantId,
                sequence + 1,
                0,
                0,
                1,
                new ZLinkServiceRelocationEnvelopeCodec.Payload(
                    PACKET,
                    CONTENT_TYPE,
                    rawReply == null ? new byte[0] : rawReply));
            var successor =
                ZLinkServiceRelocationEnvelopeCodec.putTerminalCompletion(
                    current.root(), completion);
            return publish(current, successor)
                .thenApply(next -> published(next, completion));
        });
    }

    public CompletionStage<Published> advance(
        Published expected,
        ZLinkBackendActorRef actor,
        int cursor) {
        if (cursor < 1 || cursor > 3
            || cursor > expected.cursor() + 1) {
            return CompletableFuture.failedFuture(
                new IllegalArgumentException(
                    "invalid deferred Join completion cursor"));
        }
        return read(actor.actorId()).thenCompose(current -> {
            validateActor(current.root(), actor);
            var completion = find(
                current.root().terminalCompletions(),
                expected.operationId());
            if (completion == null) {
                return CompletableFuture.failedFuture(
                    new CanonicalRootUnavailableException(
                        "Actor authority no longer references the deferred Join completion"));
            }
            validateCompletion(
                completion, actor, expected.rawReply());
            if (completion.deliveryState() >= cursor) {
                return CompletableFuture.completedFuture(
                    published(current, completion));
            }
            var updated =
                new ZLinkServiceRelocationEnvelopeCodec.Completion(
                    completion.operationHigh(),
                    completion.operationLow(),
                    completion.sourceOwnerId(),
                    completion.sourceOwnerLeaseGeneration(),
                    completion.sourceNodeRid(),
                    completion.sourceNodeGeneration(),
                    completion.participantId(),
                    completion.sequence(),
                    completion.terminalResult(),
                    completion.failureCode(),
                    cursor,
                    completion.payload());
            var successor =
                ZLinkServiceRelocationEnvelopeCodec.putTerminalCompletion(
                    current.root(), updated);
            return publish(current, successor)
                .thenApply(next -> published(next, updated));
        });
    }

    public CompletionStage<Void> release(
        Published delivered,
        ZLinkBackendActorRef actor) {
        if (delivered.cursor() != 3) {
            return CompletableFuture.failedFuture(
                new IllegalArgumentException(
                    "deferred Join completion must be Delivered before release"));
        }
        return authority.read(
                ZLinkAuthorityKeyCodec.actor(actor.actorId()),
                NEVER)
            .thenCompose(result -> {
            if (!(result instanceof ZLinkAuthoritySnapshot snapshot)) {
                return CompletableFuture.completedFuture(null);
            }
            var publication =
                ZLinkCanonicalRelocationAuthorityStateCodec.decode(
                    snapshot.payload());
            if (publication == null) {
                return CompletableFuture.completedFuture(null);
            }
            return load(
                    publication.reference(),
                    publication.checksumCrc32c())
                .thenCompose(loaded -> release(
                    delivered,
                    actor,
                    new Current(
                        ZLinkAuthorityKeyCodec.actor(actor.actorId()),
                        snapshot,
                        publication,
                        loaded.root(),
                        loaded.inventoryDigest())));
        });
    }

    private CompletionStage<Void> release(
        Published delivered,
        ZLinkBackendActorRef actor,
        Current current) {
            validateActor(current.root(), actor);
            var completion = find(
                current.root().terminalCompletions(),
                delivered.operationId());
            if (completion == null) {
                return CompletableFuture.completedFuture(null);
            }
            if (completion.deliveryState() != 3) {
                return CompletableFuture.failedFuture(
                    new IllegalStateException(
                        "Actor authority completion is not Delivered"));
            }
            if (!current.publication().sourceCleanupCompleted()) {
                return CompletableFuture.failedFuture(
                    new IllegalStateException(
                        "deferred Join completion cannot be released before source cleanup"));
            }
            return authority.compareExchange(
                    current.authorityKey(),
                    new ZLinkAuthorityExpectFound(
                        current.snapshot().storeVersion()),
                    new ZLinkAuthorityPut(
                        current.publication().applicationPayload(),
                        ZLinkAuthorityGenerationTransition.PRESERVE,
                        Optional.empty(),
                        Optional.empty()),
                    NEVER)
                .thenCompose(result ->
                    result instanceof ZLinkAuthorityStored
                        ? ZLinkRelocationTreeStore.delete(
                                relocation,
                                current.publication().reference(),
                                NEVER)
                        : CompletableFuture.failedFuture(
                            new IllegalStateException(
                                "deferred Join completion release CAS conflicted")));
    }

    public CompletionStage<Void> completeSourceCleanupAndRelease(
        Published delivered,
        ZLinkBackendActorRef actor) {
        return read(actor.actorId()).thenCompose(current -> {
            var completion = find(
                current.root().terminalCompletions(),
                delivered.operationId());
            if (completion == null || completion.deliveryState() != 3) {
                return CompletableFuture.failedFuture(
                    new IllegalStateException(
                        "deferred Join completion is not durably Delivered"));
            }
            var stored = new ZLinkRelocationStored(
                current.publication().reference(),
                current.publication().checksumCrc32c(),
                current.snapshot().storeNow(),
                current.snapshot().storeNow());
            byte[] completed =
                ZLinkCanonicalRelocationAuthorityStateCodec
                    .completeSourceCleanup(
                        current.snapshot().payload(),
                        stored,
                        current.root());
            return authority.compareExchange(
                    current.authorityKey(),
                    new ZLinkAuthorityExpectFound(
                        current.snapshot().storeVersion()),
                    new ZLinkAuthorityPut(
                        completed,
                        ZLinkAuthorityGenerationTransition.PRESERVE,
                        Optional.empty(),
                        Optional.empty()),
                    NEVER)
                .thenCompose(result -> {
                    if (!(result instanceof ZLinkAuthorityStored)) {
                        return CompletableFuture.failedFuture(
                            new IllegalStateException(
                                "deferred Join source-cleanup CAS conflicted"));
                    }
                    return release(delivered, actor);
                });
        });
    }

    public CompletionStage<Published> restore(
        String reference,
        long checksumCrc32c,
        ZLinkActorJoinOperationId operationId,
        ZLinkBackendActorRef actor) {
        return load(reference, checksumCrc32c).thenApply(loaded -> {
            var root = loaded.root();
            if (root.object().kind() != 1
                || !root.object().objectId().equals(actor.actorId())
                || root.object().objectGeneration() != actor.generation()) {
                throw new IllegalStateException(
                    "deferred Join manifest has a stale Actor generation");
            }
            var completion = find(root.terminalCompletions(), operationId);
            if (completion == null) {
                throw new IllegalStateException(
                    "deferred Join manifest does not contain the operation");
            }
            return new Published(
                reference,
                checksumCrc32c,
                completion.deliveryState(),
                new ZLinkActorJoinOperationId(
                    completion.operationHigh(),
                    completion.operationLow()),
                root.object().objectId(),
                root.object().objectGeneration(),
                completion.payload() == null
                    ? new byte[0]
                    : completion.payload().bytes());
        });
    }

    public CompletionStage<Published> recover(
        ZLinkActorJoinOperationId operationId,
        ZLinkBackendActorRef actor) {
        return read(actor.actorId()).thenApply(current -> {
            validateActor(current.root(), actor);
            var completion = find(
                current.root().terminalCompletions(), operationId);
            if (completion == null) {
                throw new IllegalStateException(
                    "Actor authority no longer references the deferred Join completion");
            }
            return published(current, completion);
        });
    }

    public CompletionStage<Published> recoverSuccessor(
        String staleReference,
        ZLinkActorJoinOperationId operationId,
        ZLinkBackendActorRef actor) {
        return read(actor.actorId()).thenApply(current -> {
            if (current.publication().reference().equals(staleReference)) {
                throw new IllegalStateException(
                    "published deferred Join root is missing");
            }
            validateActor(current.root(), actor);
            var completion = find(
                current.root().terminalCompletions(), operationId);
            if (completion == null) {
                throw new IllegalStateException(
                    "successor root does not contain the deferred Join completion");
            }
            return published(current, completion);
        });
    }

    private CompletionStage<Current> read(String actorId) {
        String key = ZLinkAuthorityKeyCodec.actor(actorId);
        return authority.read(key, NEVER).thenCompose(result -> {
            if (!(result instanceof ZLinkAuthoritySnapshot snapshot)) {
                return CompletableFuture.failedFuture(
                    new IllegalStateException(
                        "Actor authority is missing for deferred Join completion"));
            }
            var publication =
                ZLinkCanonicalRelocationAuthorityStateCodec.decode(
                    snapshot.payload());
            if (publication == null) {
                return CompletableFuture.failedFuture(
                    new IllegalStateException(
                        "Actor authority has no canonical relocation root"));
            }
            return load(
                    publication.reference(),
                    publication.checksumCrc32c())
                .thenApply(loaded ->
                    new Current(
                        key,
                        snapshot,
                        publication,
                        loaded.root(),
                        loaded.inventoryDigest()));
        });
    }

    private CompletionStage<Current> publish(
        Current current,
        ZLinkServiceRelocationEnvelopeCodec.Envelope successor) {
        byte[] root = ZLinkServiceRelocationEnvelopeCodec.encodeSuccessor(
            successor,
            successor.participantProgress(),
            successor.terminalCompletions());
        return ZLinkRelocationTreeStore.put(
                relocation,
                root,
                current.inventoryDigest(),
                RETENTION,
                NEVER)
            .thenCompose(stored -> authority.compareExchange(
                current.authorityKey(),
                new ZLinkAuthorityExpectFound(
                    current.snapshot().storeVersion()),
                new ZLinkAuthorityPut(
                    ZLinkCanonicalRelocationAuthorityStateCodec.replaceRoot(
                        current.snapshot().payload(),
                        stored.root(),
                        successor),
                    ZLinkAuthorityGenerationTransition.PRESERVE,
                    Optional.empty(),
                    Optional.empty()),
                NEVER)
                .thenCompose(result -> {
                    if (!(result instanceof ZLinkAuthorityStored)) {
                        return ZLinkRelocationTreeStore.delete(
                                relocation,
                                stored.root().reference(),
                                NEVER)
                            .thenCompose(ignored ->
                                CompletableFuture.failedFuture(
                                    new IllegalStateException(
                                        "deferred Join authority CAS conflicted")));
                    }
                    return ZLinkRelocationTreeStore.delete(
                            relocation,
                            current.publication().reference(),
                            NEVER)
                        .thenCompose(ignored ->
                            read(current.root().object().objectId()));
                }));
    }

    private CompletionStage<
        Loaded> load(
            String reference,
            long checksumCrc32c) {
        return relocation.get(reference, NEVER).thenCompose(result -> {
            if (result instanceof ZLinkRelocationMissing) {
                return CompletableFuture.failedFuture(
                    new CanonicalRootMissingException(
                        "deferred Join canonical relocation root is missing"));
            }
            if (!(result instanceof ZLinkRelocationFound found)
                || crc32c(found.payload()) != checksumCrc32c) {
                return CompletableFuture.failedFuture(
                    new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.RELOCATION_DATA_LOST,
                        "deferred Join canonical relocation root checksum is invalid"));
            }
            return ZLinkRelocationTreeStore.read(
                    relocation,
                    reference,
                    checksumCrc32c,
                    NEVER)
                .thenApply(read -> new Loaded(
                    ZLinkServiceRelocationEnvelopeCodec.decode(
                        read.logicalRoot()),
                    read.inventoryDigest()));
        });
    }

    private static Published published(
        Current current,
        ZLinkServiceRelocationEnvelopeCodec.Completion completion) {
        return new Published(
            current.publication().reference(),
            current.publication().checksumCrc32c(),
            completion.deliveryState(),
            new ZLinkActorJoinOperationId(
                completion.operationHigh(),
                completion.operationLow()),
            current.root().object().objectId(),
            current.root().object().objectGeneration(),
            completion.payload() == null
                ? new byte[0]
                : completion.payload().bytes());
    }

    private static void validateActor(
        ZLinkServiceRelocationEnvelopeCodec.Envelope root,
        ZLinkBackendActorRef actor) {
        if (root.object().kind() != 1
            || !root.object().objectId().equals(actor.actorId())
            || root.object().objectGeneration() != actor.generation()) {
            throw new IllegalStateException(
                "deferred Join Actor generation fence is stale");
        }
    }

    private static long onlyParticipant(
        ZLinkServiceRelocationEnvelopeCodec.Envelope root) {
        // Deferred Join moves one Actor independently. A User Spot aggregate
        // has object kind USER_SPOT and is rejected by validateActor before
        // this method; its sealed Actor lanes cannot start a concurrent Join.
        if (root.applicationStates().size() != 1
            || root.participantProgress().size() != 1) {
            throw new IllegalStateException(
                "deferred Join completion requires a standalone Actor root");
        }
        return root.participantProgress().getFirst().participantId();
    }

    private static ZLinkServiceRelocationEnvelopeCodec.Completion find(
        List<ZLinkServiceRelocationEnvelopeCodec.Completion> completions,
        ZLinkActorJoinOperationId operationId) {
        return completions.stream()
            .filter(value ->
                value.operationHigh() == operationId.high()
                    && value.operationLow() == operationId.low())
            .findFirst()
            .orElse(null);
    }

    private static void validateCompletion(
        ZLinkServiceRelocationEnvelopeCodec.Completion completion,
        ZLinkBackendActorRef actor,
        byte[] rawReply) {
        if (completion.payload() == null
            || !PACKET.equals(completion.payload().packetName())
            || !CONTENT_TYPE.equals(completion.payload().contentType())
            || !Arrays.equals(
                completion.payload().bytes(),
                rawReply == null ? new byte[0] : rawReply)) {
            throw new IllegalStateException(
                "deferred Join completion conflicts with the published operation");
        }
    }

    private static long crc32c(byte[] payload) {
        java.util.zip.CRC32C checksum = new java.util.zip.CRC32C();
        checksum.update(payload, 0, payload.length);
        return checksum.getValue();
    }

    public record Published(
        String reference,
        long checksumCrc32c,
        int cursor,
        ZLinkActorJoinOperationId operationId,
        String actorId,
        long objectGeneration,
        byte[] rawReply) {
        public Published {
            rawReply = Objects.requireNonNull(rawReply, "rawReply").clone();
        }
        @Override public byte[] rawReply() { return rawReply.clone(); }
    }

    private record Current(
        String authorityKey,
        ZLinkAuthoritySnapshot snapshot,
        ZLinkCanonicalRelocationAuthorityStateCodec.Published publication,
        ZLinkServiceRelocationEnvelopeCodec.Envelope root,
        byte[] inventoryDigest) {
        Current {
            inventoryDigest = inventoryDigest.clone();
        }
        @Override public byte[] inventoryDigest() {
            return inventoryDigest.clone();
        }
    }

    private record Loaded(
        ZLinkServiceRelocationEnvelopeCodec.Envelope root,
        byte[] inventoryDigest) {
        Loaded {
            inventoryDigest = inventoryDigest.clone();
        }
        @Override public byte[] inventoryDigest() {
            return inventoryDigest.clone();
        }
    }

    public static final class CanonicalRootUnavailableException
        extends ZLinkFrameworkException {
        CanonicalRootUnavailableException(String message) {
            super(ZLinkFrameworkErrorKind.RELOCATION_DATA_LOST, message);
        }
    }

    public static final class CanonicalRootMissingException
        extends ZLinkFrameworkException {
        CanonicalRootMissingException(String message) {
            super(ZLinkFrameworkErrorKind.RELOCATION_DATA_LOST, message);
        }
    }
}
