using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Locations;

public sealed class LocationContracts
{
    private static readonly DateTimeOffset StoreNow =
        new(2026, 7, 2, 0, 0, 0, TimeSpan.Zero);

    [Fact]
    public void Location_contract_excludes_compatibility_lease_and_slot_allocation_surface()
    {
        var assembly = typeof(IZLinkLocationStore).Assembly;
        Assert.Null(assembly.GetType(
            "Zlink.Framework.Contracts.Locations.IZLinkRoutingIdSlotAllocationStore"));
        Assert.Null(assembly.GetType(
            "Zlink.Framework.Contracts.Locations.IZLinkAllocatedRoutingIdProvider"));
        Assert.Null(assembly.GetType(
            "Zlink.Framework.Contracts.Locations.ZLinkRoutingIdSlotAcquireResult"));
        Assert.Null(assembly.GetType(
            "Zlink.Framework.Contracts.Locations.ZLinkOwnerLeaseSnapshot"));
        Assert.Null(assembly.GetType(
            "Zlink.Framework.Contracts.Locations.ZLinkOwnerLeaseRenewal"));

        var publicMethods =
            typeof(Zlink.Framework.Locations.Redis.ZLinkRedisLocationStore)
            .GetMethods();
        var publicMethodNames = publicMethods
            .Select(static method => method.Name)
            .ToHashSet(StringComparer.Ordinal);
        Assert.DoesNotContain("AcquireRoutingIdSlotAsync", publicMethodNames);
        Assert.DoesNotContain("ListRoutingIdSlotsAsync", publicMethodNames);
        Assert.DoesNotContain("ListOwnerLeasesAsync", publicMethodNames);
        Assert.DoesNotContain("RemoveOwnerLeaseAsync", publicMethodNames);

        var renew = Assert.Single(publicMethods.Where(
            static method => method.Name == nameof(
                IZLinkOwnerLeaseStore.RenewOwnerLeaseAsync)));
        Assert.Equal(
            typeof(ZLinkLocationOwnerToken),
            renew.GetParameters()[0].ParameterType);

        var release = Assert.Single(publicMethods.Where(
            static method => method.Name == nameof(
                IZLinkOwnerLeaseStore.ReleaseOwnerLeaseAsync)));
        Assert.Equal(
            typeof(ZLinkLocationOwnerToken),
            release.GetParameters()[0].ParameterType);
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
        typeof(IZLinkLocationStore),
        typeof(IZLinkAuthorityStore),
        typeof(IZLinkOwnerLeaseStore))]
    public async Task Location_store_combines_authority_and_owner_lease_transaction_domain()
    {
        Assert.Contains(typeof(IZLinkAuthorityStore), typeof(IZLinkLocationStore).GetInterfaces());
        Assert.Contains(typeof(IZLinkOwnerLeaseStore), typeof(IZLinkLocationStore).GetInterfaces());
        Assert.Contains(
            typeof(IZLinkAuthorityStore).GetMethods(),
            static method => method.Name == nameof(IZLinkAuthorityStore.CompareExchangeAuthorityAsync));

        // Owner lease: the provider issues the exact generation token and
        // returns its own clock with each read.
        var leases = new ExampleOwnerLeaseStore();
        var leaseClaim = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await leases.ClaimOwnerLeaseAsync(
                "owner-b", TimeSpan.FromSeconds(15)));
        var read = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
            await leases.ReadOwnerLeaseAsync("owner-b"));
        Assert.Equal(StoreNow, read.StoreNow);
        Assert.Equal(leaseClaim.Token, read.Token);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkLocationStore),
        typeof(IZLinkMeshNodeLocationStore),
        typeof(IZLinkFanoutLocationStore),
        typeof(IZLinkAuthorityStore))]
    public async Task MeshNode_lists_are_paged_and_object_authority_is_opaque()
    {
        // One physical store registers for every role at once:
        // AddLocationStore takes a single IZLinkLocationStore instance the
        // way codecs take serializer instances — the framework surface
        // never names a concrete backend.

        var meshNodes = new ExampleMeshNodeLocationStore();
        await meshNodes.UpdateMeshNodeAsync(MakeDescriptor("owner-a"), ZLinkLocationWriteIntent.NewClaim);

        // Descriptor lists are one point-in-time snapshot per mesh by
        // contract — reconcile diffs need one consistent list, never pages.
        var descriptors = await meshNodes.ListMeshNodesAsync(
            "play",
            new ZLinkPageRequest());
        Assert.Single(descriptors.Items);

        Assert.DoesNotContain(
            typeof(IZLinkLocationStore).GetMethods(),
            static method => method.Name.Contains("Spot", StringComparison.Ordinal)
                             || method.Name.Contains("Actor", StringComparison.Ordinal));

        // Spot and Actor ownership is available only through the opaque
        // authority surface. Object-specific projection stores are not part
        // of the public contract.
        Assert.DoesNotContain(
            typeof(IZLinkLocationStore).GetMethods(),
            static method => method.Name.StartsWith("List", StringComparison.Ordinal));
        Assert.Null(typeof(IZLinkLocationStore).Assembly.GetType(
            "Zlink.Framework.Contracts.Locations.IZLinkInstanceSpotLocationStore"));
        Assert.Null(typeof(IZLinkLocationStore).Assembly.GetType(
            "Zlink.Framework.Contracts.Locations.InstanceSpotLocation"));
    }

    [Fact]
    public void Object_and_mesh_routes_are_not_public_resolvers()
    {
        var assembly = typeof(IZLinkLocationStore).Assembly;
        Assert.Null(assembly.GetType(
            "Zlink.Framework.Contracts.Locations.IZLinkMeshNodeLocationResolver"));
        Assert.Null(assembly.GetType(
            "Zlink.Framework.Contracts.Locations.IZLinkSpotHandleResolver"));
        Assert.Null(assembly.GetType(
            "Zlink.Framework.Contracts.Locations.IZLinkActorSpotHandleResolver"));
        Assert.Null(assembly.GetType(
            "Zlink.Framework.Contracts.Locations.SpotHandle"));
    }

    [Fact]
    public void Public_object_location_rows_exclude_internal_authority_axes()
    {
        var spotProperties = typeof(ZLinkSpotLocation).GetProperties()
            .Select(static property => property.Name)
            .ToHashSet(StringComparer.Ordinal);
        Assert.Contains(nameof(ZLinkSpotLocation.LeaseGeneration), spotProperties);
        Assert.DoesNotContain("AuthorityOwnerGeneration", spotProperties);
        Assert.DoesNotContain("OwnerLeaseGeneration", spotProperties);

        var actorProperties = typeof(ZLinkActorLocation).GetProperties()
            .Select(static property => property.Name)
            .ToHashSet(StringComparer.Ordinal);
        Assert.Contains(nameof(ZLinkActorLocation.LeaseGeneration), actorProperties);
        Assert.DoesNotContain("MembershipEpoch", actorProperties);
        Assert.DoesNotContain("AuthorityOwnerGeneration", actorProperties);
        Assert.DoesNotContain("OwnerLeaseGeneration", actorProperties);
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
        Assert.Single(descriptors.Items);

        var topology = await query.ListTopologyAsync(new ZLinkLocationTopologyFilter(MeshName: "play"));
        Assert.Single(topology.Items);

        var summaries = await query.ListServiceSummariesAsync(
            new ZLinkLocationServiceSummaryFilter(MeshName: "play"));
        Assert.Single(summaries.Items);

        // A poller skips the full list query while the stamp is unchanged.
        // The stamp is an optimization, never a correctness authority.
        var stamps = new ExampleChangeStampStore();
        var stamp = await stamps.GetChangeStampAsync(
            new ZLinkLocationChangeStampScope(ZLinkLocationChangeScopeKind.MeshNode, "play"));
        Assert.Equal(1UL, stamp);
    }

    private static ZLinkMeshNodeDescriptor MakeDescriptor(string ownerId) => new(
        "play",
        RoutingId.From("node-1"),
        LifecycleGeneration: 1,
        DescriptorRevision: 1,
        "tcp://127.0.0.1:5001",
        new Dictionary<string, int>(StringComparer.Ordinal) { ["play"] = 100 },
        SecurityIdentity: "cluster-a",
        OwnerId: ownerId,
        LeaseGeneration: 1,
        UpdatedAt: StoreNow);

    private sealed class ExampleOwnerLeaseStore : IZLinkOwnerLeaseStore
    {
        private readonly Dictionary<string, Lease> _leases = [];
        private long _generation;

        public ValueTask<ZLinkOwnerLeaseClaimResult> ClaimOwnerLeaseAsync(
            string ownerId,
            TimeSpan leaseTtl,
            CancellationToken cancellationToken = default)
        {
            if (_leases.TryGetValue(ownerId, out var current)
                && current.LeaseExpiresAt > StoreNow)
                return ValueTask.FromResult<ZLinkOwnerLeaseClaimResult>(
                    new ZLinkOwnerLeaseClaimResult.Conflict());
            var expiresAt = StoreNow + leaseTtl;
            var token = new ZLinkLocationOwnerToken(ownerId, ++_generation);
            _leases[ownerId] = new Lease(token.LeaseGeneration, expiresAt);
            return ValueTask.FromResult<ZLinkOwnerLeaseClaimResult>(
                new ZLinkOwnerLeaseClaimResult.Claimed(
                    token, expiresAt, StoreNow));
        }

        public ValueTask<ZLinkOwnerLeaseReadResult> ReadOwnerLeaseAsync(
            string ownerId,
            CancellationToken cancellationToken = default)
        {
            if (!_leases.TryGetValue(ownerId, out var lease)
                || lease.LeaseExpiresAt <= StoreNow)
                return ValueTask.FromResult<ZLinkOwnerLeaseReadResult>(
                    new ZLinkOwnerLeaseReadResult.Missing());
            return ValueTask.FromResult<ZLinkOwnerLeaseReadResult>(
                new ZLinkOwnerLeaseReadResult.Found(
                    new ZLinkLocationOwnerToken(
                        ownerId, lease.LeaseGeneration),
                    lease.LeaseExpiresAt,
                    StoreNow));
        }

        public ValueTask<ZLinkOwnerLeaseRenewResult> RenewOwnerLeaseAsync(
            ZLinkLocationOwnerToken token,
            TimeSpan leaseTtl,
            CancellationToken cancellationToken = default)
        {
            if (!_leases.TryGetValue(token.OwnerId, out var lease)
                || lease.LeaseGeneration != token.LeaseGeneration)
                return ValueTask.FromResult<ZLinkOwnerLeaseRenewResult>(
                    new ZLinkOwnerLeaseRenewResult.Stale());
            var expiresAt = StoreNow + leaseTtl;
            _leases[token.OwnerId] = lease with
            {
                LeaseExpiresAt = expiresAt
            };
            return ValueTask.FromResult<ZLinkOwnerLeaseRenewResult>(
                new ZLinkOwnerLeaseRenewResult.Renewed(
                    expiresAt, StoreNow));
        }

        public ValueTask<ZLinkOwnerLeaseReleaseResult> ReleaseOwnerLeaseAsync(
            ZLinkLocationOwnerToken token,
            CancellationToken cancellationToken = default)
        {
            if (!_leases.TryGetValue(token.OwnerId, out var lease)
                || lease.LeaseGeneration != token.LeaseGeneration)
                return ValueTask.FromResult(ZLinkOwnerLeaseReleaseResult.Stale);
            _leases.Remove(token.OwnerId);
            return ValueTask.FromResult(
                ZLinkOwnerLeaseReleaseResult.Released);
        }

        private readonly record struct Lease(
            long LeaseGeneration,
            DateTimeOffset LeaseExpiresAt);
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

        public ValueTask<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
            string meshName,
            ZLinkPageRequest page,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkMeshNodeDescriptor> items =
                _row is { } row && row.MeshName == meshName ? [row] : [];
            return ValueTask.FromResult(
                new ZLinkLocationPage<ZLinkMeshNodeDescriptor>(items, null));
        }
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

        public ValueTask<ZLinkLocationPage<ZLinkMeshNodeDescriptor>>
            ListMeshNodeDescriptorsAsync(
            string meshName,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkMeshNodeDescriptor> items = [MakeDescriptor("owner-a")];
            return ValueTask.FromResult(
                new ZLinkLocationPage<ZLinkMeshNodeDescriptor>(items, null));
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

        public ValueTask<ZLinkLocationPage<ZLinkLocationServiceSummary>>
            ListServiceSummariesAsync(
            ZLinkLocationServiceSummaryFilter filter,
            ZLinkPageRequest page = default,
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
            return ValueTask.FromResult(
                new ZLinkLocationPage<ZLinkLocationServiceSummary>(items, null));
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
