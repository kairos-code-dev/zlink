package systems.zlink.framework.runtime.locations;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import java.util.function.Predicate;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationFilter;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationChangeStampScope;
import systems.zlink.framework.locations.ZLinkLocationChangeStampStore;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteResult;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkMeshNodeDescriptor;
import systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey;
import systems.zlink.framework.locations.ZLinkOwnerLease;
import systems.zlink.framework.locations.ZLinkOwnerLeaseRenewal;
import systems.zlink.framework.locations.ZLinkOwnerLeaseSnapshot;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationFilter;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAcquireRequest;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAcquireResult;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAcquired;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocation;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocationMember;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocationSnapshot;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocationStore;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotGroupConfigurationMismatch;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotGroupExhausted;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotReleaseResult;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationFilter;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;

public final class ZLinkInMemoryLocationStore implements
    ZLinkLocationStore,
    ZLinkLocationChangeStampStore,
    ZLinkRoutingIdSlotAllocationStore {

    private final Object gate = new Object();
    private final Clock clock;
    private final ZLinkInMemoryAuthorityStore authority;
    private final Map<String, LeaseRow> leases = new HashMap<>();
    private long ownerLeaseGeneration;
    private final RowTable<ZLinkMeshNodeDescriptor> meshNodes =
        new RowTable<>();
    private final RowTable<ZLinkPeerLocation> peers = new RowTable<>();
    private final RowTable<ZLinkSpotLocation> spots = new RowTable<>();
    private final RowTable<ZLinkActorLocation> actors = new RowTable<>();
    private final RowTable<ZLinkRouteLocation> routes = new RowTable<>();
    private final Map<ZLinkLocationChangeStampScope, Long> stamps = new HashMap<>();
    private final Map<String, SlotGroup> slotGroups = new HashMap<>();

    public ZLinkInMemoryLocationStore() {
        this(Clock.systemUTC());
    }

    public ZLinkInMemoryLocationStore(Clock clock) {
        this.clock = Objects.requireNonNull(clock, "clock");
        this.authority = new ZLinkInMemoryAuthorityStore(
            clock,
            this::isExactOwnerLeaseLive,
            this::findMeshNodeDescriptor);
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
    public CompletionStage<systems.zlink.framework.locations.ZLinkObjectAbortResult> abort(
        systems.zlink.framework.locations.ZLinkObjectReservation reservation,
        systems.zlink.framework.locations.ZLinkStoreCancellation cancellation) {
        return authority.abort(reservation, cancellation);
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
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent) {
        Objects.requireNonNull(descriptor, "descriptor");
        Objects.requireNonNull(intent, "intent");
        synchronized (gate) {
            ZLinkLocationOwnerToken owner =
                new ZLinkLocationOwnerToken(
                    descriptor.ownerId(),
                    descriptor.leaseGeneration());
            if (!isExactOwnerLeaseLive(owner)) {
                return completed(
                    ZLinkLocationWriteResult.ignoredStale());
            }
            String key = meshNodeKey(
                new ZLinkMeshNodeDescriptorKey(
                    descriptor.meshName(),
                    descriptor.rid()));
            ZLinkMeshNodeDescriptor current =
                meshNodes.rows.get(key);
            if (intent == ZLinkLocationWriteIntent.NEW_CLAIM
                && current != null
                && isExactOwnerLeaseLive(
                    new ZLinkLocationOwnerToken(
                        current.ownerId(),
                        current.leaseGeneration()))) {
                return completed(
                    ZLinkLocationWriteResult.rejectedConflict());
            }
            if (intent == ZLinkLocationWriteIntent.TAKEOVER
                && current != null
                && isExactOwnerLeaseLive(
                    new ZLinkLocationOwnerToken(
                        current.ownerId(),
                        current.leaseGeneration()))) {
                return completed(
                    ZLinkLocationWriteResult.rejectedConflict());
            }
            if (intent == ZLinkLocationWriteIntent.RENEW
                && current != null
                && descriptor.descriptorRevision()
                    == current.descriptorRevision()) {
                if (hasSameDescriptorFields(current, descriptor)) {
                    return completed(ZLinkLocationWriteResult.stored(
                        current.lifecycleGeneration(),
                        current.updatedAt()));
                }
                throw new IllegalArgumentException(
                    "same descriptor revision has different bytes");
            }
            if (intent == ZLinkLocationWriteIntent.RENEW
                && (current == null
                    || !current.ownerId().equals(
                        descriptor.ownerId())
                    || current.leaseGeneration()
                        != descriptor.leaseGeneration()
                    || current.lifecycleGeneration()
                        != descriptor.lifecycleGeneration()
                    || !hasSameImmutableDescriptorFields(
                        current,
                        descriptor)
                    || descriptor.descriptorRevision()
                        <= current.descriptorRevision())) {
                return completed(
                    ZLinkLocationWriteResult.ignoredStale());
            }
            Instant now = clock.instant();
            meshNodes.rows.put(
                key,
                copyDescriptor(descriptor, now));
            return completed(ZLinkLocationWriteResult.stored(
                descriptor.lifecycleGeneration(),
                now));
        }
    }

    @Override
    public CompletionStage<ZLinkLocationWriteStatus> removeMeshNode(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner) {
        synchronized (gate) {
            ZLinkMeshNodeDescriptor current =
                meshNodes.rows.get(meshNodeKey(key));
            if (current == null
                || !current.ownerId().equals(owner.ownerId())
                || current.leaseGeneration()
                    != owner.leaseGeneration()) {
                return completed(
                    ZLinkLocationWriteStatus.IGNORED_STALE);
            }
            meshNodes.rows.remove(meshNodeKey(key));
            return completed(ZLinkLocationWriteStatus.STORED);
        }
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkMeshNodeDescriptor>>
        listMeshNodes(String meshName, ZLinkPageRequest page) {
        ZLinkLocationPage<ZLinkMeshNodeDescriptor> stored;
        synchronized (gate) {
            stored = page(
                meshNodes,
                descriptor -> descriptor.meshName().equals(meshName),
                page);
        }
        return completed(new ZLinkLocationPage<>(
            stored.items().stream()
                .map(this::projectCapacity)
                .toList(),
            stored.continuationToken()));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updatePeer(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent) {
        return completed(write(
            peers,
            ZLinkLocationKeyCodec.encodePeerKey(new ZLinkPeerLocationKey(
                peer.autoConnectType(), peer.meshName(), peer.role(), peer.nodeRid(), peer.endpoint())),
            peer,
            intent,
            peer.ownerId(),
            peer.generation(),
            ZLinkPeerLocation::ownerId,
            ZLinkPeerLocation::generation,
            (row, generation, now) -> new ZLinkPeerLocation(
                row.autoConnectType(), row.meshName(), row.nodeRid(), row.role(), row.endpoint(),
                row.weight(), row.draining(), row.value(), row.metadata(), row.capabilities(), row.ownerId(),
                generation, now),
            ZLinkLocationKind.PEER,
            peer.meshName()));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removePeer(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return completed(remove(
            peers,
            ZLinkLocationKeyCodec.encodePeerKey(key),
            owner,
            ZLinkPeerLocation::ownerId,
            ZLinkPeerLocation::generation,
            ZLinkLocationKind.PEER,
            key.meshName()));
    }

    @Override
    public CompletionStage<List<ZLinkPeerLocation>> listPeerLocations(ZLinkPeerLocationFilter filter) {
        synchronized (gate) {
            return completed(peers.rows.values().stream().filter(row -> matches(row, filter)).toList());
        }
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateSpot(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent) {
        return completed(write(
            spots,
            ZLinkLocationKeyCodec.encodeSpotKey(new ZLinkSpotLocationKey(spot.meshName(), spot.spotRid())),
            spot,
            intent,
            spot.ownerId(),
            spot.generation(),
            ZLinkSpotLocation::ownerId,
            ZLinkSpotLocation::generation,
            (row, generation, now) -> new ZLinkSpotLocation(
                row.meshName(), row.spotRid(), row.spotGeneration(), row.spotType(),
                row.nodeRid(), row.spotKind(),
                row.routeEndpoint(), row.ownerId(), generation, now),
            ZLinkLocationKind.SPOT,
            spot.meshName()));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeSpot(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return completed(remove(
            spots,
            ZLinkLocationKeyCodec.encodeSpotKey(key),
            owner,
            ZLinkSpotLocation::ownerId,
            ZLinkSpotLocation::generation,
            ZLinkLocationKind.SPOT,
            key.meshName()));
    }

    @Override
    public CompletionStage<ZLinkSpotLocation> resolveSpot(ZLinkSpotLocationKey key) {
        synchronized (gate) {
            return completed(spots.rows.get(ZLinkLocationKeyCodec.encodeSpotKey(key)));
        }
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocations(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page) {
        synchronized (gate) {
            return completed(page(spots, row -> matches(row, filter), page));
        }
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateActor(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent) {
        return completed(write(
            actors,
            ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(actor.actorId())),
            actor,
            intent,
            actor.ownerId(),
            actor.generation(),
            ZLinkActorLocation::ownerId,
            ZLinkActorLocation::generation,
            (row, generation, now) -> new ZLinkActorLocation(
                row.actorId(), row.actorType(), row.actorRef(), row.nodeRid(), row.locationKind(),
                row.spotMeshName(), row.spotRid(), row.ownerId(), generation, now),
            ZLinkLocationKind.ACTOR,
            null));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeActor(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return completed(remove(
            actors,
            ZLinkLocationKeyCodec.encodeActorKey(key),
            owner,
            ZLinkActorLocation::ownerId,
            ZLinkActorLocation::generation,
            ZLinkLocationKind.ACTOR,
            null));
    }

    @Override
    public CompletionStage<ZLinkActorLocation> resolveActor(ZLinkActorLocationKey key) {
        synchronized (gate) {
            return completed(actors.rows.get(ZLinkLocationKeyCodec.encodeActorKey(key)));
        }
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocations(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page) {
        synchronized (gate) {
            return completed(page(actors, row -> matches(row, filter), page));
        }
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateRoute(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent) {
        return completed(write(
            routes,
            ZLinkLocationKeyCodec.encodeRouteKey(new ZLinkRouteLocationKey(route.routeKind(), route.routeKey())),
            route,
            intent,
            route.ownerId(),
            route.generation(),
            ZLinkRouteLocation::ownerId,
            ZLinkRouteLocation::generation,
            (row, generation, now) -> new ZLinkRouteLocation(
                row.routeKind(), row.routeKey(), row.ownerNodeRid(), row.ownerId(), generation,
                row.value(), now),
            ZLinkLocationKind.ROUTE,
            null));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeRoute(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return completed(remove(
            routes,
            ZLinkLocationKeyCodec.encodeRouteKey(key),
            owner,
            ZLinkRouteLocation::ownerId,
            ZLinkRouteLocation::generation,
            ZLinkLocationKind.ROUTE,
            null));
    }

    @Override
    public CompletionStage<ZLinkRouteLocation> resolveRoute(ZLinkRouteLocationKey key) {
        synchronized (gate) {
            return completed(routes.rows.get(ZLinkLocationKeyCodec.encodeRouteKey(key)));
        }
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocations(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page) {
        synchronized (gate) {
            return completed(page(routes, row -> matches(row, filter), page));
        }
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult>
        claimOwnerLease(
        String ownerId,
        Duration leaseTtl) {
        synchronized (gate) {
            Instant now = clock.instant();
            LeaseRow current = leases.get(ownerId);
            if (current != null && current.expiresAt().isAfter(now)) {
                return completed(new systems.zlink.framework.locations
                    .ZLinkOwnerLeaseClaimConflict());
            }
            if (ownerLeaseGeneration == Long.MAX_VALUE) {
                return completed(new systems.zlink.framework.locations
                    .ZLinkOwnerLeaseGenerationExhausted());
            }
            ZLinkLocationOwnerToken token = new ZLinkLocationOwnerToken(
                ownerId,
                ++ownerLeaseGeneration);
            Instant expiresAt = now.plus(leaseTtl);
            leases.put(ownerId, new LeaseRow(token, expiresAt));
            return completed(new systems.zlink.framework.locations
                .ZLinkOwnerLeaseClaimed(token, expiresAt, now));
        }
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult>
        readOwnerLease(String ownerId) {
        synchronized (gate) {
            Instant now = clock.instant();
            LeaseRow current = leases.get(ownerId);
            if (current == null || !current.expiresAt().isAfter(now)) {
                leases.remove(ownerId);
                return completed(new systems.zlink.framework.locations
                    .ZLinkOwnerLeaseMissing());
            }
            return completed(new systems.zlink.framework.locations
                .ZLinkOwnerLeaseFound(
                    current.token(),
                    current.expiresAt(),
                    now));
        }
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult>
        renewOwnerLease(
            ZLinkLocationOwnerToken token,
            Duration leaseTtl) {
        synchronized (gate) {
            Instant now = clock.instant();
            LeaseRow current = leases.get(token.ownerId());
            if (current == null
                || !current.expiresAt().isAfter(now)
                || !current.token().equals(token)) {
                return completed(new systems.zlink.framework.locations
                    .ZLinkOwnerLeaseRenewStale());
            }
            Instant expiresAt = now.plus(leaseTtl);
            leases.put(token.ownerId(), new LeaseRow(token, expiresAt));
            return completed(new systems.zlink.framework.locations
                .ZLinkOwnerLeaseRenewed(expiresAt, now));
        }
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult>
        releaseOwnerLease(ZLinkLocationOwnerToken token) {
        synchronized (gate) {
            Instant now = clock.instant();
            LeaseRow current = leases.get(token.ownerId());
            if (current == null
                || !current.expiresAt().isAfter(now)
                || !current.token().equals(token)) {
                if (current != null
                    && !current.expiresAt().isAfter(now)) {
                    leases.remove(token.ownerId());
                }
                return completed(systems.zlink.framework.locations
                    .ZLinkOwnerLeaseReleaseResult.STALE);
            }
            leases.remove(token.ownerId());
            return completed(systems.zlink.framework.locations
                .ZLinkOwnerLeaseReleaseResult.RELEASED);
        }
    }

    @Override
    public CompletionStage<Long> removeAllByOwner(
        ZLinkLocationOwnerToken owner) {
        Objects.requireNonNull(owner, "owner");
        synchronized (gate) {
            if (!isExactOwnerLeaseLive(owner)) {
                return CompletableFuture.failedFuture(
                    new IllegalStateException(
                        "Owner cleanup token is stale."));
            }
            String ownerId = owner.ownerId();
            long removed = 0;
            List<String> descriptorKeys = meshNodes.rows.entrySet()
                .stream()
                .filter(entry -> entry.getValue().ownerId()
                    .equals(ownerId))
                .map(Map.Entry::getKey)
                .toList();
            descriptorKeys.forEach(meshNodes.rows::remove);
            removed += descriptorKeys.size();
            removed += removeByOwner(peers, ownerId, ZLinkPeerLocation::ownerId, ZLinkLocationKind.PEER, ZLinkPeerLocation::meshName);
            removed += removeByOwner(spots, ownerId, ZLinkSpotLocation::ownerId, ZLinkLocationKind.SPOT, ZLinkSpotLocation::meshName);
            removed += removeByOwner(actors, ownerId, ZLinkActorLocation::ownerId, ZLinkLocationKind.ACTOR, row -> null);
            removed += removeByOwner(routes, ownerId, ZLinkRouteLocation::ownerId, ZLinkLocationKind.ROUTE, row -> null);
            return completed(removed);
        }
    }

    @Override
    public CompletionStage<Long> getChangeStamp(ZLinkLocationChangeStampScope scope) {
        synchronized (gate) {
            return completed(stamps.getOrDefault(scope, 0L));
        }
    }

    @Override
    public CompletionStage<ZLinkRoutingIdSlotAcquireResult> acquireRoutingIdSlot(
        ZLinkRoutingIdSlotAcquireRequest request) {
        Objects.requireNonNull(request, "request");
        List<ZLinkRoutingIdSlotAllocationMember> members = normalizeMembers(request.members());
        synchronized (gate) {
            Instant now = clock.instant();
            SlotGroup group = slotGroups.get(request.groupName());
            if (group == null) {
                group = new SlotGroup(members, request.slotCount());
                slotGroups.put(request.groupName(), group);
            } else if (group.slotCount != request.slotCount()
                || !group.members.equals(members)) {
                return completed(new ZLinkRoutingIdSlotGroupConfigurationMismatch(
                    members,
                    request.slotCount(),
                    group.members,
                    group.slotCount));
            }

            for (var entry : group.allocations.entrySet()) {
                ZLinkRoutingIdSlotAllocation current = entry.getValue();
                if (current.owner().ownerId().equals(request.ownerId())
                    && isOwnerLive(request.ownerId(), now)) {
                    ZLinkRoutingIdSlotAllocation renewed = new ZLinkRoutingIdSlotAllocation(
                        current.slot(),
                        current.owner(),
                        now.plus(request.leaseTtl()),
                        now);
                    entry.setValue(renewed);
                    renewAllocationOwner(request.ownerId(), request.leaseTtl(), now);
                    return completed(new ZLinkRoutingIdSlotAcquired(renewed));
                }
            }

            int selected = 0;
            for (int slot = 1; slot <= group.slotCount; slot++) {
                ZLinkRoutingIdSlotAllocation current = group.allocations.get(slot);
                if (current == null || !isOwnerLive(current.owner().ownerId(), now)) {
                    selected = slot;
                    break;
                }
            }
            if (selected == 0) {
                return completed(new ZLinkRoutingIdSlotGroupExhausted());
            }

            long generation = group.generations.getOrDefault(selected, 0L) + 1L;
            group.generations.put(selected, generation);
            ZLinkRoutingIdSlotAllocation acquired = new ZLinkRoutingIdSlotAllocation(
                selected,
                new ZLinkLocationOwnerToken(request.ownerId(), generation),
                now.plus(request.leaseTtl()),
                now);
            group.allocations.put(selected, acquired);
            renewAllocationOwner(request.ownerId(), request.leaseTtl(), now);
            return completed(new ZLinkRoutingIdSlotAcquired(acquired));
        }
    }

    @Override
    public CompletionStage<ZLinkRoutingIdSlotReleaseResult> releaseRoutingIdSlot(
        String groupName,
        int slot,
        ZLinkLocationOwnerToken owner) {
        synchronized (gate) {
            SlotGroup group = slotGroups.get(groupName);
            ZLinkRoutingIdSlotAllocation current = group == null
                ? null
                : group.allocations.get(slot);
            if (current == null || !current.owner().equals(owner)) {
                return completed(ZLinkRoutingIdSlotReleaseResult.IGNORED_STALE);
            }
            group.allocations.remove(slot);
            return completed(ZLinkRoutingIdSlotReleaseResult.RELEASED);
        }
    }

    @Override
    public CompletionStage<ZLinkRoutingIdSlotAllocationSnapshot> listRoutingIdSlots(
        String groupName) {
        synchronized (gate) {
            Instant now = clock.instant();
            SlotGroup group = slotGroups.get(groupName);
            if (group == null) {
                return completed(new ZLinkRoutingIdSlotAllocationSnapshot(
                    groupName, List.of(), 0, List.of(), now));
            }
            List<ZLinkRoutingIdSlotAllocation> allocations = group.allocations.values().stream()
                .filter(value -> isOwnerLive(value.owner().ownerId(), now))
                .sorted(Comparator.comparingInt(ZLinkRoutingIdSlotAllocation::slot))
                .toList();
            return completed(new ZLinkRoutingIdSlotAllocationSnapshot(
                groupName, group.members, group.slotCount, allocations, now));
        }
    }

    private void renewAllocationOwner(String ownerId, Duration leaseTtl, Instant now) {
        LeaseRow current = leases.get(ownerId);
        if (current == null) {
            if (ownerLeaseGeneration == Long.MAX_VALUE) {
                throw new ZLinkConfigurationException(
                    "owner lease generation is exhausted");
            }
            current = new LeaseRow(
                new ZLinkLocationOwnerToken(
                    ownerId,
                    ++ownerLeaseGeneration),
                now.plus(leaseTtl));
        }
        leases.put(
            ownerId,
            new LeaseRow(
                current.token(),
                now.plus(leaseTtl)));
    }

    private static List<ZLinkRoutingIdSlotAllocationMember> normalizeMembers(
        List<ZLinkRoutingIdSlotAllocationMember> members) {
        return members.stream()
            .sorted(Comparator.comparing(ZLinkRoutingIdSlotAllocationMember::meshName)
                .thenComparing(ZLinkRoutingIdSlotAllocationMember::routingIdPrefix))
            .toList();
    }

    private <TRow> ZLinkLocationWriteResult write(
        RowTable<TRow> table,
        String key,
        TRow row,
        ZLinkLocationWriteIntent intent,
        String ownerId,
        long generation,
        Function<TRow, String> ownerOf,
        Function<TRow, Long> generationOf,
        RowFinalizer<TRow> finalizer,
        ZLinkLocationKind kind,
        String meshName) {
        synchronized (gate) {
            Instant now = clock.instant();
            TRow current = table.rows.get(key);
            boolean exists = current != null;
            if (intent == ZLinkLocationWriteIntent.NEW_CLAIM && exists && isOwnerLive(ownerOf.apply(current), now)) {
                return ZLinkLocationWriteResult.rejectedConflict();
            }

            if (intent == ZLinkLocationWriteIntent.NEW_CLAIM || intent == ZLinkLocationWriteIntent.TAKEOVER) {
                long next = table.generations.getOrDefault(key, 0L) + 1;
                table.generations.put(key, next);
                table.rows.put(key, finalizer.apply(row, next, now));
                bump(kind, meshName);
                return ZLinkLocationWriteResult.stored(next, now);
            }

            if (intent == ZLinkLocationWriteIntent.RENEW
                && exists
                && Objects.equals(ownerOf.apply(current), ownerId)
                && generationOf.apply(current) == generation) {
                table.rows.put(key, finalizer.apply(row, generation, now));
                bump(kind, meshName);
                return ZLinkLocationWriteResult.stored(generation, now);
            }

            return ZLinkLocationWriteResult.ignoredStale();
        }
    }

    private <TRow> ZLinkLocationWriteResult remove(
        RowTable<TRow> table,
        String key,
        ZLinkLocationOwnerToken owner,
        Function<TRow, String> ownerOf,
        Function<TRow, Long> generationOf,
        ZLinkLocationKind kind,
        String meshName) {
        synchronized (gate) {
            TRow current = table.rows.get(key);
            if (current == null
                || !Objects.equals(ownerOf.apply(current), owner.ownerId())
                || generationOf.apply(current) != owner.leaseGeneration()) {
                return ZLinkLocationWriteResult.ignoredStale();
            }

            table.rows.remove(key);
            bump(kind, meshName);
            return ZLinkLocationWriteResult.stored(
                owner.leaseGeneration(),
                clock.instant());
        }
    }

    private <TRow> long removeByOwner(
        RowTable<TRow> table,
        String ownerId,
        Function<TRow, String> ownerOf,
        ZLinkLocationKind kind,
        Function<TRow, String> meshOf) {
        synchronized (gate) {
            List<String> removedKeys = table.rows.entrySet().stream()
                .filter(pair -> Objects.equals(ownerOf.apply(pair.getValue()), ownerId))
                .map(Map.Entry::getKey)
                .toList();
            for (String key : removedKeys) {
                TRow row = table.rows.remove(key);
                bump(kind, meshOf.apply(row));
            }
            return removedKeys.size();
        }
    }

    private <TRow> ZLinkLocationPage<TRow> page(
        RowTable<TRow> table,
        Predicate<TRow> matches,
        ZLinkPageRequest request) {
        ZLinkPageRequest safeRequest = request == null ? ZLinkPageRequest.firstPage() : request;
        List<Map.Entry<String, TRow>> ordered = table.rows.entrySet().stream()
            .filter(pair -> matches.test(pair.getValue()))
            .sorted(Comparator.comparing(Map.Entry::getKey))
            .toList();

        int offset = parseOffset(safeRequest.continuationToken());
        int size = safeRequest.pageSize() > 0 ? safeRequest.pageSize() : Integer.MAX_VALUE;
        List<TRow> items = new ArrayList<>();
        for (int i = offset; i < ordered.size() && items.size() < size; i++) {
            items.add(ordered.get(i).getValue());
        }

        int nextOffset = offset + items.size();
        String next = nextOffset < ordered.size() ? Integer.toString(nextOffset) : null;
        return new ZLinkLocationPage<>(List.copyOf(items), next);
    }

    private int parseOffset(String token) {
        if (token == null || token.isBlank()) {
            return 0;
        }
        try {
            return Math.max(0, Integer.parseInt(token));
        } catch (NumberFormatException ignored) {
            return 0;
        }
    }

    private boolean isOwnerLive(String ownerId, Instant now) {
        LeaseRow lease = leases.get(ownerId);
        return lease != null && lease.expiresAt().isAfter(now);
    }

    private boolean isExactOwnerLeaseLive(
        ZLinkLocationOwnerToken token) {
        synchronized (gate) {
            LeaseRow lease = leases.get(token.ownerId());
            return lease != null
                && lease.token().equals(token)
                && lease.expiresAt().isAfter(clock.instant());
        }
    }

    private ZLinkMeshNodeDescriptor findMeshNodeDescriptor(
        ZLinkMeshNodeDescriptorKey key,
        long lifecycleGeneration,
        ZLinkLocationOwnerToken owner) {
        synchronized (gate) {
            return meshNodes.rows.get(meshNodeKey(key));
        }
    }

    private static String meshNodeKey(
        ZLinkMeshNodeDescriptorKey key) {
        return key.meshName().length()
            + ":"
            + key.meshName()
            + key.rid().toHex().length()
            + ":"
            + key.rid().toHex();
    }

    private static ZLinkMeshNodeDescriptor copyDescriptor(
        ZLinkMeshNodeDescriptor descriptor,
        Instant updatedAt) {
        return new ZLinkMeshNodeDescriptor(
            descriptor.meshName(),
            descriptor.rid(),
            descriptor.lifecycleGeneration(),
            descriptor.descriptorRevision(),
            descriptor.endpoint(),
            descriptor.channelWeights(),
            descriptor.applicationVersion(),
            descriptor.objectCapabilities(),
            descriptor.objectRole(),
            descriptor.placementWeight(),
            descriptor.capacity(),
            descriptor.maintenanceWave(),
            descriptor.state(),
            descriptor.securityIdentity(),
            descriptor.ownerId(),
            descriptor.leaseGeneration(),
            updatedAt);
    }

    private ZLinkMeshNodeDescriptor projectCapacity(
        ZLinkMeshNodeDescriptor descriptor) {
        long active = authority.activeCapacity(
            new ZLinkMeshNodeDescriptorKey(
                descriptor.meshName(),
                descriptor.rid()),
            descriptor.lifecycleGeneration());
        long pending = authority.pendingCapacity(
            new ZLinkMeshNodeDescriptorKey(
                descriptor.meshName(),
                descriptor.rid()),
            descriptor.lifecycleGeneration());
        return new ZLinkMeshNodeDescriptor(
            descriptor.meshName(),
            descriptor.rid(),
            descriptor.lifecycleGeneration(),
            descriptor.descriptorRevision(),
            descriptor.endpoint(),
            descriptor.channelWeights(),
            descriptor.applicationVersion(),
            descriptor.objectCapabilities(),
            descriptor.objectRole(),
            descriptor.placementWeight(),
            new systems.zlink.framework.locations.ZLinkPlacementCapacity(
                Math.toIntExact(active),
                Math.toIntExact(pending),
                descriptor.capacity().activeLimit(),
                descriptor.capacity().pendingLimit()),
            descriptor.maintenanceWave(),
            descriptor.state(),
            descriptor.securityIdentity(),
            descriptor.ownerId(),
            descriptor.leaseGeneration(),
            descriptor.updatedAt());
    }

    private static boolean hasSameImmutableDescriptorFields(
        ZLinkMeshNodeDescriptor current,
        ZLinkMeshNodeDescriptor candidate) {
        return current.meshName().equals(candidate.meshName())
            && current.rid().equals(candidate.rid())
            && current.lifecycleGeneration()
                == candidate.lifecycleGeneration()
            && current.endpoint().equals(candidate.endpoint())
            && current.channelWeights().keySet().equals(
                candidate.channelWeights().keySet())
            && current.applicationVersion()
                == candidate.applicationVersion()
            && hasSameImmutableCapabilities(
                current.objectCapabilities(),
                candidate.objectCapabilities())
            && current.objectRole() == candidate.objectRole()
            && current.capacity().activeLimit()
                == candidate.capacity().activeLimit()
            && current.capacity().pendingLimit()
                == candidate.capacity().pendingLimit()
            && current.securityIdentity().equals(
                candidate.securityIdentity())
            && current.ownerId().equals(candidate.ownerId())
            && current.leaseGeneration()
                == candidate.leaseGeneration();
    }

    private static boolean hasSameImmutableCapabilities(
        List<systems.zlink.framework.locations.ZLinkObjectCapability> current,
        List<systems.zlink.framework.locations.ZLinkObjectCapability> candidate) {
        if (current.size() != candidate.size()) {
            return false;
        }
        for (int index = 0; index < current.size(); index++) {
            var left = current.get(index);
            var right = candidate.get(index);
            if (left.objectKind() != right.objectKind()
                || !left.stableType().equals(right.stableType())
                || left.policy() != right.policy()
                || left.hasSnapshotAdapter()
                    != right.hasSnapshotAdapter()
                || !left.placementProfiles().equals(
                    right.placementProfiles())
                || !Objects.equals(
                    left.activeLimit(),
                    right.activeLimit())
                || !Objects.equals(
                    left.pendingLimit(),
                    right.pendingLimit())) {
                return false;
            }
        }
        return true;
    }

    private static boolean hasSameDescriptorFields(
        ZLinkMeshNodeDescriptor current,
        ZLinkMeshNodeDescriptor candidate) {
        return current.meshName().equals(candidate.meshName())
            && current.rid().equals(candidate.rid())
            && current.lifecycleGeneration()
                == candidate.lifecycleGeneration()
            && current.descriptorRevision()
                == candidate.descriptorRevision()
            && current.endpoint().equals(candidate.endpoint())
            && current.channelWeights().equals(candidate.channelWeights())
            && current.applicationVersion()
                == candidate.applicationVersion()
            && current.objectCapabilities().equals(
                candidate.objectCapabilities())
            && current.objectRole() == candidate.objectRole()
            && current.placementWeight() == candidate.placementWeight()
            && current.capacity().equals(candidate.capacity())
            && current.maintenanceWave().equals(
                candidate.maintenanceWave())
            && current.state() == candidate.state()
            && current.securityIdentity().equals(
                candidate.securityIdentity())
            && current.ownerId().equals(candidate.ownerId())
            && current.leaseGeneration()
                == candidate.leaseGeneration();
    }

    private void bump(ZLinkLocationKind kind, String meshName) {
        bumpScope(new ZLinkLocationChangeStampScope(kind, meshName));
        if (meshName != null) {
            bumpScope(new ZLinkLocationChangeStampScope(kind, null));
        }
    }

    private void bumpScope(ZLinkLocationChangeStampScope scope) {
        stamps.put(scope, stamps.getOrDefault(scope, 0L) + 1);
    }

    private static boolean matches(ZLinkPeerLocation row, ZLinkPeerLocationFilter filter) {
        ZLinkPeerLocationFilter safeFilter = filter == null ? ZLinkPeerLocationFilter.all() : filter;
        return (safeFilter.autoConnectType() == null || row.autoConnectType() == safeFilter.autoConnectType())
            && (safeFilter.meshName() == null || Objects.equals(row.meshName(), safeFilter.meshName()))
            && (safeFilter.role() == null || row.role() == safeFilter.role())
            && (safeFilter.nodeRid() == null || Objects.equals(row.nodeRid(), safeFilter.nodeRid()))
            && (safeFilter.endpoint() == null || Objects.equals(row.endpoint(), safeFilter.endpoint()));
    }

    private static boolean matches(ZLinkSpotLocation row, ZLinkSpotLocationFilter filter) {
        ZLinkSpotLocationFilter safeFilter = filter == null ? ZLinkSpotLocationFilter.all() : filter;
        return (safeFilter.meshName() == null || Objects.equals(row.meshName(), safeFilter.meshName()))
            && (safeFilter.spotType() == null || Objects.equals(row.spotType(), safeFilter.spotType()))
            && (safeFilter.nodeRid() == null || Objects.equals(row.nodeRid(), safeFilter.nodeRid()))
            && (safeFilter.spotKind() == null || row.spotKind() == safeFilter.spotKind());
    }

    private static boolean matches(ZLinkActorLocation row, ZLinkActorLocationFilter filter) {
        ZLinkActorLocationFilter safeFilter = filter == null ? ZLinkActorLocationFilter.all() : filter;
        return (safeFilter.actorType() == null || Objects.equals(row.actorType(), safeFilter.actorType()))
            && (safeFilter.nodeRid() == null || Objects.equals(row.nodeRid(), safeFilter.nodeRid()))
            && (safeFilter.spotRid() == null || Objects.equals(row.spotRid(), safeFilter.spotRid()))
            && (safeFilter.locationKind() == null || row.locationKind() == safeFilter.locationKind());
    }

    private static boolean matches(ZLinkRouteLocation row, ZLinkRouteLocationFilter filter) {
        ZLinkRouteLocationFilter safeFilter = filter == null ? ZLinkRouteLocationFilter.all() : filter;
        return (safeFilter.routeKind() == null || row.routeKind() == safeFilter.routeKind())
            && (safeFilter.ownerNodeRid() == null || Objects.equals(row.ownerNodeRid(), safeFilter.ownerNodeRid()))
            && (safeFilter.ownerId() == null || Objects.equals(row.ownerId(), safeFilter.ownerId()));
    }

    private static <T> CompletionStage<T> completed(T value) {
        return CompletableFuture.completedFuture(value);
    }

    @FunctionalInterface
    private interface RowFinalizer<TRow> {
        TRow apply(TRow row, long generation, Instant updatedAt);
    }

    private static final class RowTable<TRow> {
        private final Map<String, TRow> rows = new HashMap<>();
        private final Map<String, Long> generations = new HashMap<>();
    }

    private static final class SlotGroup {
        private final List<ZLinkRoutingIdSlotAllocationMember> members;
        private final int slotCount;
        private final Map<Integer, ZLinkRoutingIdSlotAllocation> allocations = new HashMap<>();
        private final Map<Integer, Long> generations = new HashMap<>();

        private SlotGroup(List<ZLinkRoutingIdSlotAllocationMember> members, int slotCount) {
            this.members = List.copyOf(members);
            this.slotCount = slotCount;
        }
    }

    private record LeaseRow(
        ZLinkLocationOwnerToken token,
        Instant expiresAt) {
    }
}
