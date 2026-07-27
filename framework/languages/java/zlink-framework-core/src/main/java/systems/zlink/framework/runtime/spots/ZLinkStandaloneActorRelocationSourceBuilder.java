package systems.zlink.framework.runtime.spots;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ThreadLocalRandom;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkRelocationCancellation;
import systems.zlink.framework.actors.ZLinkRelocationPolicy;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.locations.*;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkAggregateRelocationCoordinator;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkRelocationPermitPool;
import systems.zlink.framework.runtime.internal.relocation
    .ZLinkRelocationAdapterRegistry;
import systems.zlink.framework.runtime.locations.ZLinkActorAuthorityPayloadCodec;
import systems.zlink.framework.runtime.locations.ZLinkAuthorityKeyCodec;
import systems.zlink.framework.runtime.mesh.MeshNodeRegistration;

/**
 * Builds the reversible source half of one Entry Spot Actor relocation from
 * the live Actor and its exact authority row.
 */
final class ZLinkStandaloneActorRelocationSourceBuilder {
    private static final int PAGE_SIZE = 1000;
    private static final int MAX_DESCRIPTORS = 65_536;
    private static final long SNAPSHOT_RESERVATION_BYTES = 64L * 1024 * 1024;
    private static final long ENVELOPE_RESERVATION_BYTES = 64L * 1024;

    private final String meshName;
    private final RoutingId localNodeRid;
    private final long localNodeGeneration;
    private final ZLinkLocationStore locations;
    private final ZLinkAggregateRelocationCoordinator coordinator;
    private final ZLinkRelocationPermitPool permits;
    private final ZLinkActorSessionCoordinator actors;
    private final ZLinkRelocationAdapterRegistry adapters;
    private final Map<String, MeshNodeRegistration.RelocatableActorFactory<?>>
        factories;
    private final ZLinkActorAuthorityPayloadCodec authorities =
        new ZLinkActorAuthorityPayloadCodec();

    ZLinkStandaloneActorRelocationSourceBuilder(
        String meshName,
        RoutingId localNodeRid,
        long localNodeGeneration,
        ZLinkLocationStore locations,
        ZLinkAggregateRelocationCoordinator coordinator,
        ZLinkRelocationPermitPool permits,
        ZLinkActorSessionCoordinator actors,
        ZLinkRelocationAdapterRegistry adapters,
        Map<String, MeshNodeRegistration.RelocatableActorFactory<?>>
            factories) {
        this.meshName = requireText(meshName, "meshName");
        this.localNodeRid = Objects.requireNonNull(
            localNodeRid, "localNodeRid");
        if (localNodeGeneration <= 0) {
            throw new IllegalArgumentException(
                "local node generation must be positive");
        }
        this.localNodeGeneration = localNodeGeneration;
        this.locations = Objects.requireNonNull(locations, "locations");
        this.coordinator = Objects.requireNonNull(coordinator, "coordinator");
        this.permits = Objects.requireNonNull(permits, "permits");
        this.actors = Objects.requireNonNull(actors, "actors");
        this.adapters = Objects.requireNonNull(adapters, "adapters");
        this.factories = Map.copyOf(
            Objects.requireNonNull(factories, "factories"));
    }

    CompletionStage<PreparedSource> prepare(
        String actorId,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(cancellation, "cancellation");
        if (cancellation.isCancellationRequested()) {
            return cancelled();
        }
        ZLinkActor actor = actors.localActor(actorId).orElse(null);
        if (actor == null) {
            return failed(new IllegalStateException(
                "Actor is not active locally: " + actorId));
        }
        return readOwned(actorId, cancellation)
            .thenCompose(owned -> listDescriptors(cancellation)
                .thenApply(descriptors -> new Admission(
                    owned,
                    selectTarget(owned, descriptors))))
            .thenCompose(admission -> sealAndCapture(
                actor, admission, cancellation));
    }

    private CompletionStage<PreparedSource> sealAndCapture(
        ZLinkActor actor,
        Admission admission,
        ZLinkStoreCancellation cancellation) {
        ZLinkRelocationPermitPool.Lease permit = permits.tryAcquire(
            ZLinkRelocationPermitPool.Request.outboundAggregate(
                admission.owned().snapshotPolicy()
                    ? SNAPSHOT_RESERVATION_BYTES
                    : ENVELOPE_RESERVATION_BYTES,
                admission.owned().snapshotPolicy() ? 1 : 0,
                false));
        if (permit == null) {
            return failed(new IllegalStateException(
                "Actor relocation permit is unavailable"));
        }
        Optional<ZLinkAsyncSerialQueue.RelocationSeal> sealed =
            actors.trySealActorRelocation(admission.owned().actorId());
        if (sealed.isEmpty()) {
            permit.close();
            return failed(new IllegalStateException(
                "Actor relocation queue cannot be sealed"));
        }
        ZLinkAsyncSerialQueue.RelocationSeal seal = sealed.orElseThrow();
        ZLinkRelocationCancellation relocationCancellation =
            cancellation::isCancellationRequested;
        CompletionStage<byte[]> captured = admission.owned().snapshotPolicy()
            ? adapters.captureActor(
                admission.owned().stableType(),
                actor,
                relocationCancellation)
            : CompletableFuture.completedFuture(new byte[0]);
        return captured.thenCompose(state -> {
            byte[] applicationState = Objects.requireNonNull(
                state, "Actor Capture returned null").clone();
            UUID relocationId = UUID.randomUUID();
            byte[] initialRoot =
                ZLinkCanonicalActorRelocationEnvelope.encode(
                    relocationId,
                    admission.owned().actorId(),
                    admission.owned().snapshot().objectGeneration(),
                    admission.owned().snapshot()
                        .authorityOwnerGeneration(),
                    admission.owned().snapshotPolicy(),
                    applicationState,
                    seal.captured());
            var request = request(
                admission.owned(),
                admission.target(),
                relocationId,
                initialRoot);
            return coordinator.stageRoot(request, cancellation)
                .thenApply(staged -> new PreparedSource(
                    coordinator,
                    actors,
                    seal,
                    permit,
                    admission.owned(),
                    admission.target(),
                    relocationId,
                    applicationState,
                    staged));
        }).exceptionallyCompose(failure -> {
            actors.abortActorRelocation(
                admission.owned().actorId(), seal);
            permit.close();
            return failed(unwrap(failure));
        });
    }

    private CompletionStage<Owned> readOwned(
        String actorId,
        ZLinkStoreCancellation cancellation) {
        String key = ZLinkAuthorityKeyCodec.actor(actorId);
        return locations.read(key, cancellation).thenCompose(read -> {
            if (!(read instanceof ZLinkAuthoritySnapshot snapshot)
                || snapshot.allocation().state()
                    != ZLinkPlacementAllocationState.ACTIVE
                || snapshot.allocation().objectKind()
                    != ZLinkPlacementObjectKind.ACTOR
                || !snapshot.allocation().descriptor().meshName()
                    .equals(meshName)
                || !snapshot.allocation().descriptor().rid()
                    .equals(localNodeRid)
                || snapshot.allocation().descriptorLifecycleGeneration()
                    != localNodeGeneration) {
                return failed(new IllegalStateException(
                    "Actor authority is not Ready on the source: " + key));
            }
            var authority = authorities.decode(snapshot.payload())
                .orElseThrow(() -> new IllegalStateException(
                    "Actor authority payload is invalid"));
            String stableType = actors.actorType(actorId);
            var factory = factories.get(stableType);
            if (factory == null
                || factory.relocationPolicy()
                    instanceof ZLinkRelocationPolicy.Disabled<?>) {
                return failed(new IllegalStateException(
                    "Actor relocation policy is unavailable: " + stableType));
            }
            if (authority.state() != ZLinkActorAuthorityPayloadCodec.State.READY
                || authority.currentSpotKind() != 1
                || !authority.actorId().equals(actorId)
                || !authority.stableType().equals(stableType)
                || !authority.nodeRid().equals(localNodeRid)
                || authority.nodeGeneration() != localNodeGeneration
                || actors.actorRef(actorId).generation()
                    != snapshot.objectGeneration()) {
                return failed(new IllegalStateException(
                    "live Entry Spot Actor differs from Location authority"));
            }
            return CompletableFuture.completedFuture(new Owned(
                key,
                actorId,
                stableType,
                snapshot,
                factory.relocationPolicy()
                    instanceof ZLinkRelocationPolicy.Snapshot<?>));
        });
    }

    private CompletionStage<List<ZLinkMeshNodeDescriptor>> listDescriptors(
        ZLinkStoreCancellation cancellation) {
        return listDescriptorPage(null, new ArrayList<>(), cancellation);
    }

    private CompletionStage<List<ZLinkMeshNodeDescriptor>> listDescriptorPage(
        String cursor,
        List<ZLinkMeshNodeDescriptor> result,
        ZLinkStoreCancellation cancellation) {
        if (cancellation.isCancellationRequested()) {
            return cancelled();
        }
        return locations.listMeshNodes(
                meshName,
                new ZLinkPageRequest(PAGE_SIZE, cursor))
            .thenCompose(page -> {
                result.addAll(page.items());
                if (result.size() > MAX_DESCRIPTORS) {
                    return failed(new IllegalStateException(
                        "MeshNode descriptor inventory exceeds its bound"));
                }
                String next = page.continuationToken();
                if (next == null || next.isBlank()) {
                    return CompletableFuture.completedFuture(
                        List.copyOf(result));
                }
                if (next.equals(cursor)) {
                    return failed(new IllegalStateException(
                        "MeshNode descriptor cursor did not advance"));
                }
                return listDescriptorPage(next, result, cancellation);
            });
    }

    private ZLinkMeshNodeDescriptor selectTarget(
        Owned actor,
        List<ZLinkMeshNodeDescriptor> descriptors) {
        List<ZLinkMeshNodeDescriptor> candidates = descriptors.stream()
            .filter(candidate -> eligible(actor, candidate))
            .toList();
        if (candidates.isEmpty()) {
            throw new IllegalStateException(
                "No eligible Entry Spot Actor relocation target is Ready");
        }
        long total = candidates.stream()
            .mapToLong(ZLinkMeshNodeDescriptor::placementWeight)
            .reduce(0L, Math::addExact);
        long selected = ThreadLocalRandom.current().nextLong(total);
        for (var candidate : candidates) {
            selected -= candidate.placementWeight();
            if (selected < 0) {
                return candidate;
            }
        }
        return candidates.getLast();
    }

    private boolean eligible(Owned actor, ZLinkMeshNodeDescriptor candidate) {
        if (!candidate.meshName().equals(meshName)
            || candidate.rid().equals(localNodeRid)
            || candidate.state() != ZLinkFrameworkRuntimeState.SERVING
            || candidate.objectRole() != ZLinkMeshNodeObjectRole.SERVER
            || candidate.entrySpotId().isEmpty()
            || candidate.placementWeight() <= 0
            || !hasCapacity(candidate.capacity().actors(), 1)) {
            return false;
        }
        var factory = factories.get(actor.stableType());
        ZLinkObjectMaintenancePolicyKind policy =
            factory.relocationPolicy()
                instanceof ZLinkRelocationPolicy.Snapshot<?>
                ? ZLinkObjectMaintenancePolicyKind.SNAPSHOT
                : ZLinkObjectMaintenancePolicyKind.RECREATE;
        return candidate.objectCapabilities().stream().anyMatch(capability ->
            capability.objectKind() == ZLinkPlacementObjectKind.ACTOR
                && capability.stableType().equals(actor.stableType())
                && capability.policy() == policy
                && capability.hasSnapshotAdapter()
                    == actor.snapshotPolicy());
    }

    private ZLinkAggregateRelocationCoordinator.Request request(
        Owned actor,
        ZLinkMeshNodeDescriptor target,
        UUID relocationId,
        byte[] root) {
        String targetSpotId = target.entrySpotId().orElseThrow();
        byte[] targetAuthority = authorities.encode(
            ZLinkActorAuthorityPayloadCodec.State.READY,
            actor.stableType(),
            actor.actorId(),
            targetSpotId,
            target.lifecycleGeneration(),
            1,
            target.ownerId(),
            target.leaseGeneration(),
            meshName,
            target.rid(),
            target.lifecycleGeneration());
        var participant = new ZLinkAggregateRelocationCoordinator.Participant(
            actor.authorityKey(),
            ZLinkPlacementObjectKind.ACTOR,
            actor.snapshot().objectGeneration(),
            actor.snapshot().authorityOwnerGeneration(),
            actor.snapshot().storeVersion(),
            ZLinkAuthorityGenerationTransition.NEW_OWNER,
            targetAuthority,
            new byte[0]);
        return new ZLinkAggregateRelocationCoordinator.Request(
            relocationId,
            1,
            List.of(participant),
            root,
            new ZLinkMeshNodeDescriptorKey(meshName, target.rid()),
            target.lifecycleGeneration(),
            ZLinkPlacementCapacityBundle.actor(1),
            new ZLinkLocationOwnerToken(
                target.ownerId(), target.leaseGeneration()));
    }

    private static boolean hasCapacity(
        ZLinkCapacityUsage usage,
        int required) {
        return usage.limit() == 0
            || (long) usage.active() + usage.reserved() + required
                <= usage.limit();
    }

    static final class PreparedSource {
        private final ZLinkAggregateRelocationCoordinator coordinator;
        private final ZLinkActorSessionCoordinator actors;
        private final ZLinkAsyncSerialQueue.RelocationSeal seal;
        private final ZLinkRelocationPermitPool.Lease permit;
        private final Owned owned;
        private final ZLinkMeshNodeDescriptor target;
        private final UUID relocationId;
        private final byte[] state;
        private final ZLinkAggregateRelocationCoordinator.StagedRoot initial;
        private ZLinkAggregateRelocationCoordinator.Prepared prepared;
        private boolean committed;
        private boolean terminal;

        private PreparedSource(
            ZLinkAggregateRelocationCoordinator coordinator,
            ZLinkActorSessionCoordinator actors,
            ZLinkAsyncSerialQueue.RelocationSeal seal,
            ZLinkRelocationPermitPool.Lease permit,
            Owned owned,
            ZLinkMeshNodeDescriptor target,
            UUID relocationId,
            byte[] state,
            ZLinkAggregateRelocationCoordinator.StagedRoot initial) {
            this.coordinator = coordinator;
            this.actors = actors;
            this.seal = seal;
            this.permit = permit;
            this.owned = owned;
            this.target = target;
            this.relocationId = relocationId;
            this.state = state.clone();
            this.initial = initial;
        }

        ZLinkStandaloneActorRelocationStagingOwner.Request targetRequest() {
            return new ZLinkStandaloneActorRelocationStagingOwner.Request(
                relocationId,
                owned.actorId(),
                owned.stableType(),
                owned.snapshot().objectGeneration(),
                owned.snapshot().authorityOwnerGeneration(),
                owned.snapshotPolicy(),
                target.entrySpotId().orElseThrow());
        }

        ZLinkAggregateRelocationCoordinator.StagedRoot initialRoot() {
            return initial;
        }

        synchronized CompletionStage<
            ZLinkAggregateRelocationCoordinator.Prepared> freezeAndPrepare(
                ZLinkStoreCancellation cancellation) {
            if (terminal || committed || prepared != null) {
                return failed(new IllegalStateException(
                    "Actor relocation source is already terminal"));
            }
            List<ZLinkAsyncSerialQueue.QueuedRecord> held =
                actors.freezeActorRelocationIngress(
                    owned.actorId(), seal).orElseThrow(() ->
                        new IllegalStateException(
                            "Actor relocation ingress freeze was lost"));
            List<ZLinkAsyncSerialQueue.QueuedRecord> journal =
                new ArrayList<>(seal.captured());
            journal.addAll(held);
            byte[] finalRoot =
                ZLinkCanonicalActorRelocationEnvelope.encode(
                    relocationId,
                    owned.actorId(),
                    owned.snapshot().objectGeneration(),
                    owned.snapshot().authorityOwnerGeneration(),
                    owned.snapshotPolicy(),
                    state,
                    journal);
            if (!permit.tryShrinkPayload(finalRoot.length)) {
                return failed(new IllegalStateException(
                    "Actor relocation root exceeded its permit"));
            }
            var request = new ZLinkAggregateRelocationCoordinator.Request(
                initial.request().aggregateId(),
                initial.request().aggregateGeneration(),
                initial.request().participants(),
                finalRoot,
                initial.request().targetDescriptor(),
                initial.request().targetDescriptorLifecycleGeneration(),
                initial.request().capacityBundle(),
                initial.request().targetOwner());
            return coordinator.prepare(request, cancellation)
                .thenApply(value -> {
                    synchronized (PreparedSource.this) {
                        prepared = value;
                    }
                    return value;
                });
        }

        synchronized void commitSourceQueue() {
            if (terminal || committed || prepared == null
                || actors.commitActorRelocation(
                    owned.actorId(), seal).isEmpty()) {
                throw new IllegalStateException(
                    "Actor relocation source queue cannot be committed");
            }
            committed = true;
        }

        CompletionStage<Void> cleanupLocal() {
            if (!committed || terminal) {
                return failed(new IllegalStateException(
                    "Actor relocation source is not committed"));
            }
            return actors.completeRelocationSource(
                    List.of(owned.actorId()))
                .thenRun(this::finish);
        }

        synchronized CompletionStage<Void> abort() {
            if (terminal || committed) {
                return failed(new IllegalStateException(
                    "committed Actor relocation cannot be aborted"));
            }
            CompletionStage<Void> authority = prepared == null
                ? CompletableFuture.completedFuture(null)
                : coordinator.abort(prepared);
            return authority
                .thenCompose(ignored ->
                    coordinator.discardStagedRoot(initial))
                .thenRun(() -> {
                    if (!actors.abortActorRelocation(
                        owned.actorId(), seal)) {
                        throw new IllegalStateException(
                            "Actor relocation source queue was lost");
                    }
                    finish();
                });
        }

        private synchronized void finish() {
            if (!terminal) {
                terminal = true;
                permit.close();
            }
        }
    }

    private record Admission(Owned owned, ZLinkMeshNodeDescriptor target) {
    }

    private record Owned(
        String authorityKey,
        String actorId,
        String stableType,
        ZLinkAuthoritySnapshot snapshot,
        boolean snapshotPolicy) {
    }

    private static String requireText(String value, String name) {
        if (value == null || value.isBlank() || value.indexOf('\0') >= 0) {
            throw new IllegalArgumentException(name + " is invalid");
        }
        return value;
    }

    private static Throwable unwrap(Throwable failure) {
        Throwable current = failure;
        while (current instanceof CompletionException
            && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    private static <T> CompletionStage<T> cancelled() {
        return failed(new java.util.concurrent.CancellationException());
    }

    private static <T> CompletionStage<T> failed(Throwable failure) {
        return CompletableFuture.failedFuture(failure);
    }
}
