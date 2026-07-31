package systems.zlink.framework.runtime.internal.locations;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

/**
 * Finds relocation roots already published by Location Store authority after
 * a runtime restart.
 */
public final class ZLinkRelocationStartupScanner {
    private static final String ACTOR_PREFIX = "zla1:a:";
    private static final String SPOT_PREFIX = "zla1:s:";
    private static final int PAGE_SIZE = 128;
    private static final int MAX_SCAN_RESTARTS = 8;

    private final ZLinkLocationRepository authorityStore;
    private final ZLinkAggregateRelocationCoordinator coordinator;

    public ZLinkRelocationStartupScanner(
        ZLinkLocationRepository authorityStore,
        ZLinkRelocationStore relocationStore) {
        this.authorityStore = Objects.requireNonNull(
            authorityStore, "authorityStore");
        this.coordinator = new ZLinkAggregateRelocationCoordinator(
            authorityStore,
            Objects.requireNonNull(relocationStore, "relocationStore"));
    }

    public CompletionStage<List<Candidate>> scan(
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(cancellation, "cancellation");
        return scanPrefix(ACTOR_PREFIX, cancellation)
            .thenCombine(
                scanPrefix(SPOT_PREFIX, cancellation),
                (actors, spots) -> {
                    var entries = new ArrayList<PublishedAuthority>(
                        actors.size() + spots.size());
                    entries.addAll(actors);
                    entries.addAll(spots);
                    return entries;
                })
            .thenCompose(entries -> verifyGroups(entries, cancellation));
    }

    private CompletionStage<List<PublishedAuthority>> scanPrefix(
        String prefix,
        ZLinkStoreCancellation cancellation) {
        return scanPage(
            prefix,
            Optional.empty(),
            new ArrayList<>(),
            0,
            cancellation);
    }

    private CompletionStage<List<PublishedAuthority>> scanPage(
        String prefix,
        Optional<ZLinkAuthorityScanCursor> cursor,
        List<PublishedAuthority> found,
        int restartCount,
        ZLinkStoreCancellation cancellation) {
        if (cancellation.isCancellationRequested()) {
            return CompletableFuture.failedFuture(
                new IllegalStateException("relocation startup scan cancelled"));
        }
        return authorityStore.list(prefix, cursor, PAGE_SIZE, cancellation)
            .thenCompose(result -> {
                if (result instanceof ZLinkAuthorityScanExpired) {
                    if (restartCount >= MAX_SCAN_RESTARTS) {
                        return CompletableFuture.failedFuture(
                            new ZLinkAggregateRelocationCoordinator
                                .RelocationDataLostException(
                                "relocation authority scan repeatedly expired: "
                                    + prefix));
                    }
                    return scanPage(
                        prefix,
                        Optional.empty(),
                        new ArrayList<>(),
                        restartCount + 1,
                        cancellation);
                }
                if (!(result instanceof ZLinkAuthorityPage page)) {
                    return CompletableFuture.failedFuture(
                        new ZLinkAggregateRelocationCoordinator
                            .RelocationDataLostException(
                            "relocation authority scan returned an unknown result"));
                }
                for (ZLinkAuthorityEntry entry : page.items()) {
                    var publication =
                        ZLinkCanonicalRelocationAuthorityStateCodec.decode(
                            entry.snapshot().payload());
                    if (publication != null) {
                        found.add(new PublishedAuthority(entry, publication));
                    }
                }
                return page.nextCursor().isPresent()
                    ? scanPage(
                        prefix,
                        page.nextCursor(),
                        found,
                        restartCount,
                        cancellation)
                    : CompletableFuture.completedFuture(List.copyOf(found));
            });
    }

    private CompletionStage<List<Candidate>> verifyGroups(
        List<PublishedAuthority> entries,
        ZLinkStoreCancellation cancellation) {
        Map<String, List<PublishedAuthority>> groups = new LinkedHashMap<>();
        entries.stream()
            .sorted(Comparator.comparing(value -> value.entry().key()))
            .forEach(value -> groups.computeIfAbsent(
                value.publication().reference(),
                ignored -> new ArrayList<>()).add(value));

        CompletionStage<List<Candidate>> result =
            CompletableFuture.completedFuture(new ArrayList<>());
        for (List<PublishedAuthority> group : groups.values()) {
            result = result.thenCompose(candidates ->
                verifyGroup(group, cancellation).thenApply(candidate -> {
                    candidates.add(candidate);
                    return candidates;
                }));
        }
        return result.thenApply(candidates -> candidates.stream()
            .sorted(Comparator.comparing(Candidate::reference))
            .toList());
    }

    private CompletionStage<Candidate> verifyGroup(
        List<PublishedAuthority> group,
        ZLinkStoreCancellation cancellation) {
        PublishedAuthority first = group.get(0);
        var publication = first.publication();
        var expected = new ArrayList<
            ZLinkAggregateRelocationCoordinator.ExpectedParticipant>(
                group.size());
        for (PublishedAuthority value : group) {
            var snapshot = value.entry().snapshot();
            var current = value.publication();
            if (!samePublication(publication, current)
                || !snapshot.ownerId().equals(current.targetOwnerId())
                || snapshot.ownerLeaseGeneration()
                    != current.targetOwnerLeaseGeneration()
                || snapshot.authorityOwnerGeneration() <= 1) {
                return CompletableFuture.failedFuture(
                    new ZLinkAggregateRelocationCoordinator
                        .RelocationDataLostException(
                        "published relocation authorities are inconsistent: "
                            + value.entry().key()));
            }
            expected.add(
                new ZLinkAggregateRelocationCoordinator.ExpectedParticipant(
                    value.entry().key(),
                    snapshot.objectGeneration(),
                    snapshot.authorityOwnerGeneration() - 1));
        }

        var fence = new ZLinkAggregateFence(
            publication.aggregateId(),
            publication.aggregateGeneration());
        var target = new ZLinkLocationOwnerToken(
            publication.targetOwnerId(),
            publication.targetOwnerLeaseGeneration());
        return coordinator.readRoot(
                publication.reference(),
                publication.checksumCrc32c(),
                cancellation)
            .thenCompose(root -> {
                var envelope = ZLinkServiceRelocationEnvelopeCodec.decode(
                    root.payload());
                if (envelope.relocationHigh()
                        != publication.aggregateId().getMostSignificantBits()
                    || envelope.relocationLow()
                        != publication.aggregateId().getLeastSignificantBits()
                    || envelope.participantProgress().size() != group.size()
                    || envelope.applicationStates().size() != group.size()) {
                    return CompletableFuture.failedFuture(
                        new ZLinkAggregateRelocationCoordinator
                            .RelocationDataLostException(
                            "published relocation root inventory is incomplete"));
                }
                return coordinator.readPublishedAggregate(
                        expected,
                        fence,
                        target,
                        root.inventoryDigest(),
                        cancellation)
                    .thenApply(verified -> new Candidate(
                        publication.reference(),
                        publication.checksumCrc32c(),
                        fence,
                        publication.sourceOwnerId(),
                        publication.sourceOwnerLeaseGeneration(),
                        publication.sourceNodeRid(),
                        publication.sourceNodeGeneration(),
                        target,
                        publication.targetNodeRid(),
                        publication.targetNodeGeneration(),
                        publication.sourceCleanupCompleted(),
                        verified,
                        group.stream()
                            .map(PublishedAuthority::entry)
                            .sorted(Comparator.comparing(
                                ZLinkAuthorityEntry::key))
                            .toList()));
            });
    }

    private static boolean samePublication(
        ZLinkCanonicalRelocationAuthorityStateCodec.Published left,
        ZLinkCanonicalRelocationAuthorityStateCodec.Published right) {
        return left.reference().equals(right.reference())
            && left.checksumCrc32c() == right.checksumCrc32c()
            && left.aggregateId().equals(right.aggregateId())
            && left.aggregateGeneration() == right.aggregateGeneration()
            && left.sourceOwnerId().equals(right.sourceOwnerId())
            && left.sourceOwnerLeaseGeneration()
                == right.sourceOwnerLeaseGeneration()
            && left.sourceNodeRid().equals(right.sourceNodeRid())
            && left.sourceNodeGeneration() == right.sourceNodeGeneration()
            && left.targetOwnerId().equals(right.targetOwnerId())
            && left.targetOwnerLeaseGeneration()
                == right.targetOwnerLeaseGeneration()
            && left.targetNodeRid().equals(right.targetNodeRid())
            && left.targetNodeGeneration() == right.targetNodeGeneration()
            && left.sourceCleanupCompleted()
                == right.sourceCleanupCompleted();
    }

    private record PublishedAuthority(
        ZLinkAuthorityEntry entry,
        ZLinkCanonicalRelocationAuthorityStateCodec.Published publication) {
    }

    public record Candidate(
        String reference,
        long checksumCrc32c,
        ZLinkAggregateFence fence,
        String sourceOwnerId,
        long sourceOwnerLeaseGeneration,
        systems.zlink.contracts.core.RoutingId sourceNodeRid,
        long sourceNodeGeneration,
        ZLinkLocationOwnerToken targetOwner,
        systems.zlink.contracts.core.RoutingId targetNodeRid,
        long targetNodeGeneration,
        boolean sourceCleanupCompleted,
        ZLinkAggregateRelocationCoordinator.PublishedRoot root,
        List<ZLinkAuthorityEntry> authorities) {
        public Candidate {
            Objects.requireNonNull(reference, "reference");
            Objects.requireNonNull(fence, "fence");
            Objects.requireNonNull(sourceOwnerId, "sourceOwnerId");
            Objects.requireNonNull(sourceNodeRid, "sourceNodeRid");
            Objects.requireNonNull(targetOwner, "targetOwner");
            Objects.requireNonNull(targetNodeRid, "targetNodeRid");
            Objects.requireNonNull(root, "root");
            authorities = List.copyOf(authorities);
            if (reference.isBlank()
                || authorities.isEmpty()
                || sourceOwnerId.isBlank()
                || sourceOwnerLeaseGeneration <= 0
                || sourceNodeGeneration <= 0
                || targetNodeGeneration <= 0) {
                throw new IllegalArgumentException(
                    "relocation recovery owner and node fences are invalid");
            }
        }
    }
}
