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
    private final Map<String, ZLinkOwnerLease> leases = new HashMap<>();
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
    public CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLease(
        String ownerId,
        RoutingId nodeRid,
        Duration leaseTtl) {
        synchronized (gate) {
            Instant now = clock.instant();
            Instant expiresAt = now.plus(leaseTtl);
            leases.put(ownerId, new ZLinkOwnerLease(ownerId, nodeRid, expiresAt, now));
            return completed(new ZLinkOwnerLeaseRenewal(expiresAt, now));
        }
    }

    @Override
    public CompletionStage<Boolean> removeOwnerLease(String ownerId) {
        synchronized (gate) {
            return completed(leases.remove(ownerId) != null);
        }
    }

    @Override
    public CompletionStage<Long> removeAllByOwner(String ownerId) {
        synchronized (gate) {
            long removed = 0;
            removed += removeByOwner(peers, ownerId, ZLinkPeerLocation::ownerId, ZLinkLocationKind.PEER, ZLinkPeerLocation::meshName);
            removed += removeByOwner(spots, ownerId, ZLinkSpotLocation::ownerId, ZLinkLocationKind.SPOT, ZLinkSpotLocation::meshName);
            removed += removeByOwner(actors, ownerId, ZLinkActorLocation::ownerId, ZLinkLocationKind.ACTOR, row -> null);
            removed += removeByOwner(routes, ownerId, ZLinkRouteLocation::ownerId, ZLinkLocationKind.ROUTE, row -> null);
            return completed(removed);
        }
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeases() {
        synchronized (gate) {
            return completed(new ZLinkOwnerLeaseSnapshot(List.copyOf(leases.values()), clock.instant()));
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
        ZLinkOwnerLease current = leases.get(ownerId);
        RoutingId nodeRid = current == null ? RoutingId.from(ownerId) : current.nodeRid();
        leases.put(ownerId, new ZLinkOwnerLease(ownerId, nodeRid, now.plus(leaseTtl), now));
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
                || generationOf.apply(current) != owner.generation()) {
                return ZLinkLocationWriteResult.ignoredStale();
            }

            table.rows.remove(key);
            bump(kind, meshName);
            return ZLinkLocationWriteResult.stored(owner.generation(), clock.instant());
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
        ZLinkOwnerLease lease = leases.get(ownerId);
        return lease != null && lease.leaseExpiresAt().isAfter(now);
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
}
