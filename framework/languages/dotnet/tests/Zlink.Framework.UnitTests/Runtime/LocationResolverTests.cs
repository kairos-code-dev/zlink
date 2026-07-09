using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class LocationResolverTests
{
    private const string OwnerA = "owner-a";
    private const string OwnerB = "owner-b";
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(15);
    private static readonly ZLinkActorLocationKey ActorKey = new("actor-1");

    [Fact]
    public async Task Every_Resolve_Reads_The_Store()
    {
        // The resolver has no cache (spot-address messaging draft §8):
        // consecutive resolves of the same key each reach the store, and a
        // takeover behind the caller's back is visible on the very next read.
        var fixture = await FixtureAsync();
        var counting = new CountingActorStore(fixture.Store);
        var observed = new ZLinkObservedLocationGenerations();
        var resolvers = new ZLinkStoreLocationResolvers(
            fixture.Store, fixture.Store, counting, fixture.Store,
            new ZLinkOwnerLeaseTracker(
                fixture.Store, new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero }, fixture.Time),
            observed);
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);

        var first = await resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(OwnerA, first!.OwnerId);

        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerB, 0), ZLinkLocationWriteIntent.Takeover);

        var second = await resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(OwnerB, second!.OwnerId);
        Assert.Equal(2, counting.ResolveCalls);
    }

    [Fact]
    public async Task NotFound_Then_Claim_Is_Visible_Immediately()
    {
        var fixture = await FixtureAsync();

        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));

        // An actor created right after a miss must be visible to the very
        // next resolve — the create-if-absent race depends on it.
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);

        Assert.NotNull(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));
    }

    [Fact]
    public async Task Rows_Of_Expired_Owner_Are_Not_Returned()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);
        Assert.NotNull(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));

        // Owner A crashes: no more heartbeats, the lease runs out. The row
        // itself is never written again, yet resolve must stop returning it.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));
    }

    [Fact]
    public async Task Actor_Row_Is_Not_Returned_Before_ActorRef_Is_Published()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0) with { ActorRef = null },
            ZLinkLocationWriteIntent.NewClaim);

        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));
    }

    [Fact]
    public async Task Peer_List_Excludes_Expired_Owners()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdatePeerAsync(
            InMemoryLocationStoreTests.Peer(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        await fixture.Store.RenewOwnerLeaseAsync(OwnerB, RoutingId.From("node-2"), TimeSpan.FromMinutes(5));
        await fixture.Store.UpdatePeerAsync(
            InMemoryLocationStoreTests.Peer(OwnerB, "tcp://127.0.0.1:5002", "node-2"),
            ZLinkLocationWriteIntent.NewClaim);

        var filter = new ZLinkPeerLocationFilter(MeshName: "play");
        var both = await fixture.Resolvers.ListLivePeersAsync(filter);
        Assert.Equal(2, both.Count);

        // Owner A's lease expires; the peer drops out of the desired set
        // within one polling interval without any row write.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        var survivors = await fixture.Resolvers.ListLivePeersAsync(filter);
        Assert.Single(survivors);
        Assert.Equal(OwnerB, survivors[0].OwnerId);
    }

    [Fact]
    public async Task Route_Rows_Of_Expired_Owner_Are_Not_Returned()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateRouteAsync(
            InMemoryLocationStoreTests.Route(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        var key = new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, "route-1");
        Assert.NotNull(await fixture.Resolvers.ResolveRouteAsync(key));

        // Owner A crashes: its lease runs out and the route row must stop
        // resolving without any row write.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        Assert.Null(await fixture.Resolvers.ResolveRouteAsync(key));
    }

    [Fact]
    public async Task Spot_Address_Resolves_Across_Registered_Meshes()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateSpotAsync(
            InMemoryLocationStoreTests.Spot(OwnerA, "spot-1"), ZLinkLocationWriteIntent.NewClaim);

        var registration = new ZLinkFrameworkRegistration();
        registration.SpotMeshChannels.Add(
            "other", new ZLinkSpotMeshChannelRegistration { ChannelName = "other" });
        registration.SpotMeshChannels.Add(
            "play", new ZLinkSpotMeshChannelRegistration { ChannelName = "play" });
        var spots = new ZLinkSpotMeshLocationResolver(registration, fixture.Resolvers);
        var addresses = new ZLinkLocationAddressResolvers(fixture.Resolvers, spots);

        var address = await addresses.ResolveSpotRefAsync(RoutingId.From("spot-1"));

        Assert.NotNull(address);
        Assert.Equal(RoutingId.From("spot-1"), address.Value.SpotRid);
        Assert.True(address.Value.NodeRid.Size > 0);

        Assert.Null(await addresses.ResolveSpotRefAsync(RoutingId.From("no-such-spot")));
    }

    [Fact]
    public async Task Actor_Address_Is_The_Entry_Spot_For_Entry_Actors_And_The_User_Spot_Otherwise()
    {
        var fixture = await FixtureAsync();
        var registration = new ZLinkFrameworkRegistration();
        var spots = new ZLinkSpotMeshLocationResolver(registration, fixture.Resolvers);
        var addresses = new ZLinkLocationAddressResolvers(fixture.Resolvers, spots);

        var entryActor = InMemoryLocationStoreTests.Actor(OwnerA, 0) with
        {
            LocationKind = ZLinkSpotKind.Entry,
            SpotRid = null
        };
        await fixture.Store.UpdateActorAsync(entryActor, ZLinkLocationWriteIntent.NewClaim);

        var entryAddress = await addresses.ResolveActorSpotRefAsync(entryActor.ActorId);
        Assert.NotNull(entryAddress);
        Assert.Equal(entryAddress.Value.NodeRid, entryAddress.Value.SpotRid);

        var userActor = entryActor with
        {
            ActorId = "actor-2",
            LocationKind = ZLinkSpotKind.User,
            SpotRid = RoutingId.From("spot-7")
        };
        await fixture.Store.UpdateActorAsync(userActor, ZLinkLocationWriteIntent.NewClaim);

        var userAddress = await addresses.ResolveActorSpotRefAsync(userActor.ActorId);
        Assert.NotNull(userAddress);
        Assert.Equal(RoutingId.From("spot-7"), userAddress.Value.SpotRid);
        Assert.Equal(userActor.NodeRid, userAddress.Value.NodeRid);
    }

    [Fact]
    public async Task Peer_Rows_Outside_The_Closed_Value_Sets_Are_Ignored()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        await store.UpdatePeerAsync(
            InMemoryLocationStoreTests.Peer(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);

        // A store (replica lag, another runtime's bug, hand-edited rows)
        // can serve values outside the closed sets; the resolver must drop
        // them instead of feeding them to reconcile.
        var junk = new JunkAppendingPeerStore(
            store,
            InMemoryLocationStoreTests.Peer(OwnerA, "tcp://junk:1", "node-x") with
            {
                Role = (ZLinkLocationRole)99
            },
            InMemoryLocationStoreTests.Peer(OwnerA, "tcp://junk:2", "node-y") with
            {
                AutoConnectType = (ZLinkLocationAutoConnectType)77
            });
        var resolvers = new ZLinkStoreLocationResolvers(
            junk, store, store, store, tracker, new ZLinkObservedLocationGenerations());

        var rows = await resolvers.ListLivePeersAsync(new ZLinkPeerLocationFilter(MeshName: "play"));

        var survivor = Assert.Single(rows);
        Assert.Equal(ZLinkLocationRole.Router, survivor.Role);
    }

    private sealed class JunkAppendingPeerStore(
        IZLinkPeerLocationStore inner,
        params ZLinkPeerLocation[] junk) : IZLinkPeerLocationStore
    {
        public async ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
            ZLinkPeerLocationFilter filter,
            CancellationToken cancellationToken = default)
        {
            var rows = await inner.ListPeersAsync(filter, cancellationToken);
            return [.. rows, .. junk];
        }

        public ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
            ZLinkPeerLocation peer,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            inner.UpdatePeerAsync(peer, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
            ZLinkPeerLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemovePeerAsync(key, owner, cancellationToken);

    }

    [Fact]
    public async Task Actor_Type_Null_And_Empty_Are_The_Same_Key()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0) with { ActorType = "" },
            ZLinkLocationWriteIntent.NewClaim);

        var byEmpty = await fixture.Resolvers.ResolveActorRowAsync(new ZLinkActorLocationKey("actor-1"));
        var byNull = await fixture.Resolvers.ResolveActorRowAsync(new ZLinkActorLocationKey("actor-1"));

        Assert.NotNull(byEmpty);
        Assert.NotNull(byNull);
        Assert.Equal(byEmpty.ActorId, byNull.ActorId);
    }

    [Fact]
    public async Task Older_Generation_From_A_Lagging_Replica_Is_Never_A_Success()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);

        // A replica store that first serves generation 2, then lags back to
        // generation 1, then catches up again.
        var replica = new ScriptedActorStore(
            InMemoryLocationStoreTests.Actor(OwnerA, 2),
            InMemoryLocationStoreTests.Actor(OwnerA, 1),
            InMemoryLocationStoreTests.Actor(OwnerA, 2));
        var resolvers = new ZLinkStoreLocationResolvers(
            store, store, replica, store, tracker, new ZLinkObservedLocationGenerations());

        var first = await resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(2, first!.Generation);

        // The lagging read must not roll the observed view backwards.
        Assert.Null(await resolvers.ResolveActorRowAsync(ActorKey));

        // An equal generation is accepted again.
        var third = await resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(2, third!.Generation);
    }

    private sealed class ScriptedActorStore(params ZLinkActorLocation[] rows) : IZLinkActorLocationStore
    {
        private readonly Queue<ZLinkActorLocation> _rows = new(rows);

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ZLinkActorLocation?>(_rows.Count > 0 ? _rows.Dequeue() : null);

        public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
            ZLinkActorLocation actor,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();
    }

    private sealed class CountingActorStore(IZLinkActorLocationStore inner) : IZLinkActorLocationStore
    {
        public int ResolveCalls { get; private set; }

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key,
            CancellationToken cancellationToken = default)
        {
            ResolveCalls++;
            return inner.ResolveActorAsync(key, cancellationToken);
        }

        public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
            ZLinkActorLocation actor,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            inner.UpdateActorAsync(actor, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemoveActorAsync(key, owner, cancellationToken);

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            inner.ListActorsAsync(filter, page, cancellationToken);
    }

    private static async Task<ResolverFixture> FixtureAsync()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), LeaseTtl);
        await store.RenewOwnerLeaseAsync(OwnerB, RoutingId.From("node-2"), TimeSpan.FromMinutes(5));

        var options = new ZLinkLocationOptions
        {
            // Keep the lease snapshot maximally fresh in unit tests so lease
            // expiry is observed on the next read.
            PollingInterval = TimeSpan.Zero
        };

        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var observed = new ZLinkObservedLocationGenerations();
        var resolvers = new ZLinkStoreLocationResolvers(
            store, store, store, store, tracker, observed);
        return new ResolverFixture(store, resolvers, time);
    }

    private sealed record ResolverFixture(
        ZLinkInMemoryLocationStore Store,
        ZLinkStoreLocationResolvers Resolvers,
        ManualTimeProvider Time);
}
