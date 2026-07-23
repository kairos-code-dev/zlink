using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Locations;

public sealed class LocationContracts
{
    private static readonly DateTimeOffset StoreNow =
        new(2026, 7, 2, 0, 0, 0, TimeSpan.Zero);

    [Fact]
    [ContractExample(
        typeof(IZLinkAllocatedRoutingIdProvider),
        typeof(IZLinkRoutingIdSlotAllocationStore))]
    public void Routing_id_slot_allocation_uses_one_optional_store_capability()
    {
        Assert.Contains(
            typeof(IZLinkRoutingIdSlotAllocationStore).GetMethods(),
            static method => method.Name == nameof(IZLinkRoutingIdSlotAllocationStore.AcquireRoutingIdSlotAsync));
        Assert.Contains(
            typeof(IZLinkRoutingIdSlotAllocationStore).GetMethods(),
            static method => method.Name == nameof(IZLinkRoutingIdSlotAllocationStore.ReleaseRoutingIdSlotAsync));
        Assert.Contains(
            typeof(IZLinkRoutingIdSlotAllocationStore).GetMethods(),
            static method => method.Name == nameof(IZLinkRoutingIdSlotAllocationStore.ListRoutingIdSlotsAsync));

        Assert.Equal(
            [nameof(ZLinkRoutingIdSlotAllocationMember.MeshName), nameof(ZLinkRoutingIdSlotAllocationMember.RoutingIdPrefix)],
            typeof(ZLinkRoutingIdSlotAllocationMember).GetProperties().Select(static property => property.Name));
    }

    [Fact]
    public void Location_options_do_not_expose_a_spot_mesh_route_mapping()
    {
        Assert.DoesNotContain(
            typeof(ZLinkLocationOptions).GetMethods(),
            static candidate => candidate.Name == "MapSpotMesh" + "ToRouteChannel");
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkActorLocationStore),
        typeof(IZLinkActorTransferStore),
        typeof(IZLinkOwnerLeaseStore))]
    public async Task Actor_store_issues_generations_and_guards_writes_with_owner_tokens()
    {
        var store = new ExampleActorLocationStore();
        var ownerA = "owner-a";
        var ownerB = "owner-b";

        // NewClaim asks with no token; the store issues a fencing generation
        // and returns it in the write result. This is the only path the
        // store token travels — it is never distributed between nodes.
        var claimed = await store.UpdateActorAsync(
            MakeActor(ownerA),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, claimed.Status);
        Assert.Equal(1UL, claimed.Generation);

        // A concurrent NewClaim over a live row loses with RejectedConflict.
        var lost = await store.UpdateActorAsync(
            MakeActor(ownerB),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, lost.Status);

        // Renew must present the current owner.
        var renewed = await store.UpdateActorAsync(
            MakeActor(ownerA),
            ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, renewed.Status);
        Assert.Equal(claimed.Generation, renewed.Generation);

        // Takeover replaces a live row and gets a fresh generation.
        var takeover = await store.UpdateActorAsync(
            MakeActor(ownerB),
            ZLinkLocationWriteIntent.Takeover);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, takeover.Status);
        Assert.Equal(2UL, takeover.Generation);

        // The replaced owner's next write is ignored as stale — that is how
        // it learns it lost ownership and must deactivate its instance.
        var stale = await store.UpdateActorAsync(
            MakeActor(ownerA),
            ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, stale.Status);

        // Owner lease: one row per runtime, snapshot carries the store time
        // so expiry is never judged against an application wall clock.
        var leases = new ExampleOwnerLeaseStore();
        await leases.RenewOwnerLeaseAsync(
            ownerB, RoutingId.From("node-b"), TimeSpan.FromSeconds(15));
        var snapshot = await leases.ListOwnerLeasesAsync();
        Assert.Equal(StoreNow, snapshot.StoreNow);
        Assert.Single(snapshot.Leases);

        var removedCount = await store.RemoveAllByOwnerAsync(
            new ZLinkLocationOwnerToken(ownerB, takeover.Generation));
        Assert.Equal(1, removedCount);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkLocationStore),
        typeof(IZLinkMeshNodeLocationStore),
        typeof(IZLinkSpotLocationStore))]
    public async Task MeshNode_lists_are_snapshots_and_spot_actor_rows_are_resolve_only()
    {
        // One physical store registers for every role at once:
        // AddLocationStore takes a single IZLinkLocationStore instance the
        // way codecs take serializer instances — the framework surface
        // never names a concrete backend.

        var meshNodes = new ExampleMeshNodeLocationStore();
        await meshNodes.UpdateMeshNodeAsync(MakeDescriptor("owner-a"), ZLinkLocationWriteIntent.NewClaim);

        // Descriptor lists are one point-in-time snapshot per mesh by
        // contract — reconcile diffs need one consistent list, never pages.
        var descriptors = await meshNodes.ListMeshNodesAsync("play");
        Assert.Single(descriptors);

        var spots = new ExampleSpotLocationStore();
        await spots.UpdateSpotAsync(MakeSpot("owner-a"), ZLinkLocationWriteIntent.NewClaim);
        var resolved = await spots.ResolveSpotAsync(
            new ZLinkSpotLocationKey("play", RoutingId.From("spot-1")));
        Assert.NotNull(resolved);

        // Spot and actor rows are resolve-only: no store-level listing
        // surface exists for them (06-location-store §5).
        Assert.DoesNotContain(
            typeof(IZLinkSpotLocationStore).GetMethods(),
            static method => method.Name.StartsWith("List", StringComparison.Ordinal));
        Assert.DoesNotContain(
            typeof(IZLinkActorLocationStore).GetMethods(),
            static method => method.Name.StartsWith("List", StringComparison.Ordinal));
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkMeshNodeLocationResolver),
        typeof(IZLinkSpotHandleResolver),
        typeof(IZLinkActorSpotHandleResolver))]
    public async Task MeshNode_reads_are_live_and_messaging_resolvers_return_opaque_handles()
    {
        var resolver = new ExampleLocationResolver();

        // MeshNode discovery is a live store read rather than a retained
        // messaging handle.
        var descriptors = await resolver.ListLiveMeshNodesAsync("play");
        Assert.Single(descriptors);

        // Messaging lookup returns an opaque handle. The framework, not the caller,
        // owns its location snapshot updates and the safe request refresh rule.
        var spotHandle = await resolver.ResolveSpotHandleAsync(
            "play", RoutingId.From("spot-1"));
        Assert.Null(spotHandle);

        var actorSpotHandle = await resolver.ResolveActorSpotHandleAsync("play", "actor-1");
        Assert.Null(actorSpotHandle);

        Assert.Empty(typeof(SpotHandle).GetConstructors());
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkLocationRuntimeQuery),
        typeof(IZLinkLocationReadiness),
        typeof(IZLinkLocationChangeStampStore))]
    public async Task Runtime_query_reads_store_directly_and_change_stamp_is_optional()
    {
        var query = new ExampleLocationRuntimeQuery();
        var readiness = new ExampleLocationReadiness(query);

        var status = await query.GetStatusAsync();
        Assert.True(status.StoreHealthy);
        Assert.True(status.OwnerLeaseHealthy);

        var ready = await readiness.IsPeerReadyAsync("play", ZLinkLocationRole.Router);
        Assert.True(ready);

        // Runtime query never goes through a cache, so it takes no freshness.
        var descriptors = await query.ListMeshNodeDescriptorsAsync("play");
        Assert.Single(descriptors);

        var topology = await query.ListTopologyAsync(new ZLinkLocationTopologyFilter(MeshName: "play"));
        Assert.Single(topology.Items);

        var summaries = await query.ListServiceSummariesAsync(
            new ZLinkLocationServiceSummaryFilter(MeshName: "play"));
        Assert.Single(summaries);

        // A poller skips the full list query while the stamp is unchanged.
        // The stamp is an optimization, never a correctness authority.
        var stamps = new ExampleChangeStampStore();
        var stamp = await stamps.GetChangeStampAsync(
            new ZLinkLocationChangeStampScope(ZLinkLocationChangeScopeKind.MeshNode, "play"));
        Assert.Equal(1UL, stamp);
    }

    private static ZLinkActorLocation MakeActor(string ownerId) => new(
        "play",
        "actor-1",
        "player",
        new ActorRef(RoutingId.From("node-1"), "actor-1", 1),
        OwnerNodeRid: RoutingId.From("node-1"),
        OwnerNodeGeneration: 1,
        SpotRid: default,
        SpotGeneration: 0,
        SpotKind: ZLinkSpotKind.Entry,
        MembershipEpoch: 0,
        OwnerId: ownerId,
        UpdatedAt: StoreNow);

    private static ZLinkMeshNodeDescriptor MakeDescriptor(string ownerId) => new(
        "play",
        RoutingId.From("node-1"),
        LifecycleGeneration: 1,
        DescriptorRevision: 1,
        "tcp://127.0.0.1:5001",
        new Dictionary<string, int>(StringComparer.Ordinal) { ["play"] = 100 },
        Draining: false,
        SecurityIdentity: "cluster-a",
        OwnerId: ownerId,
        UpdatedAt: StoreNow);

    private static ZLinkSpotLocation MakeSpot(string ownerId) => new(
        "play",
        RoutingId.From("spot-1"),
        SpotGeneration: 1,
        OwnerNodeRid: RoutingId.From("node-1"),
        OwnerNodeGeneration: 1,
        SpotKind: ZLinkSpotKind.User,
        SpotType: "game",
        OwnerId: ownerId,
        UpdatedAt: StoreNow);

    private sealed class ExampleActorLocationStore : IZLinkActorLocationStore
    {
        private ZLinkActorLocation? _row;
        private ulong _generationCounter;
        private ulong _rowGeneration;

        public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
            ZLinkActorLocation actor,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default)
        {
            switch (intent)
            {
                case ZLinkLocationWriteIntent.NewClaim when _row is not null:
                    return ValueTask.FromResult(ZLinkLocationWriteResult.RejectedConflict);
                case ZLinkLocationWriteIntent.NewClaim:
                case ZLinkLocationWriteIntent.Takeover:
                    _generationCounter++;
                    _row = actor;
                    _rowGeneration = _generationCounter;
                    return ValueTask.FromResult(
                        ZLinkLocationWriteResult.Stored(_generationCounter, StoreNow));
                case ZLinkLocationWriteIntent.Renew
                    when _row is not null && _row.OwnerId == actor.OwnerId:
                    _row = actor;
                    return ValueTask.FromResult(
                        ZLinkLocationWriteResult.Stored(_rowGeneration, StoreNow));
                default:
                    return ValueTask.FromResult(ZLinkLocationWriteResult.IgnoredStale);
            }
        }

        public ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default)
        {
            if (_row is null || _row.OwnerId != owner.OwnerId || _rowGeneration != owner.Generation)
            {
                return ValueTask.FromResult(ZLinkLocationWriteStatus.IgnoredStale);
            }

            _row = null;
            return ValueTask.FromResult(ZLinkLocationWriteStatus.Stored);
        }

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(_row);

        public ValueTask<long> RemoveAllByOwnerAsync(
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default)
        {
            if (_row is null
                || _row.OwnerId != owner.OwnerId
                || _rowGeneration != owner.Generation)
            {
                return ValueTask.FromResult(0L);
            }

            _row = null;
            return ValueTask.FromResult(1L);
        }
    }

    private sealed class ExampleOwnerLeaseStore : IZLinkOwnerLeaseStore
    {
        private readonly Dictionary<string, ZLinkOwnerLease> _leases = [];

        public ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
            string ownerId,
            RoutingId nodeRid,
            TimeSpan leaseTtl,
            CancellationToken cancellationToken = default)
        {
            // The store computes the absolute expiry from its own clock.
            var expiresAt = StoreNow + leaseTtl;
            _leases[ownerId] = new ZLinkOwnerLease(ownerId, nodeRid, expiresAt, StoreNow);
            return ValueTask.FromResult(new ZLinkOwnerLeaseRenewal(expiresAt, StoreNow));
        }

        public ValueTask<bool> RemoveOwnerLeaseAsync(
            string ownerId,
            CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult(_leases.Remove(ownerId));
        }

        public ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkOwnerLeaseSnapshot([.. _leases.Values], StoreNow));
    }

    private sealed class ExampleMeshNodeLocationStore : IZLinkMeshNodeLocationStore
    {
        private ZLinkMeshNodeDescriptor? _row;

        public ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
            ZLinkMeshNodeDescriptor descriptor,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default)
        {
            _row = descriptor;
            return ValueTask.FromResult(
                ZLinkLocationWriteResult.Stored(descriptor.LifecycleGeneration, StoreNow));
        }

        public ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
            ZLinkMeshNodeDescriptorKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default)
        {
            _row = null;
            return ValueTask.FromResult(ZLinkLocationWriteStatus.Stored);
        }

        public ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
            string meshName,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkMeshNodeDescriptor> items =
                _row is { } row && row.MeshName == meshName ? [row] : [];
            return ValueTask.FromResult(items);
        }
    }

    private sealed class ExampleSpotLocationStore : IZLinkSpotLocationStore
    {
        private ZLinkSpotLocation? _row;

        public ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
            ZLinkSpotLocation spot,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default)
        {
            _row = spot;
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(spot.SpotGeneration, StoreNow));
        }

        public ValueTask<ZLinkLocationWriteStatus> RemoveSpotAsync(
            ZLinkSpotLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default)
        {
            _row = null;
            return ValueTask.FromResult(ZLinkLocationWriteStatus.Stored);
        }

        public ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
            ZLinkSpotLocationKey key,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(_row);
    }

    private sealed class ExampleLocationResolver :
        IZLinkMeshNodeLocationResolver,
        IZLinkSpotHandleResolver,
        IZLinkActorSpotHandleResolver
    {
        public ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListLiveMeshNodesAsync(
            string meshName,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkMeshNodeDescriptor> items = [MakeDescriptor("owner-a")];
            return ValueTask.FromResult(items);
        }

        public ValueTask<SpotHandle?> ResolveSpotHandleAsync(
            string meshName,
            RoutingId spotRid,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<SpotHandle?>(null);

        public ValueTask<SpotHandle?> ResolveActorSpotHandleAsync(
            string meshName,
            string actorId,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<SpotHandle?>(null);
    }

    private sealed class ExampleLocationRuntimeQuery : IZLinkLocationRuntimeQuery
    {
        public ValueTask<ZLinkLocationRuntimeStatus> GetStatusAsync(
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkLocationRuntimeStatus(
                StoreHealthy: true,
                WatchEnabled: false,
                PollingInterval: TimeSpan.FromSeconds(1),
                LastRefreshAt: StoreNow,
                LastError: null,
                OwnerLeaseHealthy: true,
                OwnerLeaseRenewedAt: StoreNow));

        public ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodeDescriptorsAsync(
            string meshName,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkMeshNodeDescriptor> items = [MakeDescriptor("owner-a")];
            return ValueTask.FromResult(items);
        }

        public ValueTask<ZLinkLocationPage<ZLinkLocationTopologyEntry>> ListTopologyAsync(
            ZLinkLocationTopologyFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkLocationPage<ZLinkLocationTopologyEntry>(
                [
                    new ZLinkLocationTopologyEntry(
                        "play",
                        RoutingId.From("node-1"),
                        "tcp://127.0.0.1:5001",
                        Draining: false,
                        ZLinkLocationTopologyState.Ready,
                        StoreNow)
                ],
                null));

        public ValueTask<IReadOnlyList<ZLinkLocationServiceSummary>> ListServiceSummariesAsync(
            ZLinkLocationServiceSummaryFilter filter,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkLocationServiceSummary> items =
            [
                new ZLinkLocationServiceSummary(
                    "play",
                    1,
                    1,
                    0,
                    0,
                    StoreNow)
            ];
            return ValueTask.FromResult(items);
        }
    }

    private sealed class ExampleLocationReadiness(IZLinkLocationRuntimeQuery query) : IZLinkLocationReadiness
    {
        public async ValueTask<bool> IsPeerReadyAsync(
            string meshName,
            ZLinkLocationRole role,
            RoutingId? nodeRid = null,
            CancellationToken cancellationToken = default)
        {
            _ = meshName;
            _ = role;
            _ = nodeRid;
            var status = await query.GetStatusAsync(cancellationToken);
            return status.StoreHealthy && status.OwnerLeaseHealthy;
        }
    }

    private sealed class ExampleChangeStampStore : IZLinkLocationChangeStampStore
    {
        public ValueTask<ulong> GetChangeStampAsync(
            ZLinkLocationChangeStampScope scope,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(1UL);
    }
}
