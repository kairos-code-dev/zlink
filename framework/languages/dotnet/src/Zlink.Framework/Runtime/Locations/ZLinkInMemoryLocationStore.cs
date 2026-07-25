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
    IZLinkClientServerLocationStore,
    IZLinkFanoutLocationStore,
    IZLinkInstanceSpotLocationStore,
    IZLinkRoutingIdSlotAllocationStore,
    IZLinkLocationChangeStampStore
{
    private readonly object _gate = new();
    private readonly TimeProvider _time;
    private readonly Dictionary<string, ZLinkOwnerLease> _leases = [];
    private long _ownerLeaseGeneration;
    private readonly RowTable<ZLinkMeshNodeDescriptor> _meshNodes = new();
    private readonly Dictionary<string, EntrySpotIdClaim> _entrySpotIdClaims =
        new(StringComparer.Ordinal);
    private readonly RowTable<ZLinkClientServerServerDescriptor> _clientServers = new();
    private readonly RowTable<ZLinkFanoutPublisherDescriptor> _fanoutPublishers = new();
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
        CancellationToken cancellationToken = default)
    {
        ValidateMeshNodeDescriptor(descriptor);
        descriptor = CanonicalizeMeshNodeDescriptor(descriptor);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            var owner = new ZLinkLocationOwnerToken(
                descriptor.OwnerId,
                descriptor.LeaseGeneration);
            if (!MatchesLiveOwnerLease(owner, now))
                return ValueTask.FromResult(
                    ZLinkLocationWriteResult.IgnoredStale);

            var key = ZLinkLocationKeyCodec.EncodeMeshNodeKey(
                new ZLinkMeshNodeDescriptorKey(
                    descriptor.MeshName,
                    descriptor.Rid));
            var exists = _meshNodes.Rows.TryGetValue(key, out var current);
            var currentOwnerLive = exists
                                   && MatchesLiveOwnerLease(
                                       new ZLinkLocationOwnerToken(
                                           current!.OwnerId,
                                           current.LeaseGeneration),
                                       now);
            if (intent == ZLinkLocationWriteIntent.NewClaim && currentOwnerLive)
                return ValueTask.FromResult(
                    ZLinkLocationWriteResult.RejectedConflict);
            if (intent == ZLinkLocationWriteIntent.Takeover && currentOwnerLive)
                return ValueTask.FromResult(
                    ZLinkLocationWriteResult.IgnoredStale);
            if (intent == ZLinkLocationWriteIntent.Renew)
            {
                if (!exists
                    || current!.OwnerId != descriptor.OwnerId
                    || current.LeaseGeneration != descriptor.LeaseGeneration
                    || current.LifecycleGeneration
                    != descriptor.LifecycleGeneration
                    || !MeshNodeImmutableFieldsEqual(current, descriptor)
                    || descriptor.DescriptorRevision
                    <= current.DescriptorRevision)
                    return ValueTask.FromResult(
                        ZLinkLocationWriteResult.IgnoredStale);
            }
            if (!CanPublishEntrySpotIdNoLock(descriptor, key, now))
                return ValueTask.FromResult(
                    ZLinkLocationWriteResult.RejectedConflict);

            _meshNodes.Generations.TryGetValue(key, out var last);
            var generation = intent == ZLinkLocationWriteIntent.Renew
                ? last
                : checked(last + 1);
            _meshNodes.Generations[key] = generation;
            _meshNodes.Rows[key] = WithCurrentPlacementCapacity(
                descriptor with { UpdatedAt = now });
            PublishEntrySpotIdNoLock(current, descriptor, key);
            Bump(ZLinkLocationChangeScopeKind.MeshNode, descriptor.MeshName);
            return ValueTask.FromResult(
                ZLinkLocationWriteResult.Stored(generation, now));
        }
    }

    public ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var canonicalKey = ZLinkLocationKeyCodec.EncodeMeshNodeKey(key);
        lock (_gate)
        {
            _meshNodes.Generations.TryGetValue(canonicalKey, out var generation);
            if (!_meshNodes.Rows.TryGetValue(canonicalKey, out var row)
                || row.OwnerId != owner.OwnerId
                || generation != checked((ulong)owner.LeaseGeneration))
                return ValueTask.FromResult(
                    ZLinkLocationWriteStatus.IgnoredStale);

            _meshNodes.Rows.Remove(canonicalKey);
            RemoveEntrySpotIdClaimNoLock(row, canonicalKey);
            Bump(ZLinkLocationChangeScopeKind.MeshNode, key.MeshName);
            return ValueTask.FromResult(ZLinkLocationWriteStatus.Stored);
        }
    }

    public ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            IReadOnlyList<ZLinkMeshNodeDescriptor> items = _meshNodes.Rows.Values
                .Where(row => string.Equals(row.MeshName, meshName, StringComparison.Ordinal))
                .Select(WithCurrentPlacementCapacity)
                .ToArray();
            return ValueTask.FromResult(items);
        }
    }

    public ValueTask<ZLinkLocationWriteResult> UpdateClientServerAsync(
        ZLinkClientServerServerDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.ChannelName);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.Endpoint);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.OwnerId);
        if (descriptor.ServerRid.Size == 0
            || descriptor.LifecycleGeneration == 0
            || descriptor.DescriptorRevision == 0
            || descriptor.Weight is < 0 or > ZLinkSocketConfig.MaximumPeerWeight)
            throw new ArgumentOutOfRangeException(nameof(descriptor));
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            var owner = new ZLinkLocationOwnerToken(
                descriptor.OwnerId,
                descriptor.LeaseGeneration);
            if (!MatchesLiveOwnerLease(owner, now))
                return ValueTask.FromResult(ZLinkLocationWriteResult.IgnoredStale);

            var key = ClientServerKey(descriptor.ChannelName, descriptor.ServerRid);
            var exists = _clientServers.Rows.TryGetValue(key, out var current);
            var currentOwnerLive = exists
                                   && MatchesLiveOwnerLease(
                                       new ZLinkLocationOwnerToken(
                                           current!.OwnerId,
                                           current.LeaseGeneration),
                                       now);
            if (intent == ZLinkLocationWriteIntent.NewClaim && currentOwnerLive)
                return ValueTask.FromResult(ZLinkLocationWriteResult.RejectedConflict);
            if (intent == ZLinkLocationWriteIntent.Takeover && currentOwnerLive)
                return ValueTask.FromResult(ZLinkLocationWriteResult.IgnoredStale);
            if (intent == ZLinkLocationWriteIntent.Renew
                && (!exists
                    || current!.OwnerId != descriptor.OwnerId
                    || current.LeaseGeneration != descriptor.LeaseGeneration
                    || current.LifecycleGeneration != descriptor.LifecycleGeneration
                    || current.Endpoint != descriptor.Endpoint
                    || current.SecurityIdentity != descriptor.SecurityIdentity
                    || descriptor.DescriptorRevision <= current.DescriptorRevision))
                return ValueTask.FromResult(ZLinkLocationWriteResult.IgnoredStale);

            _clientServers.Generations.TryGetValue(key, out var last);
            var generation = intent == ZLinkLocationWriteIntent.Renew
                ? last
                : checked(last + 1);
            _clientServers.Generations[key] = generation;
            _clientServers.Rows[key] = descriptor with { UpdatedAt = now };
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(generation, now));
        }
    }

    public ValueTask<ZLinkLocationWriteStatus> RemoveClientServerAsync(
        ZLinkClientServerServerDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var encoded = ClientServerKey(key.ChannelName, key.ServerRid);
            if (!_clientServers.Rows.TryGetValue(encoded, out var current)
                || current.OwnerId != owner.OwnerId
                || current.LeaseGeneration != owner.LeaseGeneration)
                return ValueTask.FromResult(ZLinkLocationWriteStatus.IgnoredStale);
            _clientServers.Rows.Remove(encoded);
            return ValueTask.FromResult(ZLinkLocationWriteStatus.Stored);
        }
    }

    public ValueTask<ZLinkLocationPage<ZLinkClientServerServerDescriptor>>
        ListClientServersAsync(
            string channelName,
            ZLinkPageRequest page,
            CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var pageSize = page.PageSize <= 0 ? 256 : page.PageSize;
            var offset = page.ContinuationToken is { } token
                         && int.TryParse(token, out var parsed)
                ? parsed
                : 0;
            var rows = _clientServers.Rows.Values
                .Where(row => StringComparer.Ordinal.Equals(
                    row.ChannelName,
                    channelName))
                .OrderBy(static row => row.ServerRid.ToHex(), StringComparer.Ordinal)
                .ToArray();
            var items = rows.Skip(offset).Take(pageSize).ToArray();
            var next = offset + items.Length < rows.Length
                ? (offset + items.Length).ToString(CultureInfo.InvariantCulture)
                : null;
            return ValueTask.FromResult(
                new ZLinkLocationPage<ZLinkClientServerServerDescriptor>(
                    items,
                    next));
        }
    }

    private static string ClientServerKey(string channelName, RoutingId rid) =>
        $"{channelName}\u001f{rid.ToHex()}";

    public ValueTask<ZLinkLocationWriteResult> UpdateFanoutPublisherAsync(
        ZLinkFanoutPublisherDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.ChannelName);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.Endpoint);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.OwnerId);
        if (descriptor.PublisherRid.Size == 0
            || descriptor.LifecycleGeneration == 0
            || descriptor.DescriptorRevision == 0)
            throw new ArgumentOutOfRangeException(nameof(descriptor));
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            var owner = new ZLinkLocationOwnerToken(
                descriptor.OwnerId,
                descriptor.LeaseGeneration);
            if (!MatchesLiveOwnerLease(owner, now))
                return ValueTask.FromResult(ZLinkLocationWriteResult.IgnoredStale);

            var key = FanoutKey(descriptor.ChannelName, descriptor.PublisherRid);
            var exists = _fanoutPublishers.Rows.TryGetValue(key, out var current);
            var currentOwnerLive = exists
                                   && MatchesLiveOwnerLease(
                                       new ZLinkLocationOwnerToken(
                                           current!.OwnerId,
                                           current.LeaseGeneration),
                                       now);
            if (intent == ZLinkLocationWriteIntent.NewClaim && currentOwnerLive)
                return ValueTask.FromResult(ZLinkLocationWriteResult.RejectedConflict);
            if (intent == ZLinkLocationWriteIntent.Takeover && currentOwnerLive)
                return ValueTask.FromResult(ZLinkLocationWriteResult.IgnoredStale);
            if (intent == ZLinkLocationWriteIntent.Renew
                && (!exists
                    || current!.OwnerId != descriptor.OwnerId
                    || current.LeaseGeneration != descriptor.LeaseGeneration
                    || current.LifecycleGeneration != descriptor.LifecycleGeneration
                    || current.Endpoint != descriptor.Endpoint
                    || current.SecurityIdentity != descriptor.SecurityIdentity
                    || descriptor.DescriptorRevision <= current.DescriptorRevision))
                return ValueTask.FromResult(ZLinkLocationWriteResult.IgnoredStale);

            _fanoutPublishers.Generations.TryGetValue(key, out var last);
            var generation = intent == ZLinkLocationWriteIntent.Renew
                ? last
                : checked(last + 1);
            _fanoutPublishers.Generations[key] = generation;
            _fanoutPublishers.Rows[key] = descriptor with { UpdatedAt = now };
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(generation, now));
        }
    }

    public ValueTask<ZLinkLocationWriteStatus> RemoveFanoutPublisherAsync(
        ZLinkFanoutPublisherDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var encoded = FanoutKey(key.ChannelName, key.PublisherRid);
            if (!_fanoutPublishers.Rows.TryGetValue(encoded, out var current)
                || current.OwnerId != owner.OwnerId
                || current.LeaseGeneration != owner.LeaseGeneration)
                return ValueTask.FromResult(ZLinkLocationWriteStatus.IgnoredStale);
            _fanoutPublishers.Rows.Remove(encoded);
            return ValueTask.FromResult(ZLinkLocationWriteStatus.Stored);
        }
    }

    public ValueTask<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>>
        ListFanoutPublishersAsync(
            string channelName,
            ZLinkPageRequest page,
            CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var pageSize = page.PageSize <= 0 ? 256 : page.PageSize;
            var offset = page.ContinuationToken is { } token
                         && int.TryParse(token, out var parsed)
                ? parsed
                : 0;
            var rows = _fanoutPublishers.Rows.Values
                .Where(row => StringComparer.Ordinal.Equals(
                    row.ChannelName,
                    channelName))
                .OrderBy(static row => row.PublisherRid.ToHex(), StringComparer.Ordinal)
                .ToArray();
            var items = rows.Skip(offset).Take(pageSize).ToArray();
            var next = offset + items.Length < rows.Length
                ? (offset + items.Length).ToString(CultureInfo.InvariantCulture)
                : null;
            return ValueTask.FromResult(
                new ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>(
                    items,
                    next));
        }
    }

    private static string FanoutKey(string channelName, RoutingId rid) =>
        $"{channelName}\u001f{rid.ToHex()}";

    private ZLinkMeshNodeDescriptor WithCurrentPlacementCapacity(
        ZLinkMeshNodeDescriptor descriptor)
    {
        var key = new ZLinkMeshNodeDescriptorKey(
            descriptor.MeshName,
            descriptor.Rid);
        var actorActive = PlacementCapacityUsage(
            _activePlacementCapacity,
            key,
            descriptor.LifecycleGeneration,
            ZLinkPlacementObjectKind.Actor);
        var actorReserved = PlacementCapacityUsage(
            _pendingPlacementCapacity,
            key,
            descriptor.LifecycleGeneration,
            ZLinkPlacementObjectKind.Actor);
        var spotActive = PlacementCapacityUsage(
            _activePlacementCapacity,
            key,
            descriptor.LifecycleGeneration,
            ZLinkPlacementObjectKind.UserSpot,
            ZLinkPlacementObjectKind.InstanceSpot);
        var spotReserved = PlacementCapacityUsage(
            _pendingPlacementCapacity,
            key,
            descriptor.LifecycleGeneration,
            ZLinkPlacementObjectKind.UserSpot,
            ZLinkPlacementObjectKind.InstanceSpot);
        var spotTypes = descriptor.ObjectCapabilities
            .Where(static capability =>
                capability.ObjectKind is ZLinkPlacementObjectKind.UserSpot
                    or ZLinkPlacementObjectKind.InstanceSpot)
            .Select(capability =>
            {
                var typeKey = new PlacementCapacityKey(
                    key,
                    descriptor.LifecycleGeneration,
                    capability.ObjectKind,
                    capability.StableType);
                return new ZLinkSpotTypeCapacity(
                    capability.ObjectKind,
                    capability.StableType,
                    checked((int)_activePlacementCapacity
                        .GetValueOrDefault(typeKey)),
                    checked((int)_pendingPlacementCapacity
                        .GetValueOrDefault(typeKey)),
                    capability.Limit);
            })
            .ToArray();
        return descriptor with
        {
            Capacity = new ZLinkPlacementCapacity(
                descriptor.Capacity.Actors with
                {
                    Active = checked((int)actorActive),
                    Reserved = checked((int)actorReserved)
                },
                descriptor.Capacity.Spots with
                {
                    Active = checked((int)spotActive),
                    Reserved = checked((int)spotReserved)
                },
                spotTypes)
        };
    }

    private static bool MeshNodeImmutableFieldsEqual(
        ZLinkMeshNodeDescriptor current,
        ZLinkMeshNodeDescriptor incoming) =>
        current.MeshName == incoming.MeshName
        && current.Rid == incoming.Rid
        && current.LifecycleGeneration == incoming.LifecycleGeneration
        && current.Endpoint == incoming.Endpoint
        && current.SecurityIdentity == incoming.SecurityIdentity
        && current.OwnerId == incoming.OwnerId
        && current.LeaseGeneration == incoming.LeaseGeneration
        && current.ApplicationVersion == incoming.ApplicationVersion
        && current.ObjectRole == incoming.ObjectRole
        && current.ChannelWeights.Keys.ToHashSet(StringComparer.Ordinal)
            .SetEquals(incoming.ChannelWeights.Keys)
        && ObjectCapabilitiesEqual(
            current.ObjectCapabilities,
            incoming.ObjectCapabilities)
        && current.Capacity.Actors.Limit == incoming.Capacity.Actors.Limit
        && current.Capacity.Spots.Limit == incoming.Capacity.Spots.Limit;


    private static ZLinkMeshNodeDescriptor CanonicalizeMeshNodeDescriptor(
        ZLinkMeshNodeDescriptor descriptor) =>
        descriptor with
        {
            ChannelWeights = descriptor.ChannelWeights
                .OrderBy(
                    static pair => pair.Key,
                    Utf8StringComparer.Instance)
                .ToDictionary(
                    static pair => pair.Key,
                    static pair => pair.Value,
                    StringComparer.Ordinal),
            ObjectCapabilities = descriptor.ObjectCapabilities
                .OrderBy(static capability => capability.ObjectKind)
                .ThenBy(
                    static capability => capability.StableType,
                    Utf8StringComparer.Instance)
                .ToArray()
        };

    private static bool ObjectCapabilitiesEqual(
        IReadOnlyList<ZLinkObjectCapability> current,
        IReadOnlyList<ZLinkObjectCapability> incoming)
    {
        if (current.Count != incoming.Count)
            return false;
        for (var index = 0; index < current.Count; index++)
        {
            var left = current[index];
            var right = incoming[index];
            if (left.ObjectKind != right.ObjectKind
                || left.StableType != right.StableType
                || left.Policy != right.Policy
                || left.HasSnapshotAdapter != right.HasSnapshotAdapter
                || left.Limit != right.Limit)
                return false;
        }
        return true;
    }

    private static void ValidateMeshNodeDescriptor(
        ZLinkMeshNodeDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        ValidateUtf8Value(descriptor.MeshName, nameof(descriptor.MeshName));
        if (descriptor.Rid.IsEmpty
            || descriptor.LifecycleGeneration == 0
            || descriptor.DescriptorRevision == 0
            || descriptor.ApplicationVersion < 0
            || descriptor.ChannelWeights is null
            || string.IsNullOrWhiteSpace(descriptor.OwnerId)
            || descriptor.LeaseGeneration <= 0
            || !Enum.IsDefined(descriptor.State)
            || !Enum.IsDefined(descriptor.ObjectRole)
            || descriptor.PlacementWeight is < 0 or > ZLinkSocketConfig.MaximumPeerWeight
            || !IsValidCapacity(descriptor.Capacity.Actors)
            || !IsValidCapacity(descriptor.Capacity.Spots)
            || descriptor.Capacity.SpotTypes is null
            || descriptor.ActivationConcurrency is not
            {
                Active: >= 0,
                Limit: > 0
            }
            || descriptor.ActivationConcurrency.Active
                > descriptor.ActivationConcurrency.Limit
            || descriptor.ObjectCapabilities is null
            || descriptor.ObjectCapabilities.Count > 1024
            || descriptor.ObjectRole != ZLinkMeshNodeObjectRole.Server
            && descriptor.ObjectCapabilities.Count != 0)
            throw new ArgumentException(
                "The MeshNode descriptor is invalid.",
                nameof(descriptor));
        foreach (var (channelName, weight) in descriptor.ChannelWeights)
        {
            ValidateUtf8Value(channelName, "ChannelName");
            if (weight is < 0 or > ZLinkSocketConfig.MaximumPeerWeight)
                throw new ArgumentOutOfRangeException(
                    nameof(descriptor),
                    "Channel weight must be between 0 and 10000.");
        }
        if (descriptor.MaintenanceWave is { } wave)
            ValidateUtf8Value(wave, nameof(descriptor.MaintenanceWave));
        if (descriptor.ObjectRole == ZLinkMeshNodeObjectRole.Server)
            ValidateUtf8Value(
                descriptor.EntrySpotId
                ?? throw new ArgumentException(
                    "Object Server descriptor EntrySpotId is required.",
                    nameof(descriptor)),
                nameof(descriptor.EntrySpotId));
        else if (descriptor.EntrySpotId is not null)
            throw new ArgumentException(
                "Only an Object Server descriptor can publish EntrySpotId.",
                nameof(descriptor));
        var identities =
            new HashSet<(ZLinkPlacementObjectKind, string)>();
        foreach (var capability in descriptor.ObjectCapabilities)
        {
            if (capability is null
                || !Enum.IsDefined(capability.ObjectKind)
                || !Enum.IsDefined(capability.Policy)
                || !identities.Add((
                    capability.ObjectKind,
                    capability.StableType))
                || capability.Policy
                == ZLinkObjectMaintenancePolicyKind.Snapshot
                != capability.HasSnapshotAdapter
                || capability.Limit < 0
                || capability.ObjectKind == ZLinkPlacementObjectKind.Actor
                    && capability.Limit != 0)
                throw new ArgumentException(
                    "The MeshNode object capabilities are invalid.",
                    nameof(descriptor));
            ValidateUtf8Value(
                capability.StableType,
                nameof(capability.StableType));
        }
        var expectedSpotTypes = descriptor.ObjectCapabilities
            .Where(static capability =>
                capability.ObjectKind is ZLinkPlacementObjectKind.UserSpot
                    or ZLinkPlacementObjectKind.InstanceSpot)
            .Select(static capability =>
                (capability.ObjectKind, capability.StableType, capability.Limit))
            .ToArray();
        if (descriptor.Capacity.SpotTypes.Count != expectedSpotTypes.Length)
            throw new ArgumentException(
                "The MeshNode Spot type capacity projection is invalid.",
                nameof(descriptor));
        for (var index = 0; index < expectedSpotTypes.Length; index++)
        {
            var capacity = descriptor.Capacity.SpotTypes[index];
            var expected = expectedSpotTypes[index];
            if (capacity.ObjectKind != expected.ObjectKind
                || capacity.StableType != expected.StableType
                || capacity.Limit != expected.Limit
                || capacity.Active < 0
                || capacity.Reserved < 0
                || capacity.Limit > 0
                    && capacity.Active + (long)capacity.Reserved
                    > capacity.Limit)
                throw new ArgumentException(
                    "The MeshNode Spot type capacity projection is invalid.",
                    nameof(descriptor));
        }
    }

    private static bool IsValidCapacity(ZLinkPopulationCapacity capacity) =>
        capacity is { Active: >= 0, Reserved: >= 0, Limit: >= 0 }
        && (capacity.Limit == 0
            || capacity.Active + (long)capacity.Reserved <= capacity.Limit);

    private bool CanPublishEntrySpotIdNoLock(
        ZLinkMeshNodeDescriptor descriptor,
        string descriptorKey,
        DateTimeOffset now)
    {
        if (descriptor.EntrySpotId is not { } entrySpotId)
            return true;

        if (_entrySpotIdClaims.TryGetValue(entrySpotId, out var claim)
            && (!string.Equals(
                    claim.DescriptorKey,
                    descriptorKey,
                    StringComparison.Ordinal)
                || claim.DescriptorLifecycleGeneration
                != descriptor.LifecycleGeneration
                || claim.Owner != new ZLinkLocationOwnerToken(
                    descriptor.OwnerId,
                    descriptor.LeaseGeneration))
            && MatchesLiveOwnerLease(claim.Owner, now))
            return false;

        var spotKey = ZLinkLocationKeyCodec.EncodeSpotKey(
            new ZLinkSpotLocationKey(entrySpotId));
        if (_instanceSpots.Rows.ContainsKey(spotKey))
            return false;
        var authorityKey =
            Zlink.Framework.Runtime.Spots.ZLinkUserSpotAuthorityPayloadCodec
                .AuthorityKey(entrySpotId);
        if (_authorities.ContainsKey(authorityKey.Value))
            return false;
        if (_spots.Rows.TryGetValue(spotKey, out var existing)
            && (existing.SpotKind != ZLinkSpotKind.Entry
                || existing.OwnerNodeRid != descriptor.Rid
                || existing.OwnerNodeGeneration
                != descriptor.LifecycleGeneration
                || existing.OwnerId != descriptor.OwnerId))
            return false;

        return true;
    }

    private void PublishEntrySpotIdNoLock(
        ZLinkMeshNodeDescriptor? previous,
        ZLinkMeshNodeDescriptor descriptor,
        string descriptorKey)
    {
        if (previous is not null
            && !string.Equals(
                previous.EntrySpotId,
                descriptor.EntrySpotId,
                StringComparison.Ordinal))
            RemoveEntrySpotIdClaimNoLock(previous, descriptorKey);

        if (descriptor.EntrySpotId is { } entrySpotId)
        {
            _entrySpotIdClaims[entrySpotId] = new EntrySpotIdClaim(
                descriptorKey,
                descriptor.LifecycleGeneration,
                new ZLinkLocationOwnerToken(
                    descriptor.OwnerId,
                    descriptor.LeaseGeneration));
        }
    }

    private void RemoveEntrySpotIdClaimNoLock(
        ZLinkMeshNodeDescriptor descriptor,
        string descriptorKey)
    {
        if (descriptor.EntrySpotId is not { } entrySpotId
            || !_entrySpotIdClaims.TryGetValue(entrySpotId, out var claim)
            || !string.Equals(
                claim.DescriptorKey,
                descriptorKey,
                StringComparison.Ordinal)
            || claim.DescriptorLifecycleGeneration
            != descriptor.LifecycleGeneration
            || claim.Owner != new ZLinkLocationOwnerToken(
                descriptor.OwnerId,
                descriptor.LeaseGeneration))
            return;

        _entrySpotIdClaims.Remove(entrySpotId);
    }

    private static bool EntrySpotRowMatchesClaim(
        ZLinkSpotLocation spot,
        EntrySpotIdClaim claim) =>
        spot.OwnerNodeGeneration == claim.DescriptorLifecycleGeneration
        && spot.OwnerId == claim.Owner.OwnerId;

    private static void ValidateUtf8Value(string value, string name)
    {
        var size = System.Text.Encoding.UTF8.GetByteCount(value);
        if (size is < 1 or > 255 || value.Contains('\0'))
            throw new ArgumentException(
                $"{name} must be 1 to 255 UTF-8 bytes without NUL.",
                name);
    }

    private sealed class Utf8StringComparer : IComparer<string>
    {
        internal static Utf8StringComparer Instance { get; } = new();

        public int Compare(string? left, string? right)
        {
            if (ReferenceEquals(left, right))
                return 0;
            if (left is null)
                return -1;
            if (right is null)
                return 1;
            return System.Text.Encoding.UTF8.GetBytes(left)
                .AsSpan()
                .SequenceCompareTo(
                    System.Text.Encoding.UTF8.GetBytes(right));
        }
    }

    private sealed record EntrySpotIdClaim(
        string DescriptorKey,
        ulong DescriptorLifecycleGeneration,
        ZLinkLocationOwnerToken Owner);

    public ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var key = ZLinkLocationKeyCodec.EncodeSpotKey(
            new ZLinkSpotLocationKey(spot.SpotId));
        lock (_gate)
        {
            if (_entrySpotIdClaims.TryGetValue(
                    spot.SpotId,
                    out var entryClaim)
                && (spot.SpotKind != ZLinkSpotKind.Entry
                    || !EntrySpotRowMatchesClaim(spot, entryClaim)))
            {
                return ValueTask.FromResult(
                    ZLinkLocationWriteResult.RejectedConflict);
            }
            if (_instanceSpots.Rows.ContainsKey(key))
            {
                return ValueTask.FromResult(
                    ZLinkLocationWriteResult.RejectedConflict);
            }

            return ValueTask.FromResult(Write(
                _spots,
                key,
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
        }
    }

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
            null));

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
                new ZLinkSpotLocationKey(request.SpotId));
            if (_entrySpotIdClaims.ContainsKey(request.SpotId)
                || _spots.Rows.ContainsKey(key))
            {
                return ValueTask.FromResult<InstanceSpotClaimResult>(
                    new InstanceSpotClaimResult.Conflict());
            }
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
                request.SpotId,
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
                new ZLinkSpotLocationKey(fence.SpotId));
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
                new ZLinkSpotLocationKey(fence.SpotId));
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
                new ZLinkSpotLocationKey(fence.SpotId));
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
        && location.SpotId == fence.SpotId
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
                new ZLinkActorLocationKey(actor.ActorId)),
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
            null));

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
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(owner.OwnerId);
        if (owner.LeaseGeneration <= 0)
            throw new ArgumentOutOfRangeException(nameof(owner));
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!MatchesLiveOwnerLease(owner, _time.GetUtcNow()))
                return ValueTask.FromResult(0L);
            var removed = 0L;
            var ownedDescriptors = _meshNodes.Rows
                .Where(pair => pair.Value.OwnerId == owner.OwnerId)
                .ToArray();
            removed += RemoveByOwnerNoLock(
                _meshNodes, owner.OwnerId, static row => row.OwnerId, ZLinkLocationChangeScopeKind.MeshNode,
                static row => row.MeshName);
            foreach (var descriptor in ownedDescriptors)
                RemoveEntrySpotIdClaimNoLock(
                    descriptor.Value,
                    descriptor.Key);
            var clientServerKeys = _clientServers.Rows
                .Where(pair => pair.Value.OwnerId == owner.OwnerId)
                .Select(static pair => pair.Key)
                .ToArray();
            foreach (var key in clientServerKeys)
            {
                _clientServers.Rows.Remove(key);
                removed++;
            }
            removed += RemoveByOwnerNoLock(
                _spots, owner.OwnerId, static row => row.OwnerId, ZLinkLocationChangeScopeKind.Spot,
                static row => row.MeshName);
            removed += RemoveByOwnerNoLock(
                _instanceSpots, owner.OwnerId, static row => row.OwnerId,
                ZLinkLocationChangeScopeKind.Spot, static row => row.MeshName);
            removed += RemoveByOwnerNoLock(
                _actors, owner.OwnerId, static row => row.OwnerId, ZLinkLocationChangeScopeKind.Actor,
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
