using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class LocationLifecycleTests
{
    private const string ActorType = "player";
    private const string ActorId = "actor-1";

    [Fact]
    public async Task Actor_Create_Claims_Before_Activation_And_Loser_Never_Activates()
    {
        var fixture = await LifecycleFixture.CreateAsync();
        var nodeA = await fixture.NodeAsync("node-a");
        var nodeB = await fixture.NodeAsync("node-b");
        var activatedB = 0;

        var winner = await nodeA.Lifecycle.ExecuteActorClaimThenActivateAsync(
            ActorType,
            ActorId,
            RoutingId.From("node-a"),
            deactivate: null,
            activate: async _ =>
            {
                // Claim-then-activate: the row must already exist and be
                // owned by this runtime when activation runs.
                var row = await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorType, ActorId));
                Assert.NotNull(row);
                Assert.Equal(nodeA.Runtime.OwnerId, row.OwnerId);
                return "instance-a";
            },
            CancellationToken.None);

        Assert.Equal("instance-a", winner.Activated);
        Assert.Null(winner.ExistingLocation);

        var loser = await nodeB.Lifecycle.ExecuteActorClaimThenActivateAsync<string>(
            ActorType,
            ActorId,
            RoutingId.From("node-b"),
            deactivate: null,
            activate: _ =>
            {
                activatedB++;
                return ValueTask.FromResult("instance-b");
            },
            CancellationToken.None);

        // The losing claimer backs off before any instance exists and gets
        // the winner's freshly resolved location instead.
        Assert.Null(loser.Activated);
        Assert.Equal(0, activatedB);
        Assert.NotNull(loser.ExistingLocation);
        Assert.Equal(RoutingId.From("node-a"), loser.ExistingLocation.NodeRid);
        Assert.Equal(nodeA.Runtime.OwnerId, loser.ExistingLocation.OwnerId);
    }

    [Fact]
    public async Task Actor_Activation_Failure_Rolls_The_Claim_Back()
    {
        var fixture = await LifecycleFixture.CreateAsync();
        var node = await fixture.NodeAsync("node-a");

        await Assert.ThrowsAsync<InvalidOperationException>(async () =>
            await node.Lifecycle.ExecuteActorClaimThenActivateAsync<string>(
                ActorType,
                ActorId,
                RoutingId.From("node-a"),
                deactivate: null,
                activate: _ => throw new InvalidOperationException("factory failed"),
                CancellationToken.None));

        // The rolled-back key is claimable again, by anyone.
        Assert.Null(await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorType, ActorId)));
        var reclaim = await node.Lifecycle.ClaimActorAsync(
            ActorType, ActorId, RoutingId.From("node-a"), null, CancellationToken.None);
        Assert.Equal(ZLinkActorClaimStatus.Claimed, reclaim.Status);
    }

    [Fact]
    public async Task Actor_Join_And_Leave_Renew_Location_Kind_And_Keep_The_Generation()
    {
        var fixture = await LifecycleFixture.CreateAsync();
        var node = await fixture.NodeAsync("node-a");
        var key = new ZLinkActorLocationKey(ActorType, ActorId);

        var claim = await node.Lifecycle.ClaimActorAsync(
            ActorType, ActorId, RoutingId.From("node-a"), null, CancellationToken.None);
        Assert.Equal(ZLinkActorClaimStatus.Claimed, claim.Status);
        var claimed = await fixture.Store.ResolveActorAsync(key);
        var generation = claimed!.Generation;

        await node.Lifecycle.SetActorRefAsync(ActorType, ActorId, "ref-1");
        await node.Lifecycle.NotifyActorJoinedSpotAsync(ActorType, ActorId, RoutingId.From("spot-1"));

        var joined = await fixture.Store.ResolveActorAsync(key);
        Assert.Equal(ZLinkSpotKind.User, joined!.LocationKind);
        Assert.Equal(RoutingId.From("spot-1"), joined.SpotRid);
        Assert.Equal("ref-1", joined.ActorRef);
        Assert.Equal(generation, joined.Generation);

        await node.Lifecycle.NotifyActorLeftSpotAsync(ActorType, ActorId);

        var left = await fixture.Store.ResolveActorAsync(key);
        Assert.Equal(ZLinkSpotKind.Entry, left!.LocationKind);
        Assert.Null(left.SpotRid);
        Assert.Equal(generation, left.Generation);
    }

    [Fact]
    public async Task Actor_Release_And_Spot_Release_Remove_Their_Rows_With_The_Owner_Token()
    {
        var fixture = await LifecycleFixture.CreateAsync();
        var node = await fixture.NodeAsync("node-a");
        var spotRid = RoutingId.From("spot-7");

        await node.Lifecycle.ClaimActorAsync(
            ActorType, ActorId, RoutingId.From("node-a"), null, CancellationToken.None);
        await node.Lifecycle.ReleaseActorAsync(ActorType, ActorId);
        Assert.Null(await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorType, ActorId)));

        var status = await node.Lifecycle.ClaimSpotAsync(
            "mesh",
            spotRid,
            "game",
            RoutingId.From("node-a"),
            ZLinkSpotKind.User,
            "tcp://127.0.0.1:9000",
            deactivate: null);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, status);
        Assert.NotNull(await fixture.Store.ResolveSpotAsync(new ZLinkSpotLocationKey("mesh", spotRid)));

        await node.Lifecycle.ReleaseSpotAsync("mesh", spotRid);
        Assert.Null(await fixture.Store.ResolveSpotAsync(new ZLinkSpotLocationKey("mesh", spotRid)));
    }

    [Fact]
    public async Task OwnershipLost_Deactivates_The_Hosted_Actor_Exactly_Once()
    {
        var fixture = await LifecycleFixture.CreateAsync();
        var nodeA = await fixture.NodeAsync("node-a");
        var nodeB = await fixture.NodeAsync("node-b");
        var deactivated = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        var claim = await nodeA.Lifecycle.ClaimActorAsync(
            ActorType,
            ActorId,
            RoutingId.From("node-a"),
            deactivate: _ =>
            {
                deactivated.TrySetResult();
                return ValueTask.CompletedTask;
            },
            CancellationToken.None);
        Assert.Equal(ZLinkActorClaimStatus.Claimed, claim.Status);

        // Node B fences the row away (unplanned takeover). Node A only
        // learns about it when its next write comes back IgnoredStale.
        var takeover = await nodeB.Runtime.WriteActorAsync(
            InMemoryLocationStoreTests.Actor("ignored", 0),
            ZLinkLocationWriteIntent.Takeover);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, takeover.Status);

        await nodeA.Lifecycle.NotifyActorJoinedSpotAsync(ActorType, ActorId, RoutingId.From("spot-1"));

        await deactivated.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.False(nodeA.Lifecycle.OwnsActor(ActorType, ActorId));

        // The stale owner must not be able to damage the new row.
        await nodeA.Lifecycle.ReleaseActorAsync(ActorType, ActorId);
        var row = await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorType, ActorId));
        Assert.Equal(nodeB.Runtime.OwnerId, row!.OwnerId);
    }

    [Fact]
    public async Task Spot_Remote_Address_Resolver_Uses_The_Row_And_Misses_When_The_Lease_Expires()
    {
        var fixture = await LifecycleFixture.CreateAsync();
        var node = await fixture.NodeAsync("node-a");
        var spotRid = RoutingId.From("spot-9");

        var registration = new ZLinkFrameworkRegistration();
        registration.SpotDiscoveries.Add(
            "mesh", new ZLinkSpotDiscoveryRegistration { ChannelName = "mesh" });
        var resolver = new ZLinkLocationSpotRemoteAddressResolver(
            new ZLinkSpotLocationRidResolver(registration, node.Resolvers));

        var status = await node.Lifecycle.ClaimSpotAsync(
            "mesh",
            spotRid,
            "game",
            RoutingId.From("node-a"),
            ZLinkSpotKind.User,
            "tcp://127.0.0.1:9000",
            deactivate: null);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, status);

        var address = await resolver.ResolveSpotRemoteAddressAsync(spotRid, CancellationToken.None);
        Assert.Equal("mesh", address.RouterChannelId);
        Assert.Equal(RoutingId.From("node-a"), address.TargetNodeRid);
        Assert.Equal(spotRid, address.SpotRid);
        Assert.Equal(ZLinkSpotKind.User, address.SpotKind);

        // No heartbeat: once the owner lease expires the row is stale and
        // the resolver reports a clean miss instead of a wrong node.
        fixture.Time.Advance(fixture.Options.OwnerLeaseTtl + TimeSpan.FromSeconds(1));

        var miss = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await resolver.ResolveSpotRemoteAddressAsync(spotRid, CancellationToken.None));
        Assert.Equal(ZLinkFrameworkErrorKind.SpotRouteNotFound, miss.Kind);
    }

    [Fact]
    public async Task Actor_Session_Route_Binds_Rebinds_With_Takeover_And_Removes()
    {
        var fixture = await LifecycleFixture.CreateAsync();
        var nodeA = await fixture.NodeAsync("node-a");
        var nodeB = await fixture.NodeAsync("node-b");
        var sessionRid = RoutingId.From("session-1");
        var key = new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, sessionRid.ToHex());

        await nodeA.Lifecycle.BindActorSessionRouteAsync(sessionRid, ActorId, RoutingId.From("node-a"));
        var bound = await fixture.Store.ResolveRouteAsync(key);
        Assert.Equal(nodeA.Runtime.OwnerId, bound!.OwnerId);
        Assert.Equal(RoutingId.From("node-a"), bound.OwnerNodeRid);

        // A rebind moves the route to another node: an owner change, so the
        // new owner replaces the live row with Takeover.
        await nodeB.Lifecycle.BindActorSessionRouteAsync(sessionRid, ActorId, RoutingId.From("node-b"));
        var rebound = await fixture.Store.ResolveRouteAsync(key);
        Assert.Equal(nodeB.Runtime.OwnerId, rebound!.OwnerId);
        Assert.Equal(RoutingId.From("node-b"), rebound.OwnerNodeRid);

        await nodeB.Lifecycle.RemoveActorSessionRouteAsync(sessionRid);
        Assert.Null(await fixture.Store.ResolveRouteAsync(key));
    }

    [Fact]
    public async Task Actor_Reconnect_Refreshes_The_Location_And_Only_Rebinds_The_Session()
    {
        var fixture = await LifecycleFixture.CreateAsync();
        var nodeA = await fixture.NodeAsync("node-a");
        var nodeB = await fixture.NodeAsync("node-b");
        var nodeC = await fixture.NodeAsync("node-c");

        await nodeA.Lifecycle.ExecuteActorClaimThenActivateAsync(
            ActorType, ActorId, RoutingId.From("node-a"),
            deactivate: null,
            activate: _ => ValueTask.FromResult("instance-a"),
            CancellationToken.None);

        // Node B has resolved the actor before and still caches node A.
        var cached = await nodeB.Resolvers.ResolveActorRowAsync(new ZLinkActorLocationKey(ActorType, ActorId));
        Assert.Equal(RoutingId.From("node-a"), cached!.NodeRid);

        // The actor moves to node C behind node B's cache.
        await nodeC.Runtime.WriteActorAsync(
            InMemoryLocationStoreTests.Actor("ignored", 0) with { NodeRid = RoutingId.From("node-c") },
            ZLinkLocationWriteIntent.Takeover);
        var moved = await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorType, ActorId));

        // Reconnect at node B: the claim conflicts, the existing location
        // is re-read with Refresh (never from the stale cache), and no
        // local instance is activated.
        var activatedB = 0;
        var reconnect = await nodeB.Lifecycle.ExecuteActorClaimThenActivateAsync<string>(
            ActorType, ActorId, RoutingId.From("node-b"),
            deactivate: null,
            activate: _ =>
            {
                activatedB++;
                return ValueTask.FromResult("instance-b");
            },
            CancellationToken.None);

        Assert.Null(reconnect.Activated);
        Assert.Equal(0, activatedB);
        Assert.Equal(RoutingId.From("node-c"), reconnect.ExistingLocation!.NodeRid);

        // Only the session route is rebound to the refreshed location; the
        // actor row itself stays untouched by the reconnect.
        var sessionRid = RoutingId.From("session-1");
        await nodeB.Lifecycle.BindActorSessionRouteAsync(
            sessionRid, ActorId, reconnect.ExistingLocation.NodeRid);
        var route = await fixture.Store.ResolveRouteAsync(
            new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, sessionRid.ToHex()));
        Assert.Equal(RoutingId.From("node-c"), route!.OwnerNodeRid);

        var after = await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorType, ActorId));
        Assert.Equal(moved!.Generation, after!.Generation);
        Assert.Equal(nodeC.Runtime.OwnerId, after.OwnerId);
    }

    private sealed class LifecycleFixture
    {
        private LifecycleFixture(
            ZLinkInMemoryLocationStore store,
            ManualTimeProvider time,
            ZLinkLocationOptions options)
        {
            Store = store;
            Time = time;
            Options = options;
        }

        public ZLinkInMemoryLocationStore Store { get; }

        public ManualTimeProvider Time { get; }

        public ZLinkLocationOptions Options { get; }

        public static Task<LifecycleFixture> CreateAsync()
        {
            var time = new ManualTimeProvider();
            var store = new ZLinkInMemoryLocationStore(time);
            var options = new ZLinkLocationOptions
            {
                // Keep the lease snapshot maximally fresh so expiry is
                // observed on the next read.
                PollingInterval = TimeSpan.Zero
            };
            return Task.FromResult(new LifecycleFixture(store, time, options));
        }

        public async Task<LifecycleNode> NodeAsync(string nodeRid)
        {
            _ = nodeRid;
            var runtime = new ZLinkLocationRuntime(Options, Store, Store, Store, Store, Store, Time);
            // A single lease renewal instead of StartAsync keeps the
            // heartbeat loop out of the test so lease expiry is driven by
            // the manual clock alone.
            Assert.True(await runtime.RenewOwnerLeaseOnceAsync());
            var resolvers = new ZLinkStoreLocationResolvers(
                Options, Store, Store, Store, Store,
                new ZLinkOwnerLeaseTracker(Store, Options, Time),
                Time);
            return new LifecycleNode(runtime, resolvers, new ZLinkLocationLifecycle(runtime, resolvers));
        }
    }

    private sealed record LifecycleNode(
        ZLinkLocationRuntime Runtime,
        ZLinkStoreLocationResolvers Resolvers,
        ZLinkLocationLifecycle Lifecycle);
}
