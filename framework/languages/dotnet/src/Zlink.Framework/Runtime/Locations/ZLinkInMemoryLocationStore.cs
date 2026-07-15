using System.Globalization;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Single-process store for local development, unit tests, and sample smoke
/// tests. It backs every location store interface plus the owner lease store
/// in one instance, which satisfies the contract requirement that location
/// rows and owner leases share one physical store. Never use it for
/// production topologies where processes must share location data.
/// </summary>
internal sealed class ZLinkInMemoryLocationStore :
    IZLinkLocationStore,
    IZLinkRoutingIdSlotAllocationStore,
    IZLinkLocationChangeStampStore
{
    private readonly object _gate = new();
    private readonly TimeProvider _time;
    private readonly Dictionary<string, ZLinkOwnerLease> _leases = [];
    private readonly RowTable<ZLinkPeerLocation> _peers = new();
    private readonly RowTable<ZLinkSpotLocation> _spots = new();
    private readonly RowTable<ZLinkActorLocation> _actors = new();
    private readonly RowTable<ZLinkRouteLocation> _routes = new();
    private readonly Dictionary<ZLinkLocationChangeStampScope, long> _stamps = [];
    private readonly Dictionary<string, RoutingIdAllocationGroup> _routingIdGroups =
        new(StringComparer.Ordinal);

    public ZLinkInMemoryLocationStore(TimeProvider? timeProvider = null)
    {
        _time = timeProvider ?? TimeProvider.System;
    }

    public ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Write(
            _peers,
            ZLinkLocationKeyCodec.EncodePeerKey(new ZLinkPeerLocationKey(
                peer.AutoConnectType, peer.MeshName, peer.Role, peer.NodeRid, peer.Endpoint)),
            peer,
            intent,
            peer.OwnerId,
            peer.Generation,
            static row => row.OwnerId,
            static row => row.Generation,
            static (row, generation, now) => row with { Generation = generation, UpdatedAt = now },
            ZLinkLocationKind.Peer,
            peer.MeshName));

    public ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Remove(
            _peers,
            ZLinkLocationKeyCodec.EncodePeerKey(key),
            owner,
            static row => row.OwnerId,
            static row => row.Generation,
            ZLinkLocationKind.Peer,
            key.MeshName));

    public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            IReadOnlyList<ZLinkPeerLocation> items = _peers.Rows.Values
                .Where(row => ZLinkLocationFilterMatcher.Matches(row, filter))
                .ToArray();
            return ValueTask.FromResult(items);
        }
    }

    public ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Write(
            _spots,
            ZLinkLocationKeyCodec.EncodeSpotKey(new ZLinkSpotLocationKey(spot.MeshName, spot.SpotRid)),
            spot,
            intent,
            spot.OwnerId,
            spot.Generation,
            static row => row.OwnerId,
            static row => row.Generation,
            static (row, generation, now) => row with { Generation = generation, UpdatedAt = now },
            ZLinkLocationKind.Spot,
            spot.MeshName));

    public ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Remove(
            _spots,
            ZLinkLocationKeyCodec.EncodeSpotKey(key),
            owner,
            static row => row.OwnerId,
            static row => row.Generation,
            ZLinkLocationKind.Spot,
            key.MeshName));

    public ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            _spots.Rows.TryGetValue(ZLinkLocationKeyCodec.EncodeSpotKey(key), out var row);
            return ValueTask.FromResult(row);
        }
    }

    public ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            return ValueTask.FromResult(Page(_spots, row => ZLinkLocationFilterMatcher.Matches(row, filter), page));
        }
    }

    public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Write(
            _actors,
            ZLinkLocationKeyCodec.EncodeActorKey(new ZLinkActorLocationKey(actor.ActorId)),
            actor,
            intent,
            actor.OwnerId,
            actor.Generation,
            static row => row.OwnerId,
            static row => row.Generation,
            static (row, generation, now) => row with { Generation = generation, UpdatedAt = now },
            ZLinkLocationKind.Actor,
            meshName: null));

    public ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Remove(
            _actors,
            ZLinkLocationKeyCodec.EncodeActorKey(key),
            owner,
            static row => row.OwnerId,
            static row => row.Generation,
            ZLinkLocationKind.Actor,
            meshName: null));

    public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            _actors.Rows.TryGetValue(ZLinkLocationKeyCodec.EncodeActorKey(key), out var row);
            return ValueTask.FromResult(row);
        }
    }

    public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            return ValueTask.FromResult(Page(_actors, row => ZLinkLocationFilterMatcher.Matches(row, filter), page));
        }
    }

    public ValueTask<ZLinkLocationWriteResult> UpdateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Write(
            _routes,
            ZLinkLocationKeyCodec.EncodeRouteKey(new ZLinkRouteLocationKey(route.RouteKind, route.RouteKey)),
            route,
            intent,
            route.OwnerId,
            route.Generation,
            static row => row.OwnerId,
            static row => row.Generation,
            static (row, generation, now) => row with { Generation = generation, UpdatedAt = now },
            ZLinkLocationKind.Route,
            meshName: null));

    public ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Remove(
            _routes,
            ZLinkLocationKeyCodec.EncodeRouteKey(key),
            owner,
            static row => row.OwnerId,
            static row => row.Generation,
            ZLinkLocationKind.Route,
            meshName: null));

    public ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            _routes.Rows.TryGetValue(ZLinkLocationKeyCodec.EncodeRouteKey(key), out var row);
            return ValueTask.FromResult(row);
        }
    }

    public ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            return ValueTask.FromResult(Page(_routes, row => ZLinkLocationFilterMatcher.Matches(row, filter), page));
        }
    }

    public ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            var expiresAt = now + leaseTtl;
            _leases[ownerId] = new ZLinkOwnerLease(ownerId, nodeRid, expiresAt, now);
            return ValueTask.FromResult(new ZLinkOwnerLeaseRenewal(expiresAt, now));
        }
    }

    public ValueTask<bool> RemoveOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            return ValueTask.FromResult(_leases.Remove(ownerId));
        }
    }

    public ValueTask<long> RemoveAllByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            var removed = 0L;
            removed += RemoveByOwnerNoLock(
                _peers, ownerId, static row => row.OwnerId, ZLinkLocationKind.Peer,
                static row => row.MeshName);
            removed += RemoveByOwnerNoLock(
                _spots, ownerId, static row => row.OwnerId, ZLinkLocationKind.Spot,
                static row => row.MeshName);
            removed += RemoveByOwnerNoLock(
                _actors, ownerId, static row => row.OwnerId, ZLinkLocationKind.Actor,
                static _ => null);
            removed += RemoveByOwnerNoLock(
                _routes, ownerId, static row => row.OwnerId, ZLinkLocationKind.Route,
                static _ => null);
            return ValueTask.FromResult(removed);
        }
    }

    public ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            return ValueTask.FromResult(new ZLinkOwnerLeaseSnapshot(
                [.. _leases.Values],
                _time.GetUtcNow()));
        }
    }

    public ValueTask<long> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            _stamps.TryGetValue(scope, out var stamp);
            return ValueTask.FromResult(stamp);
        }
    }

    public ValueTask<ZLinkRoutingIdSlotAcquireResult> AcquireRoutingIdSlotAsync(
        ZLinkRoutingIdSlotAcquireRequest request,
        CancellationToken cancellationToken = default)
    {
        ZLinkRoutingIdSlotAllocationValidator.ValidateAcquire(request);
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            var members = NormalizeMembers(request.Members);
            if (!_routingIdGroups.TryGetValue(request.GroupName, out var group))
            {
                var memberNames = members.Select(static member => member.ChannelName)
                    .ToHashSet(StringComparer.Ordinal);
                if (_peers.Rows.Values.Any(peer =>
                        memberNames.Contains(peer.MeshName) && IsOwnerLive(peer.OwnerId, now)))
                    return ValueTask.FromResult<ZLinkRoutingIdSlotAcquireResult>(
                        new ZLinkRoutingIdSlotIdentityModeConflict());

                group = new RoutingIdAllocationGroup(members, request.SlotCount);
                _routingIdGroups.Add(request.GroupName, group);
            }
            else if (group.SlotCount != request.SlotCount
                     || !group.Members.SequenceEqual(members))
            {
                return ValueTask.FromResult<ZLinkRoutingIdSlotAcquireResult>(
                    new ZLinkRoutingIdSlotGroupConfigurationMismatch(
                        group.Members,
                        group.SlotCount,
                        members,
                        request.SlotCount));
            }

            var existing = group.Allocations.Values.FirstOrDefault(allocation =>
                allocation.Owner.OwnerId == request.OwnerId
                && IsOwnerLive(allocation.Owner.OwnerId, now));
            if (existing is not null)
            {
                var renewed = existing with { LeaseExpiresAt = now + request.LeaseTtl, StoreNow = now };
                group.Allocations[renewed.Slot] = renewed;
                RenewAllocationLeaseNoLock(request.OwnerId, request.LeaseTtl, now);
                return ValueTask.FromResult<ZLinkRoutingIdSlotAcquireResult>(
                    new ZLinkRoutingIdSlotAcquired(renewed));
            }

            var slot = Enumerable.Range(1, request.SlotCount).FirstOrDefault(candidate =>
                !group.Allocations.TryGetValue(candidate, out var allocation)
                || !IsOwnerLive(allocation.Owner.OwnerId, now));
            if (slot == 0)
                return ValueTask.FromResult<ZLinkRoutingIdSlotAcquireResult>(
                    new ZLinkRoutingIdSlotGroupExhausted());

            group.Generations.TryGetValue(slot, out var generation);
            generation++;
            group.Generations[slot] = generation;
            var acquired = new ZLinkRoutingIdSlotAllocation(
                slot,
                new ZLinkLocationOwnerToken(request.OwnerId, generation),
                now + request.LeaseTtl,
                now);
            group.Allocations[slot] = acquired;
            RenewAllocationLeaseNoLock(request.OwnerId, request.LeaseTtl, now);
            return ValueTask.FromResult<ZLinkRoutingIdSlotAcquireResult>(
                new ZLinkRoutingIdSlotAcquired(acquired));
        }
    }

    public ValueTask<ZLinkRoutingIdSlotReleaseResult> ReleaseRoutingIdSlotAsync(
        string groupName,
        int slot,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        ZLinkRoutingIdSlotAllocationValidator.ValidateRelease(groupName, slot, owner);
        lock (_gate)
        {
            if (!_routingIdGroups.TryGetValue(groupName, out var group)
                || !group.Allocations.TryGetValue(slot, out var allocation)
                || allocation.Owner != owner)
                return ValueTask.FromResult(ZLinkRoutingIdSlotReleaseResult.IgnoredStale);

            group.Allocations.Remove(slot);
            return ValueTask.FromResult(ZLinkRoutingIdSlotReleaseResult.Released);
        }
    }

    public ValueTask<ZLinkRoutingIdSlotAllocationSnapshot> ListRoutingIdSlotsAsync(
        string groupName,
        CancellationToken cancellationToken = default)
    {
        ZLinkRoutingIdSlotAllocationValidator.ValidateGroupName(groupName);
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            if (!_routingIdGroups.TryGetValue(groupName, out var group))
                return ValueTask.FromResult(new ZLinkRoutingIdSlotAllocationSnapshot(
                    groupName,
                    [],
                    0,
                    [],
                    now));

            var liveAllocations = group.Allocations.Values
                .Where(allocation => IsOwnerLive(allocation.Owner.OwnerId, now))
                .Select(allocation => allocation with
                {
                    LeaseExpiresAt = _leases[allocation.Owner.OwnerId].LeaseExpiresAt,
                    StoreNow = now
                })
                .OrderBy(static allocation => allocation.Slot)
                .ToArray();
            return ValueTask.FromResult(new ZLinkRoutingIdSlotAllocationSnapshot(
                groupName,
                group.Members,
                group.SlotCount,
                liveAllocations,
                now));
        }
    }

    private void RenewAllocationLeaseNoLock(string ownerId, TimeSpan leaseTtl, DateTimeOffset now)
    {
        var nodeRid = _leases.TryGetValue(ownerId, out var current) ? current.NodeRid : default;
        _leases[ownerId] = new ZLinkOwnerLease(ownerId, nodeRid, now + leaseTtl, now);
    }

    private static IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> NormalizeMembers(
        IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> members) =>
        members.OrderBy(static member => member.ChannelName, StringComparer.Ordinal)
            .ThenBy(static member => member.RoutingIdPrefix, StringComparer.Ordinal)
            .ToArray();

    private ZLinkLocationWriteResult Write<TRow>(
        RowTable<TRow> table,
        string key,
        TRow row,
        ZLinkLocationWriteIntent intent,
        string ownerId,
        long generation,
        Func<TRow, string> ownerOf,
        Func<TRow, long> generationOf,
        Func<TRow, long, DateTimeOffset, TRow> finalize,
        ZLinkLocationKind kind,
        string? meshName)
        where TRow : class
    {
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            var exists = table.Rows.TryGetValue(key, out var current);
            switch (intent)
            {
                case ZLinkLocationWriteIntent.NewClaim when exists && IsOwnerLive(ownerOf(current!), now):
                    return ZLinkLocationWriteResult.RejectedConflict;

                case ZLinkLocationWriteIntent.NewClaim:
                case ZLinkLocationWriteIntent.Takeover:
                {
                    // The store issues the fencing generation atomically per
                    // key; counters survive removal so a re-claim can never
                    // reuse an old generation.
                    table.Generations.TryGetValue(key, out var last);
                    var next = last + 1;
                    table.Generations[key] = next;
                    table.Rows[key] = finalize(row, next, now);
                    Bump(kind, meshName);
                    return ZLinkLocationWriteResult.Stored(next, now);
                }

                case ZLinkLocationWriteIntent.Renew
                    when exists && ownerOf(current!) == ownerId && generationOf(current!) == generation:
                    table.Rows[key] = finalize(row, generation, now);
                    Bump(kind, meshName);
                    return ZLinkLocationWriteResult.Stored(generation, now);

                default:
                    return ZLinkLocationWriteResult.IgnoredStale;
            }
        }
    }

    private ZLinkLocationWriteResult Remove<TRow>(
        RowTable<TRow> table,
        string key,
        ZLinkLocationOwnerToken owner,
        Func<TRow, string> ownerOf,
        Func<TRow, long> generationOf,
        ZLinkLocationKind kind,
        string? meshName)
        where TRow : class
    {
        lock (_gate)
        {
            if (!table.Rows.TryGetValue(key, out var current)
                || ownerOf(current) != owner.OwnerId
                || generationOf(current) != owner.Generation)
            {
                return ZLinkLocationWriteResult.IgnoredStale;
            }

            table.Rows.Remove(key);
            Bump(kind, meshName);
            return ZLinkLocationWriteResult.Stored(owner.Generation, _time.GetUtcNow());
        }
    }

    private long RemoveByOwnerNoLock<TRow>(
        RowTable<TRow> table,
        string ownerId,
        Func<TRow, string> ownerOf,
        ZLinkLocationKind kind,
        Func<TRow, string?> meshOf)
        where TRow : class
    {
        var removedKeys = table.Rows
            .Where(pair => ownerOf(pair.Value) == ownerId)
            .Select(pair => pair.Key)
            .ToArray();
        foreach (var key in removedKeys)
        {
            var mesh = meshOf(table.Rows[key]);
            table.Rows.Remove(key);
            Bump(kind, mesh);
        }

        return removedKeys.Length;
    }

    private ZLinkLocationPage<TRow> Page<TRow>(
        RowTable<TRow> table,
        Func<TRow, bool> matches,
        ZLinkPageRequest page)
        where TRow : class
    {
        var ordered = table.Rows
            .Where(pair => matches(pair.Value))
            .OrderBy(pair => pair.Key, StringComparer.Ordinal)
            .Select(pair => pair.Value)
            .ToArray();

        var offset = 0;
        if (page.ContinuationToken is { } token
            && int.TryParse(token, NumberStyles.None, CultureInfo.InvariantCulture, out var parsed))
        {
            offset = parsed;
        }

        // A page size of zero or less means the caller did not pick one; the
        // framework layer substitutes the configured default before calling
        // the store, so the raw store treats it as unbounded.
        var size = page.PageSize > 0 ? page.PageSize : int.MaxValue;
        var items = ordered.Skip(offset).Take(size).ToArray();
        var nextOffset = offset + items.Length;
        var next = nextOffset < ordered.Length
            ? nextOffset.ToString(CultureInfo.InvariantCulture)
            : null;
        return new ZLinkLocationPage<TRow>(items, next);
    }

    private bool IsOwnerLive(string ownerId, DateTimeOffset now) =>
        _leases.TryGetValue(ownerId, out var lease) && lease.LeaseExpiresAt > now;

    private void Bump(ZLinkLocationKind kind, string? meshName)
    {
        BumpScope(new ZLinkLocationChangeStampScope(kind, meshName));
        if (meshName is not null)
        {
            BumpScope(new ZLinkLocationChangeStampScope(kind, null));
        }
    }

    private void BumpScope(ZLinkLocationChangeStampScope scope)
    {
        _stamps.TryGetValue(scope, out var stamp);
        _stamps[scope] = stamp + 1;
    }

    private sealed class RowTable<TRow>
        where TRow : class
    {
        public Dictionary<string, TRow> Rows { get; } = [];

        public Dictionary<string, long> Generations { get; } = [];
    }

    private sealed class RoutingIdAllocationGroup(
        IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> members,
        int slotCount)
    {
        public IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> Members { get; } = members;

        public int SlotCount { get; } = slotCount;

        public Dictionary<int, ZLinkRoutingIdSlotAllocation> Allocations { get; } = [];

        public Dictionary<int, long> Generations { get; } = [];
    }
}
