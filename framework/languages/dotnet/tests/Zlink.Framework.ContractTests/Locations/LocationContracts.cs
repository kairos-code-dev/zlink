using System.Runtime.CompilerServices;
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
    }

    [Fact]
    public void Location_options_expose_one_explicit_spot_mesh_to_route_channel_mapping_method()
    {
        var method = Assert.Single(
            typeof(ZLinkLocationOptions).GetMethods(),
            static candidate => candidate.Name == nameof(ZLinkLocationOptions.MapSpotMeshToRouteChannel));
        Assert.Equal(typeof(void), method.ReturnType);
        Assert.Equal(
            new[] { typeof(string), typeof(string) },
            method.GetParameters().Select(static parameter => parameter.ParameterType).ToArray());

        var options = new ZLinkLocationOptions();
        options.MapSpotMeshToRouteChannel("game.stage", "game.route");
        options.MapSpotMeshToRouteChannel("game.stage", "game.route"); // 같은 선언은 멱등이다.
        Assert.Throws<InvalidOperationException>(() =>
            options.MapSpotMeshToRouteChannel("game.stage", "other.route"));
        Assert.Throws<ArgumentException>(() =>
            options.MapSpotMeshToRouteChannel(" ", "game.route"));
        Assert.Throws<ArgumentException>(() =>
            options.MapSpotMeshToRouteChannel("game.stage", " "));
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkActorLocationStore),
        typeof(IZLinkOwnerLeaseStore))]
    public async Task Actor_store_issues_generations_and_guards_writes_with_owner_tokens()
    {
        var store = new ExampleActorLocationStore();
        var ownerA = "owner-a";
        var ownerB = "owner-b";

        // NewClaim asks with an empty generation; the store issues one and
        // returns it in the write result. This is the only path a generation
        // travels — it is never distributed between nodes.
        var claimed = await store.UpdateActorAsync(
            MakeActor(ownerA, generation: 0),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, claimed.Status);
        Assert.Equal(1, claimed.Generation);

        // A concurrent NewClaim over a live row loses with RejectedConflict.
        var lost = await store.UpdateActorAsync(
            MakeActor(ownerB, generation: 0),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, lost.Status);

        // Renew must present the current owner token.
        var renewed = await store.UpdateActorAsync(
            MakeActor(ownerA, claimed.Generation),
            ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, renewed.Status);
        Assert.Equal(claimed.Generation, renewed.Generation);

        // Takeover replaces a live row and gets a fresh generation.
        var takeover = await store.UpdateActorAsync(
            MakeActor(ownerB, generation: 0),
            ZLinkLocationWriteIntent.Takeover);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, takeover.Status);
        Assert.Equal(2, takeover.Generation);

        // The replaced owner's next write is ignored as stale — that is how
        // it learns it lost ownership and must deactivate its instance.
        var stale = await store.UpdateActorAsync(
            MakeActor(ownerA, claimed.Generation),
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

        var removedCount = await store.RemoveAllByOwnerAsync(ownerB);
        Assert.Equal(1, removedCount);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkLocationStore),
        typeof(IZLinkPeerLocationStore),
        typeof(IZLinkSpotLocationStore),
        typeof(IZLinkRouteLocationStore))]
    public async Task Peer_spot_route_stores_expose_snapshot_and_paged_lists()
    {
        // One physical store registers for every role at once:
        // AddLocationStore takes a single IZLinkLocationStore instance the
        // way codecs take serializer instances — the framework surface
        // never names a concrete backend.

        var peers = new ExamplePeerLocationStore();
        await peers.UpdatePeerAsync(MakePeer("owner-a"), ZLinkLocationWriteIntent.NewClaim);

        // Peer lists are a single point-in-time snapshot by contract —
        // reconcile diffs need one consistent list, never pages.
        var peerList = await peers.ListPeersAsync(new ZLinkPeerLocationFilter(MeshName: "play"));
        Assert.Single(peerList);

        var spots = new ExampleSpotLocationStore();
        await spots.UpdateSpotAsync(MakeSpot("owner-a"), ZLinkLocationWriteIntent.NewClaim);
        var resolved = await spots.ResolveSpotAsync(
            new ZLinkSpotLocationKey("play", RoutingId.From("spot-1")));
        Assert.NotNull(resolved);

        // Spot/actor/route lists can hold millions of rows, so they page.
        var page = await spots.ListSpotsAsync(new ZLinkSpotLocationFilter(MeshName: "play"));
        Assert.Single(page.Items);
        Assert.Null(page.ContinuationToken);

        var routes = new ExampleRouteLocationStore();
        await routes.UpdateRouteAsync(MakeRoute("owner-a"), ZLinkLocationWriteIntent.NewClaim);
        var route = await routes.ResolveRouteAsync(
            new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, "session-1"));
        Assert.NotNull(route);
        var routePage = await routes.ListRoutesAsync(new ZLinkRouteLocationFilter(
            RouteKind: ZLinkRouteKind.ActorSession));
        Assert.Single(routePage.Items);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkPeerLocationResolver),
        typeof(IZLinkSpotHandleResolver),
        typeof(IZLinkActorSpotHandleResolver))]
    public async Task Peer_reads_are_live_and_messaging_resolvers_return_opaque_handles()
    {
        var resolver = new ExampleLocationResolver();

        // Peer discovery is a live store read rather than a retained messaging handle.
        var peers = await resolver.ListLivePeersAsync(
            new ZLinkPeerLocationFilter(MeshName: "play"));
        Assert.Single(peers);

        // Messaging lookup returns an opaque handle. The framework, not the caller,
        // owns its location snapshot updates and the safe request refresh rule.
        var spotHandle = await resolver.ResolveSpotHandleAsync(RoutingId.From("spot-1"));
        Assert.Null(spotHandle);

        var actorSpotHandle = await resolver.ResolveActorSpotHandleAsync("actor-1");
        Assert.Null(actorSpotHandle);

        Assert.Empty(typeof(SpotHandle).GetConstructors());

    }

    [Fact]
    [ContractExample(
        typeof(IZLinkLocationRuntimeQuery),
        typeof(IZLinkLocationReadiness),
        typeof(IZLinkLocationWatchStore),
        typeof(IZLinkLocationChangeStampStore))]
    public async Task Runtime_query_reads_store_directly_and_watch_is_optional()
    {
        var query = new ExampleLocationRuntimeQuery();
        var readiness = new ExampleLocationReadiness(query);

        var status = await query.GetStatusAsync();
        Assert.True(status.StoreHealthy);
        Assert.True(status.OwnerLeaseHealthy);

        var ready = await readiness.IsPeerReadyAsync("play", ZLinkLocationRole.Router);
        Assert.True(ready);

        // Runtime query never goes through a cache, so it takes no freshness.
        var peerList = await query.ListPeerLocationsAsync(new ZLinkPeerLocationFilter(MeshName: "play"));
        Assert.Single(peerList);

        var actors = await query.ListActorLocationsAsync(
            new ZLinkActorLocationFilter(ActorType: "player"),
            new ZLinkPageRequest(PageSize: 10));
        Assert.Single(actors.Items);

        var topology = await query.ListTopologyAsync(new ZLinkLocationTopologyFilter(
            Kind: ZLinkLocationKind.Peer));
        Assert.Single(topology.Items);

        var summaries = await query.ListServiceSummariesAsync(
            new ZLinkLocationServiceSummaryFilter(MeshName: "play"));
        Assert.Single(summaries);

        // Watch is a latency optimization; polling stays the correctness
        // path even when a store implements it.
        var watch = new ExampleWatchAndStampStore();
        await foreach (var changed in watch.WatchAsync(
            new ZLinkLocationWatchFilter(ZLinkLocationKind.Peer, MeshName: "play")))
        {
            Assert.Equal(ZLinkLocationChangeType.Upserted, changed.ChangeType);
        }

        // A poller skips the full list query while the stamp is unchanged.
        var stamp = await watch.GetChangeStampAsync(
            new ZLinkLocationChangeStampScope(ZLinkLocationKind.Peer, "play"));
        Assert.Equal(1, stamp);
    }

    private static ZLinkActorLocation MakeActor(string ownerId, long generation) => new(
        "actor-1",
        "player",
        new ActorRef(RoutingId.From("node-1"), "actor-1", 1),
        RoutingId.From("node-1"),
        ZLinkSpotKind.Entry,
        "play",
        null,
        ownerId,
        generation,
        StoreNow);

    private static ZLinkPeerLocation MakePeer(string ownerId) => new(
        ZLinkLocationAutoConnectType.RouteMesh,
        "play",
        RoutingId.From("node-1"),
        ZLinkLocationRole.Router,
        "tcp://127.0.0.1:5001",
        100,
        false,
        0,
        null,
        null,
        ownerId,
        1,
        StoreNow);

    private static ZLinkSpotLocation MakeSpot(string ownerId) => new(
        "play",
        RoutingId.From("spot-1"),
        "game",
        RoutingId.From("node-1"),
        ZLinkSpotKind.User,
        "tcp://127.0.0.1:5001",
        ownerId,
        1,
        StoreNow);

    private static ZLinkRouteLocation MakeRoute(string ownerId) => new(
        ZLinkRouteKind.ActorSession,
        "session-1",
        RoutingId.From("node-1"),
        ownerId,
        1,
        ReadOnlyMemory<byte>.Empty,
        StoreNow);

    private sealed class ExampleActorLocationStore : IZLinkActorLocationStore
    {
        private ZLinkActorLocation? _row;
        private long _generationCounter;

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
                    _row = actor with { Generation = _generationCounter };
                    return ValueTask.FromResult(
                        ZLinkLocationWriteResult.Stored(_generationCounter, StoreNow));
                case ZLinkLocationWriteIntent.Renew
                    when _row is not null
                         && _row.OwnerId == actor.OwnerId
                         && _row.Generation == actor.Generation:
                    _row = actor;
                    return ValueTask.FromResult(
                        ZLinkLocationWriteResult.Stored(actor.Generation, StoreNow));
                default:
                    return ValueTask.FromResult(ZLinkLocationWriteResult.IgnoredStale);
            }
        }

        public ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default)
        {
            if (_row is null || _row.OwnerId != owner.OwnerId || _row.Generation != owner.Generation)
            {
                return ValueTask.FromResult(ZLinkLocationWriteResult.IgnoredStale);
            }

            var generation = _row.Generation;
            _row = null;
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(generation, StoreNow));
        }

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(_row);

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkActorLocation> items = _row is null ? [] : [_row];
            return ValueTask.FromResult(new ZLinkLocationPage<ZLinkActorLocation>(items, null));
        }

        public ValueTask<long> RemoveAllByOwnerAsync(string ownerId)
        {
            if (_row is null || _row.OwnerId != ownerId)
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

    private sealed class ExamplePeerLocationStore : IZLinkPeerLocationStore
    {
        private ZLinkPeerLocation? _row;

        public ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
            ZLinkPeerLocation peer,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default)
        {
            _row = peer;
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(peer.Generation, StoreNow));
        }

        public ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
            ZLinkPeerLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default)
        {
            _row = null;
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(owner.Generation, StoreNow));
        }

        public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
            ZLinkPeerLocationFilter filter,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkPeerLocation> items = _row is null ? [] : [_row];
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
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(spot.Generation, StoreNow));
        }

        public ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
            ZLinkSpotLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default)
        {
            _row = null;
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(owner.Generation, StoreNow));
        }

        public ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
            ZLinkSpotLocationKey key,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(_row);

        public ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
            ZLinkSpotLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkSpotLocation> items = _row is null ? [] : [_row];
            return ValueTask.FromResult(new ZLinkLocationPage<ZLinkSpotLocation>(items, null));
        }
    }

    private sealed class ExampleRouteLocationStore : IZLinkRouteLocationStore
    {
        private ZLinkRouteLocation? _row;

        public ValueTask<ZLinkLocationWriteResult> UpdateRouteAsync(
            ZLinkRouteLocation route,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default)
        {
            _row = route;
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(route.Generation, StoreNow));
        }

        public ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
            ZLinkRouteLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default)
        {
            _row = null;
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(owner.Generation, StoreNow));
        }

        public ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
            ZLinkRouteLocationKey key,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(_row);

        public ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
            ZLinkRouteLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkRouteLocation> items = _row is null ? [] : [_row];
            return ValueTask.FromResult(new ZLinkLocationPage<ZLinkRouteLocation>(items, null));
        }
    }

    private sealed class ExampleLocationResolver :
        IZLinkPeerLocationResolver,
        IZLinkSpotHandleResolver,
        IZLinkActorSpotHandleResolver
    {
        public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListLivePeersAsync(
            ZLinkPeerLocationFilter filter,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkPeerLocation> items = [MakePeer("owner-a")];
            return ValueTask.FromResult(items);
        }

        public ValueTask<SpotHandle?> ResolveSpotHandleAsync(
            RoutingId spotRid,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<SpotHandle?>(null);

        public ValueTask<SpotHandle?> ResolveActorSpotHandleAsync(
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

        public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeerLocationsAsync(
            ZLinkPeerLocationFilter filter,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkPeerLocation> items = [MakePeer("owner-a")];
            return ValueTask.FromResult(items);
        }

        public ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotLocationsAsync(
            ZLinkSpotLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkLocationPage<ZLinkSpotLocation>(
                [MakeSpot("owner-a")], null));

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorLocationsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkLocationPage<ZLinkActorLocation>(
                [MakeActor("owner-a", 1)], null));

        public ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRouteLocationsAsync(
            ZLinkRouteLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkLocationPage<ZLinkRouteLocation>(
                [MakeRoute("owner-a")], null));

        public ValueTask<ZLinkLocationPage<ZLinkLocationTopologyEntry>> ListTopologyAsync(
            ZLinkLocationTopologyFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkLocationPage<ZLinkLocationTopologyEntry>(
                [
                    new ZLinkLocationTopologyEntry(
                        ZLinkLocationKind.Peer,
                        "play",
                        ZLinkLocationRole.Router,
                        RoutingId.From("node-1"),
                        null,
                        null,
                        "tcp://127.0.0.1:5001",
                        ZLinkLocationTopologyState.Ready,
                        1,
                        1,
                        0,
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
                    ZLinkLocationAutoConnectType.RouteMesh,
                    ZLinkLocationRole.Router,
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

    private sealed class ExampleWatchAndStampStore :
        IZLinkLocationWatchStore,
        IZLinkLocationChangeStampStore
    {
        public async IAsyncEnumerable<ZLinkLocationChanged> WatchAsync(
            ZLinkLocationWatchFilter filter,
            [EnumeratorCancellation] CancellationToken cancellationToken = default)
        {
            await Task.Yield();
            yield return new ZLinkLocationChanged(
                filter.Kind,
                new ZLinkLocationKey.Peer(new ZLinkPeerLocationKey(
                    ZLinkLocationAutoConnectType.RouteMesh,
                    "play",
                    ZLinkLocationRole.Router,
                    RoutingId.From("node-1"),
                    null)),
                ZLinkLocationChangeType.Upserted,
                1,
                StoreNow);
        }

        public ValueTask<long> GetChangeStampAsync(
            ZLinkLocationChangeStampScope scope,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(1L);
    }
}
