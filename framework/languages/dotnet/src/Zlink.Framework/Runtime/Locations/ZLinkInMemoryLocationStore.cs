using System.Globalization;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Single-process store for local development, unit tests, and sample smoke
/// tests. It backs every location store interface plus the owner lease store
/// in one instance, which satisfies the contract requirement that location
/// rows and owner leases share one physical store. Never use it for
/// production topologies where processes must share location data.
/// </summary>
internal sealed partial class ZLinkInMemoryLocationStore :
    IZLinkLocationStore,
    IZLinkRoutingIdSlotAllocationStore,
    IZLinkLocationChangeStampStore
{
    private readonly object _gate = new();
    private readonly TimeProvider _time;
    private readonly Dictionary<string, ZLinkOwnerLease> _leases = [];
    private readonly RowTable<ZLinkMeshNodeDescriptor> _meshNodes = new();
    private readonly RowTable<ZLinkSpotLocation> _spots = new();
    private readonly RowTable<ZLinkActorLocation> _actors = new();
    private readonly Dictionary<ZLinkLocationChangeStampScope, ulong> _stamps = [];
    private readonly Dictionary<string, RoutingIdAllocationGroup> _routingIdGroups =
        new(StringComparer.Ordinal);

    public ZLinkInMemoryLocationStore(TimeProvider? timeProvider = null)
    {
        _time = timeProvider ?? TimeProvider.System;
    }

    public ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Write(
            _meshNodes,
            ZLinkLocationKeyCodec.EncodeMeshNodeKey(
                new ZLinkMeshNodeDescriptorKey(descriptor.MeshName, descriptor.Rid)),
            descriptor,
            intent,
            descriptor.OwnerId,
            static row => row.OwnerId,
            static (row, now, generation) => row with { UpdatedAt = now },
            ZLinkLocationChangeScopeKind.MeshNode,
            descriptor.MeshName));

    public ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Remove(
            _meshNodes,
            ZLinkLocationKeyCodec.EncodeMeshNodeKey(key),
            owner,
            static row => row.OwnerId,
            ZLinkLocationChangeScopeKind.MeshNode,
            key.MeshName));

    public ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            IReadOnlyList<ZLinkMeshNodeDescriptor> items = _meshNodes.Rows.Values
                .Where(row => string.Equals(row.MeshName, meshName, StringComparison.Ordinal))
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
            static row => row.OwnerId,
            static (row, now, generation) => row with { UpdatedAt = now },
            ZLinkLocationChangeScopeKind.Spot,
            spot.MeshName));

    public ValueTask<ZLinkLocationWriteStatus> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Remove(
            _spots,
            ZLinkLocationKeyCodec.EncodeSpotKey(key),
            owner,
            static row => row.OwnerId,
            ZLinkLocationChangeScopeKind.Spot,
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

    public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Write(
            _actors,
            ZLinkLocationKeyCodec.EncodeActorKey(
                new ZLinkActorLocationKey(actor.MeshName, actor.ActorId)),
            actor,
            intent,
            actor.OwnerId,
            static row => row.OwnerId,
            static (row, now, generation) => row with { UpdatedAt = now },
            ZLinkLocationChangeScopeKind.Actor,
            actor.MeshName));

    public ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Remove(
            _actors,
            ZLinkLocationKeyCodec.EncodeActorKey(key),
            owner,
            static row => row.OwnerId,
            ZLinkLocationChangeScopeKind.Actor,
            key.MeshName));

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
                _meshNodes, ownerId, static row => row.OwnerId, ZLinkLocationChangeScopeKind.MeshNode,
                static row => row.MeshName);
            removed += RemoveByOwnerNoLock(
                _spots, ownerId, static row => row.OwnerId, ZLinkLocationChangeScopeKind.Spot,
                static row => row.MeshName);
            removed += RemoveByOwnerNoLock(
                _actors, ownerId, static row => row.OwnerId, ZLinkLocationChangeScopeKind.Actor,
                static row => row.MeshName);
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

    public ValueTask<ulong> GetChangeStampAsync(
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
        Func<TRow, string> ownerOf,
        Func<TRow, DateTimeOffset, ulong, TRow> finalize,
        ZLinkLocationChangeScopeKind kind,
        string? meshName)
        where TRow : class
    {
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            var exists = table.Rows.TryGetValue(key, out var current);
            table.Generations.TryGetValue(key, out var last);
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
                    var next = last + 1;
                    table.Generations[key] = next;
                    table.Rows[key] = finalize(row, now, next);
                    Bump(kind, meshName);
                    return ZLinkLocationWriteResult.Stored(next, now);
                }

                case ZLinkLocationWriteIntent.Renew
                    when exists && ownerOf(current!) == ownerId:
                    table.Rows[key] = finalize(row, now, last);
                    Bump(kind, meshName);
                    return ZLinkLocationWriteResult.Stored(last, now);

                default:
                    return ZLinkLocationWriteResult.IgnoredStale;
            }
        }
    }

    private ZLinkLocationWriteStatus Remove<TRow>(
        RowTable<TRow> table,
        string key,
        ZLinkLocationOwnerToken owner,
        Func<TRow, string> ownerOf,
        ZLinkLocationChangeScopeKind kind,
        string? meshName)
        where TRow : class
    {
        lock (_gate)
        {
            table.Generations.TryGetValue(key, out var current);
            if (!table.Rows.TryGetValue(key, out var row)
                || ownerOf(row) != owner.OwnerId
                || current != owner.Generation)
            {
                return ZLinkLocationWriteStatus.IgnoredStale;
            }

            table.Rows.Remove(key);
            Bump(kind, meshName);
            return ZLinkLocationWriteStatus.Stored;
        }
    }

    private long RemoveByOwnerNoLock<TRow>(
        RowTable<TRow> table,
        string ownerId,
        Func<TRow, string> ownerOf,
        ZLinkLocationChangeScopeKind kind,
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

    private bool IsOwnerLive(string ownerId, DateTimeOffset now) =>
        _leases.TryGetValue(ownerId, out var lease) && lease.LeaseExpiresAt > now;

    private void Bump(ZLinkLocationChangeScopeKind kind, string? meshName)
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

        public Dictionary<string, ulong> Generations { get; } = [];
    }

    private sealed class RoutingIdAllocationGroup(
        IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> members,
        int slotCount)
    {
        public IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> Members { get; } = members;

        public int SlotCount { get; } = slotCount;

        public Dictionary<int, ZLinkRoutingIdSlotAllocation> Allocations { get; } = [];

        public Dictionary<int, ulong> Generations { get; } = [];
    }
}
