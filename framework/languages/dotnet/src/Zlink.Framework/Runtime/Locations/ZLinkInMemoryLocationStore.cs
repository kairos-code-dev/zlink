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
    IZLinkInstanceSpotLocationStore,
    IZLinkRoutingIdSlotAllocationStore,
    IZLinkLocationChangeStampStore
{
    private readonly object _gate = new();
    private readonly TimeProvider _time;
    private readonly Dictionary<string, ZLinkOwnerLease> _leases = [];
    private long _ownerLeaseGeneration;
    private readonly RowTable<ZLinkMeshNodeDescriptor> _meshNodes = new();
    private readonly RowTable<ZLinkSpotLocation> _spots = new();
    private readonly RowTable<InstanceSpotLocation> _instanceSpots = new();
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
            static (row, now, generation) => row with
            {
                UpdatedAt = now,
                AuthorityOwnerGeneration = generation
            },
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

    public ValueTask<InstanceSpotClaimResult> ClaimInstanceSpotAsync(
        InstanceSpotClaimRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            if (!_leases.TryGetValue(request.OwnerId, out var claimantLease)
                || claimantLease.LeaseExpiresAt <= now)
                return ValueTask.FromResult<InstanceSpotClaimResult>(
                    new InstanceSpotClaimResult.Conflict());

            var key = ZLinkLocationKeyCodec.EncodeSpotKey(
                new ZLinkSpotLocationKey(request.MeshName, request.SpotRid));
            if (_instanceSpots.Rows.TryGetValue(key, out var current)
                && _leases.TryGetValue(current.OwnerId, out var currentLease)
                && currentLease.LeaseExpiresAt > now)
                return ValueTask.FromResult<InstanceSpotClaimResult>(
                    new InstanceSpotClaimResult.Existing(
                        new InstanceSpotSnapshot(
                            current,
                            new InstanceSpotLeaseSnapshot(currentLease.LeaseExpiresAt, now))));

            _instanceSpots.Generations.TryGetValue(key, out var previousGeneration);
            var generation = checked(previousGeneration + 1);
            var activationEpoch = current is null
                ? 1UL
                : checked(current.ActivationEpoch + 1);
            var claimed = new InstanceSpotLocation(
                request.MeshName,
                request.SpotRid,
                0,
                request.TargetNodeRid,
                request.TargetNodeGeneration,
                request.InstanceSpotType,
                ZLinkSpotActivationState.Activating,
                activationEpoch,
                request.OwnerId,
                generation,
                now);
            _instanceSpots.Rows[key] = claimed;
            _instanceSpots.Generations[key] = generation;
            Bump(ZLinkLocationChangeScopeKind.Spot, request.MeshName);
            return ValueTask.FromResult<InstanceSpotClaimResult>(
                new InstanceSpotClaimResult.Claimed(
                    new InstanceSpotSnapshot(
                        claimed,
                        new InstanceSpotLeaseSnapshot(claimantLease.LeaseExpiresAt, now))));
        }
    }

    public ValueTask<InstanceSpotWriteResult> CommitInstanceSpotReadyAsync(
        InstanceSpotFence fence,
        ulong spotGeneration,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(fence);
        if (spotGeneration == 0) throw new ArgumentOutOfRangeException(nameof(spotGeneration));
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            var key = ZLinkLocationKeyCodec.EncodeSpotKey(
                new ZLinkSpotLocationKey(fence.MeshName, fence.SpotRid));
            if (!_instanceSpots.Rows.TryGetValue(key, out var current)
                || !MatchesFence(current, fence))
                return ValueTask.FromResult<InstanceSpotWriteResult>(
                    new InstanceSpotWriteResult.Stale());
            if (current.ActivationState != ZLinkSpotActivationState.Activating)
                return ValueTask.FromResult<InstanceSpotWriteResult>(
                    new InstanceSpotWriteResult.Conflict());
            if (!_leases.TryGetValue(current.OwnerId, out var lease)
                || lease.LeaseExpiresAt <= now)
                return ValueTask.FromResult<InstanceSpotWriteResult>(
                    new InstanceSpotWriteResult.Stale());

            var ready = current with
            {
                SpotGeneration = spotGeneration,
                ActivationState = ZLinkSpotActivationState.Ready,
                UpdatedAt = now
            };
            _instanceSpots.Rows[key] = ready;
            Bump(ZLinkLocationChangeScopeKind.Spot, fence.MeshName);
            return ValueTask.FromResult<InstanceSpotWriteResult>(
                new InstanceSpotWriteResult.Stored(
                    new InstanceSpotSnapshot(
                        ready,
                        new InstanceSpotLeaseSnapshot(lease.LeaseExpiresAt, now))));
        }
    }

    public ValueTask<ZLinkLocationWriteResult> BeginInstanceSpotClosingAsync(
        InstanceSpotFence fence,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(fence);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            var key = ZLinkLocationKeyCodec.EncodeSpotKey(
                new ZLinkSpotLocationKey(fence.MeshName, fence.SpotRid));
            if (!_instanceSpots.Rows.TryGetValue(key, out var current)
                || !MatchesFence(current, fence))
                return ValueTask.FromResult(ZLinkLocationWriteResult.IgnoredStale);

            _instanceSpots.Rows[key] = current with
            {
                ActivationState = ZLinkSpotActivationState.Closing,
                UpdatedAt = now
            };
            Bump(ZLinkLocationChangeScopeKind.Spot, fence.MeshName);
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(
                current.LocationGeneration, now));
        }
    }

    public ValueTask<ZLinkLocationWriteStatus> ReleaseInstanceSpotAsync(
        InstanceSpotFence fence,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(fence);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var key = ZLinkLocationKeyCodec.EncodeSpotKey(
                new ZLinkSpotLocationKey(fence.MeshName, fence.SpotRid));
            if (!_instanceSpots.Rows.TryGetValue(key, out var current)
                || !MatchesFence(current, fence))
                return ValueTask.FromResult(ZLinkLocationWriteStatus.IgnoredStale);

            _instanceSpots.Rows.Remove(key);
            Bump(ZLinkLocationChangeScopeKind.Spot, fence.MeshName);
            return ValueTask.FromResult(ZLinkLocationWriteStatus.Stored);
        }
    }

    public ValueTask<InstanceSpotResolveResult> ResolveInstanceSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            if (!_instanceSpots.Rows.TryGetValue(
                    ZLinkLocationKeyCodec.EncodeSpotKey(key), out var current)
                || !_leases.TryGetValue(current.OwnerId, out var lease)
                || lease.LeaseExpiresAt <= now)
                return ValueTask.FromResult<InstanceSpotResolveResult>(
                    new InstanceSpotResolveResult.Missing());

            return ValueTask.FromResult<InstanceSpotResolveResult>(
                new InstanceSpotResolveResult.Found(
                    new InstanceSpotSnapshot(
                        current,
                        new InstanceSpotLeaseSnapshot(lease.LeaseExpiresAt, now))));
        }
    }

    private static bool MatchesFence(
        InstanceSpotLocation location,
        InstanceSpotFence fence) =>
        string.Equals(location.MeshName, fence.MeshName, StringComparison.Ordinal)
        && location.SpotRid == fence.SpotRid
        && string.Equals(location.OwnerId, fence.OwnerId, StringComparison.Ordinal)
        && location.OwnerNodeGeneration == fence.OwnerNodeGeneration
        && location.LocationGeneration == fence.LocationGeneration
        && location.ActivationEpoch == fence.ActivationEpoch;

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
            static (row, now, generation) => row with
            {
                UpdatedAt = now,
                AuthorityOwnerGeneration = generation
            },
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
            var generation = _leases.TryGetValue(ownerId, out var current)
                ? current.LeaseGeneration
                : NextOwnerLeaseGeneration();
            _leases[ownerId] = new ZLinkOwnerLease(ownerId, nodeRid, expiresAt, now)
            {
                LeaseGeneration = generation
            };
            return ValueTask.FromResult(new ZLinkOwnerLeaseRenewal(expiresAt, now));
        }
    }

    public ValueTask<ZLinkOwnerLeaseClaimResult> ClaimOwnerLeaseAsync(
        string ownerId,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        ValidateOwnerLeaseArguments(ownerId, leaseTtl);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            if (_leases.TryGetValue(ownerId, out var current)
                && current.LeaseExpiresAt > now)
                return ValueTask.FromResult<ZLinkOwnerLeaseClaimResult>(
                    new ZLinkOwnerLeaseClaimResult.Conflict());
            if (_ownerLeaseGeneration == long.MaxValue)
                return ValueTask.FromResult<ZLinkOwnerLeaseClaimResult>(
                    new ZLinkOwnerLeaseClaimResult.GenerationExhausted());

            var token = new ZLinkLocationOwnerToken(
                ownerId,
                ++_ownerLeaseGeneration);
            var expiresAt = now + leaseTtl;
            _leases[ownerId] = new ZLinkOwnerLease(
                ownerId,
                default,
                expiresAt,
                now)
            {
                LeaseGeneration = token.LeaseGeneration
            };
            return ValueTask.FromResult<ZLinkOwnerLeaseClaimResult>(
                new ZLinkOwnerLeaseClaimResult.Claimed(
                    token,
                    expiresAt,
                    now));
        }
    }

    public ValueTask<ZLinkOwnerLeaseReadResult> ReadOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(ownerId);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            if (!_leases.TryGetValue(ownerId, out var lease)
                || lease.LeaseExpiresAt <= now)
                return ValueTask.FromResult<ZLinkOwnerLeaseReadResult>(
                    new ZLinkOwnerLeaseReadResult.Missing());
            return ValueTask.FromResult<ZLinkOwnerLeaseReadResult>(
                new ZLinkOwnerLeaseReadResult.Found(
                    new ZLinkLocationOwnerToken(
                        ownerId,
                        lease.LeaseGeneration),
                    lease.LeaseExpiresAt,
                    now));
        }
    }

    public ValueTask<ZLinkOwnerLeaseRenewResult> RenewOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        ValidateOwnerLeaseArguments(token.OwnerId, leaseTtl);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            if (!MatchesLiveOwnerLease(token, now))
                return ValueTask.FromResult<ZLinkOwnerLeaseRenewResult>(
                    new ZLinkOwnerLeaseRenewResult.Stale());
            var current = _leases[token.OwnerId];
            var expiresAt = now + leaseTtl;
            _leases[token.OwnerId] = current with
            {
                LeaseExpiresAt = expiresAt,
                UpdatedAt = now
            };
            return ValueTask.FromResult<ZLinkOwnerLeaseRenewResult>(
                new ZLinkOwnerLeaseRenewResult.Renewed(expiresAt, now));
        }
    }

    public ValueTask<ZLinkOwnerLeaseReleaseResult> ReleaseOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(token.OwnerId);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!MatchesLiveOwnerLease(token, _time.GetUtcNow()))
                return ValueTask.FromResult(ZLinkOwnerLeaseReleaseResult.Stale);
            _leases.Remove(token.OwnerId);
            return ValueTask.FromResult(ZLinkOwnerLeaseReleaseResult.Released);
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
                _instanceSpots, ownerId, static row => row.OwnerId,
                ZLinkLocationChangeScopeKind.Spot, static row => row.MeshName);
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
                new ZLinkLocationOwnerToken(
                    request.OwnerId,
                    checked((long)generation)),
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
        members.OrderBy(static member => member.MeshName, StringComparer.Ordinal)
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
                || current != checked((ulong)owner.LeaseGeneration))
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

    private bool MatchesLiveOwnerLease(
        ZLinkLocationOwnerToken token,
        DateTimeOffset now) =>
        _leases.TryGetValue(token.OwnerId, out var lease)
        && lease.LeaseExpiresAt > now
        && lease.LeaseGeneration == token.LeaseGeneration;

    private long NextOwnerLeaseGeneration()
    {
        if (_ownerLeaseGeneration == long.MaxValue)
            throw new InvalidOperationException(
                "The owner lease generation space was exhausted.");
        return ++_ownerLeaseGeneration;
    }

    private static void ValidateOwnerLeaseArguments(
        string ownerId,
        TimeSpan leaseTtl)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(ownerId);
        if (leaseTtl <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(leaseTtl));
    }

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
