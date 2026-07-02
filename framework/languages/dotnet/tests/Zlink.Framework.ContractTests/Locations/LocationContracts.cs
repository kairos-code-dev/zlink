using System.Runtime.CompilerServices;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Locations;

public sealed class LocationContracts
{
    private static readonly DateTimeOffset StoreNow =
        new(2026, 7, 2, 0, 0, 0, TimeSpan.Zero);

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

        var removedCount = await store.RemoveByOwnerAsync(ownerB);
        Assert.Equal(1, removedCount);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkPeerLocationStore),
        typeof(IZLinkSpotLocationStore),
        typeof(IZLinkRouteLocationStore))]
    public async Task Peer_spot_route_stores_expose_snapshot_and_paged_lists()
    {
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
        typeof(IZLinkSpotLocationResolver),
        typeof(IZLinkActorLocationResolver),
        typeof(IZLinkRouteLocationResolver))]
    public async Task Resolvers_are_single_resolve_surfaces_with_freshness()
    {
        var resolver = new ExampleLocationResolver();

        // Normal allows cache hits; Refresh always re-reads the store and is
        // required on reconnect and create-if-absent decisions.
        var peersNormal = await resolver.ListPeersAsync(
            new ZLinkPeerLocationFilter(MeshName: "play"));
        var peersFresh = await resolver.ListPeersAsync(
            new ZLinkPeerLocationFilter(MeshName: "play"),
            ZLinkResolveFreshness.Refresh);
        Assert.Single(peersNormal);
        Assert.Single(peersFresh);

        var spot = await resolver.ResolveSpotAsync(
            new ZLinkSpotLocationKey("play", RoutingId.From("spot-1")));
        Assert.NotNull(spot);

        var actor = await resolver.ResolveActorAsync(
            new ZLinkActorLocationKey("player", "actor-1"),
            ZLinkResolveFreshness.Refresh);
        Assert.NotNull(actor);

        var route = await resolver.ResolveRouteAsync(
            new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, "session-1"));
        Assert.NotNull(route);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkLocationRuntimeQuery),
        typeof(IZLinkLocationWatchStore),
        typeof(IZLinkLocationChangeStampStore))]
    public async Task Runtime_query_reads_store_directly_and_watch_is_optional()
    {
        var query = new ExampleLocationRuntimeQuery();

        var status = await query.GetStatusAsync();
        Assert.True(status.StoreHealthy);
        Assert.True(status.OwnerLeaseHealthy);

        // Runtime query never goes through a cache, so it takes no freshness.
        var peerList = await query.ListPeersAsync(new ZLinkPeerLocationFilter(MeshName: "play"));
        Assert.Single(peerList);

        var actors = await query.ListActorsAsync(
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
        "player",
        "actor-1",
        "actor-ref-1",
        RoutingId.From("node-1"),
        generation,
        ZLinkSpotKind.Entry,
        null,
        ZLinkSpotKind.Entry,
        ownerId,
        StoreNow);

    private static ZLinkPeerLocation MakePeer(string ownerId) => new(
        ZLinkLocationAutoConnectType.RouteMesh,
        "play",
        RoutingId.From("node-1"),
        ZLinkLocationRole.Router,
        "tcp://127.0.0.1:5001",
        100,
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

        public ValueTask<long> RemoveByOwnerAsync(
            string ownerId,
            CancellationToken cancellationToken = default)
        {
            if (_row is null || _row.OwnerId != ownerId)
            {
                return ValueTask.FromResult(0L);
            }

            _row = null;
            return ValueTask.FromResult(1L);
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
    }

    private sealed class ExampleOwnerLeaseStore : IZLinkOwnerLeaseStore
    {
        private readonly Dictionary<string, ZLinkOwnerLease> _leases = [];

        public ValueTask<ZLinkLocationWriteResult> RenewOwnerLeaseAsync(
            string ownerId,
            RoutingId nodeRid,
            TimeSpan leaseTtl,
            CancellationToken cancellationToken = default)
        {
            // The store computes the absolute expiry from its own clock.
            _leases[ownerId] = new ZLinkOwnerLease(ownerId, nodeRid, StoreNow + leaseTtl, StoreNow);
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(0, StoreNow));
        }

        public ValueTask<ZLinkLocationWriteResult> RemoveOwnerLeaseAsync(
            string ownerId,
            CancellationToken cancellationToken = default)
        {
            _leases.Remove(ownerId);
            return ValueTask.FromResult(ZLinkLocationWriteResult.Stored(0, StoreNow));
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

        public ValueTask<long> RemoveByOwnerAsync(
            string ownerId,
            CancellationToken cancellationToken = default)
        {
            var removed = _row is not null && _row.OwnerId == ownerId ? 1L : 0L;
            if (removed == 1L)
            {
                _row = null;
            }

            return ValueTask.FromResult(removed);
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

        public ValueTask<long> RemoveByOwnerAsync(
            string ownerId,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(0L);

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

        public ValueTask<long> RemoveByOwnerAsync(
            string ownerId,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(0L);

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
        IZLinkSpotLocationResolver,
        IZLinkActorLocationResolver,
        IZLinkRouteLocationResolver
    {
        public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
            ZLinkPeerLocationFilter filter,
            ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkPeerLocation> items = [MakePeer("owner-a")];
            return ValueTask.FromResult(items);
        }

        public ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
            ZLinkSpotLocationKey key,
            ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ZLinkSpotLocation?>(MakeSpot("owner-a"));

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key,
            ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ZLinkActorLocation?>(MakeActor("owner-a", 1));

        public ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
            ZLinkRouteLocationKey key,
            ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ZLinkRouteLocation?>(MakeRoute("owner-a"));
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
                OwnerLeaseRenewedAt: StoreNow,
                PeerCacheEntryCount: 1,
                SpotCacheEntryCount: 0,
                ActorCacheEntryCount: 0,
                RouteCacheEntryCount: 0));

        public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
            ZLinkPeerLocationFilter filter,
            CancellationToken cancellationToken = default)
        {
            IReadOnlyList<ZLinkPeerLocation> items = [MakePeer("owner-a")];
            return ValueTask.FromResult(items);
        }

        public ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
            ZLinkSpotLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkLocationPage<ZLinkSpotLocation>(
                [MakeSpot("owner-a")], null));

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkLocationPage<ZLinkActorLocation>(
                [MakeActor("owner-a", 1)], null));

        public ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
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
                "route-mesh|play|router|node-1",
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
