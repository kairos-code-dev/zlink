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
        await using var fixture = await LifecycleFixture.CreateAsync();
        var nodeA = await fixture.NodeAsync("node-a");
        var nodeB = await fixture.NodeAsync("node-b");
        var activatedB = 0;
        var key = new ZLinkActorLocationKey(ActorId);

        var winner = await nodeA.ActorOwnership.ExecuteActorClaimThenActivateAsync(
            ActorType,
            ActorId,
            RoutingId.From("node-a"),
            deactivate: null,
            activate: async cancellationToken =>
            {
                // Claim-then-activate: the row must already exist and be
                // owned by this runtime when activation runs. It is still
                // unpublished, so public resolver/query surfaces must hide it.
                var row = await fixture.Store.ResolveActorAsync(key, cancellationToken);
                Assert.NotNull(row);
                Assert.Equal(nodeA.Runtime.OwnerId, row.OwnerId);
                Assert.Null(row.ActorRef);
                Assert.Null(await nodeA.Resolvers.ResolveActorRowAsync(key, cancellationToken));

                var actors = await nodeA.Query.ListActorLocationsAsync(
                    new ZLinkActorLocationFilter(),
                    cancellationToken: cancellationToken);
                Assert.DoesNotContain(actors.Items, actor => actor.ActorId == ActorId);
                return "instance-a";
            },
            CancellationToken.None);

        Assert.Equal("instance-a", winner.Activated);
        Assert.Null(winner.ExistingLocation);

        var loser = await nodeB.ActorOwnership.ExecuteActorClaimThenActivateAsync<string>(
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
        // no public location for the unpublished winner. It must still never
        // activate a local instance.
        Assert.Null(loser.Activated);
        Assert.Equal(0, activatedB);
        Assert.Null(loser.ExistingLocation);

        var claimed = await fixture.Store.ResolveActorAsync(key);
        Assert.NotNull(claimed);
        Assert.Equal(nodeA.Runtime.OwnerId, claimed.OwnerId);
        Assert.Null(claimed.ActorRef);

        await nodeA.ActorOwnership.PublishActorRefAsync(
            ActorId,
            new ActorRef(RoutingId.From("node-a"), ActorId, 1));

        var resolved = await nodeB.Resolvers.ResolveActorRowAsync(key);
        Assert.NotNull(resolved);
        Assert.Equal(RoutingId.From("node-a"), resolved.NodeRid);
        Assert.Equal(nodeA.Runtime.OwnerId, resolved.OwnerId);
        Assert.Equal(ActorId, resolved.ActorRef?.ActorId);

        var published = await nodeB.Query.ListActorLocationsAsync(new ZLinkActorLocationFilter());
        Assert.Equal(ActorId, Assert.Single(published.Items).ActorId);
    }

    [Fact]
    public async Task Actor_Activation_Failure_Rolls_The_Claim_Back()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var node = await fixture.NodeAsync("node-a");

        await Assert.ThrowsAsync<InvalidOperationException>(async () =>
            await node.ActorOwnership.ExecuteActorClaimThenActivateAsync<string>(
                ActorType,
                ActorId,
                RoutingId.From("node-a"),
                deactivate: null,
                activate: _ => throw new InvalidOperationException("factory failed"),
                CancellationToken.None));

        // The rolled-back key is claimable again, by anyone.
        Assert.Null(await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorId)));
        var reclaim = await node.ActorOwnership.ExecuteActorClaimThenActivateAsync(
            ActorType,
            ActorId,
            RoutingId.From("node-a"),
            deactivate: null,
            activate: static _ => ValueTask.FromResult("reclaimed"),
            CancellationToken.None);
        Assert.Equal("reclaimed", reclaim.Activated);
    }

    [Fact]
    public async Task Actor_Activation_Failure_Reconciles_A_Failed_Claim_Remove()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var actorStore = new ControlledActorStore(fixture.Store)
        {
            RemoveFailure = new InvalidOperationException("remove failed")
        };
        var node = await fixture.NodeAsync("node-a", actorStore);
        await using var lifecycle = node.Lifecycle;

        var failure = await Assert.ThrowsAsync<AggregateException>(async () =>
            await node.ActorOwnership.ExecuteActorClaimThenActivateAsync<string>(
                ActorType,
                ActorId,
                RoutingId.From("node-a"),
                deactivate: null,
                activate: _ => throw new InvalidOperationException("factory failed"),
                CancellationToken.None));

        Assert.Contains(
            failure.InnerExceptions,
            exception => exception is InvalidOperationException { Message: "factory failed" });
        for (var attempt = 0; attempt < 50 && node.ActorOwnership.OwnsActor(ActorId); attempt++)
            await Task.Delay(20);

        Assert.False(node.ActorOwnership.OwnsActor(ActorId));
        Assert.Null(await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorId)));
        var reclaim = await node.ActorOwnership.ExecuteActorClaimThenActivateAsync(
            ActorType,
            ActorId,
            RoutingId.From("node-a"),
            deactivate: null,
            activate: static _ => ValueTask.FromResult("reclaimed"),
            CancellationToken.None);
        Assert.Equal("reclaimed", reclaim.Activated);
    }

    [Fact]
    public async Task Actor_AlreadyOwned_Does_Not_Activate_A_Second_Instance()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var node = await fixture.NodeAsync("node-a");
        var key = new ZLinkActorLocationKey(ActorId);

        var first = await node.ActorOwnership.ExecuteActorClaimThenActivateAsync(
            ActorType,
            ActorId,
            RoutingId.From("node-a"),
            deactivate: null,
            activate: _ => ValueTask.FromResult("instance-a"),
            CancellationToken.None);
        Assert.Equal("instance-a", first.Activated);

        var secondActivated = 0;
        var second = await node.ActorOwnership.ExecuteActorClaimThenActivateAsync<string>(
            ActorType,
            ActorId,
            RoutingId.From("node-a"),
            deactivate: null,
            activate: _ =>
            {
                secondActivated++;
                throw new InvalidOperationException("duplicate activation");
            },
            CancellationToken.None);

        Assert.Null(second.Activated);
        Assert.Null(second.ExistingLocation);
        Assert.Equal(0, secondActivated);
        Assert.True(node.ActorOwnership.OwnsActor(ActorId));
        Assert.NotNull(await fixture.Store.ResolveActorAsync(key));
    }

    [Fact]
    public async Task Actor_Join_And_Leave_Renew_Location_Kind_And_Keep_The_Generation()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var node = await fixture.NodeAsync("node-a");
        var key = new ZLinkActorLocationKey(ActorId);

        await CreateTrackedActorAsync(node);
        var claimed = await fixture.Store.ResolveActorAsync(key);
        var generation = claimed!.Generation;

        await node.ActorOwnership.PublishActorRefAsync(
            ActorId,
            new ActorRef(RoutingId.From("node-1"), ActorId, 1));
        await node.ActorOwnership.NotifyActorJoinedSpotAsync(ActorId, RoutingId.From("spot-1"));

        var joined = await fixture.Store.ResolveActorAsync(key);
        Assert.Equal(ZLinkSpotKind.User, joined!.LocationKind);
        Assert.Equal(RoutingId.From("spot-1"), joined.SpotRid);
        Assert.Equal(ActorId, joined.ActorRef?.ActorId);
        Assert.Equal(generation, joined.Generation);

        await node.ActorOwnership.NotifyActorLeftSpotAsync(ActorId);

        var left = await fixture.Store.ResolveActorAsync(key);
        Assert.Equal(ZLinkSpotKind.Entry, left!.LocationKind);
        Assert.Null(left.SpotRid);
        Assert.Equal(generation, left.Generation);
    }

    [Fact]
    public async Task Actor_Release_And_Spot_Release_Remove_Their_Rows_With_The_Owner_Token()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var node = await fixture.NodeAsync("node-a");
        var spotRid = RoutingId.From("spot-7");

        await CreateTrackedActorAsync(node);
        await node.ActorOwnership.ReleaseActorAsync(ActorId);
        Assert.Null(await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorId)));

        var status = await node.SpotLocations.ClaimAsync(
            "mesh",
            spotRid,
            "game",
            RoutingId.From("node-a"),
            ZLinkSpotKind.User,
            "tcp://127.0.0.1:9000",
            deactivate: null);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, status);
        Assert.NotNull(await fixture.Store.ResolveSpotAsync(new ZLinkSpotLocationKey("mesh", spotRid)));

        await node.SpotLocations.ReleaseAsync("mesh", spotRid);
        Assert.Null(await fixture.Store.ResolveSpotAsync(new ZLinkSpotLocationKey("mesh", spotRid)));
    }

    [Fact]
    public async Task Actor_Concurrent_Releases_Await_The_Same_Store_Outcome()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var controlled = new ControlledActorStore(fixture.Store)
        {
            RemoveGate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously)
        };
        var node = await fixture.NodeAsync("node-a", controlled);
        await CreateTrackedActorAsync(node);

        var first = node.ActorOwnership.ReleaseActorAsync(ActorId).AsTask();
        await controlled.RemoveStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var second = node.ActorOwnership.ReleaseActorAsync(ActorId).AsTask();

        Assert.Equal(1, controlled.RemoveCalls);
        Assert.False(first.IsCompleted);
        Assert.False(second.IsCompleted);

        controlled.RemoveGate.SetResult();
        await Task.WhenAll(first, second);

        Assert.Equal(1, controlled.RemoveCalls);
        Assert.False(node.ActorOwnership.OwnsActor(ActorId));
    }

    [Fact]
    public async Task Actor_Failed_Renew_Does_Not_Become_The_Base_Of_The_Next_Write()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var controlled = new ControlledActorStore(fixture.Store);
        var node = await fixture.NodeAsync("node-a", controlled);
        await CreateTrackedActorAsync(node);

        controlled.RejectNextRenew = true;
        await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await node.ActorOwnership.PublishActorRefAsync(
                ActorId,
                new ActorRef(RoutingId.From("node-a"), ActorId, 1)));

        await node.ActorOwnership.NotifyActorJoinedSpotAsync(
            ActorId,
            RoutingId.From("spot-1"));

        var row = await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorId));
        Assert.NotNull(row);
        Assert.Null(row.ActorRef);
        Assert.Equal(ZLinkSpotKind.User, row.LocationKind);
        Assert.Equal(RoutingId.From("spot-1"), row.SpotRid);
    }

    [Fact]
    public async Task Actor_Release_Waits_For_The_Preceding_Renew_And_Uses_Its_Committed_Generation()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var controlled = new ControlledActorStore(fixture.Store)
        {
            RenewGate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously)
        };
        var node = await fixture.NodeAsync("node-a", controlled);
        await CreateTrackedActorAsync(node);

        var renew = node.ActorOwnership.NotifyActorJoinedSpotAsync(
            ActorId,
            RoutingId.From("spot-1")).AsTask();
        await controlled.RenewStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var release = node.ActorOwnership.ReleaseActorAsync(ActorId).AsTask();

        Assert.Equal(0, controlled.RemoveCalls);
        controlled.RenewGate.SetResult();
        await renew;
        await release;

        Assert.Equal(1, controlled.RemoveCalls);
        Assert.NotNull(controlled.LastRemoveOwner);
        Assert.True(controlled.LastRemoveOwner.Value.Generation > 0);
        Assert.Null(await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorId)));
    }

    [Fact]
    public async Task Spot_Restart_On_Same_Node_Takes_Over_The_Live_Row()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var original = await fixture.NodeAsync("node-a");
        var restarted = await fixture.NodeAsync("node-a");
        var spotRid = RoutingId.From("spot-7");
        var key = new ZLinkSpotLocationKey("mesh", spotRid);

        var first = await original.SpotLocations.ClaimAsync(
            "mesh",
            spotRid,
            "game",
            RoutingId.From("node-a"),
            ZLinkSpotKind.User,
            "tcp://127.0.0.1:9000",
            deactivate: null);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, first);
        var firstRow = await fixture.Store.ResolveSpotAsync(key);

        var takeover = await restarted.SpotLocations.ClaimAsync(
            "mesh",
            spotRid,
            "game",
            RoutingId.From("node-a"),
            ZLinkSpotKind.User,
            "tcp://127.0.0.1:9001",
            deactivate: null);

        Assert.Equal(ZLinkLocationWriteStatus.Stored, takeover);
        var current = await fixture.Store.ResolveSpotAsync(key);
        Assert.Equal(restarted.Runtime.OwnerId, current!.OwnerId);
        Assert.Equal(RoutingId.From("node-a"), current.NodeRid);
        Assert.Equal("tcp://127.0.0.1:9001", current.RouteEndpoint);
        Assert.True(current.Generation > firstRow!.Generation);

        await original.SpotLocations.ReleaseAsync("mesh", spotRid);
        var afterStaleRelease = await fixture.Store.ResolveSpotAsync(key);
        Assert.Equal(restarted.Runtime.OwnerId, afterStaleRelease!.OwnerId);
    }

    [Fact]
    public async Task Spot_Claim_From_Different_Node_Does_Not_Take_Over_A_Live_Row()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var nodeA = await fixture.NodeAsync("node-a");
        var nodeB = await fixture.NodeAsync("node-b");
        var spotRid = RoutingId.From("spot-7");
        var key = new ZLinkSpotLocationKey("mesh", spotRid);

        var first = await nodeA.SpotLocations.ClaimAsync(
            "mesh",
            spotRid,
            "game",
            RoutingId.From("node-a"),
            ZLinkSpotKind.User,
            "tcp://127.0.0.1:9000",
            deactivate: null);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, first);

        var conflict = await nodeB.SpotLocations.ClaimAsync(
            "mesh",
            spotRid,
            "game",
            RoutingId.From("node-b"),
            ZLinkSpotKind.User,
            "tcp://127.0.0.1:9001",
            deactivate: null);

        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, conflict);
        var row = await fixture.Store.ResolveSpotAsync(key);
        Assert.Equal(nodeA.Runtime.OwnerId, row!.OwnerId);
        Assert.Equal(RoutingId.From("node-a"), row.NodeRid);
    }

    [Fact]
    public async Task OwnershipLost_Deactivates_The_Hosted_Actor_Exactly_Once()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var nodeA = await fixture.NodeAsync("node-a");
        var nodeB = await fixture.NodeAsync("node-b");
        var deactivated = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        await CreateTrackedActorAsync(
            nodeA,
            _ =>
            {
                deactivated.TrySetResult();
                return ValueTask.CompletedTask;
            });

        // Node B fences the row away (unplanned takeover). Node A only
        // learns about it when its next write comes back IgnoredStale.
        var takeover = await nodeB.Runtime.WriteActorAsync(
            InMemoryLocationStoreTests.Actor("ignored", 0),
            ZLinkLocationWriteIntent.Takeover);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, takeover.Status);

        var stale = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await nodeA.ActorOwnership.NotifyActorJoinedSpotAsync(ActorId, RoutingId.From("spot-1")));
        Assert.Equal(ZLinkFrameworkErrorKind.ActorLocationStale, stale.Kind);

        await deactivated.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.False(nodeA.ActorOwnership.OwnsActor(ActorId));

        // The stale owner must not be able to damage the new row.
        await nodeA.ActorOwnership.ReleaseActorAsync(ActorId);
        var row = await fixture.Store.ResolveActorAsync(new ZLinkActorLocationKey(ActorId));
        Assert.Equal(nodeB.Runtime.OwnerId, row!.OwnerId);
    }

    [Fact]
    public async Task LocationLifecycle_DisposeAsync_WaitsForOwnershipLossDeactivation_And_IsIdempotent()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var nodeA = await fixture.NodeAsync("node-a");
        var nodeB = await fixture.NodeAsync("node-b");
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var completed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        var releaseDeactivation = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        await CreateTrackedActorAsync(
            nodeA,
            async _ =>
            {
                started.TrySetResult();
                await releaseDeactivation.Task;
                completed.TrySetResult();
            });
        await nodeB.Runtime.WriteActorAsync(
            InMemoryLocationStoreTests.Actor("ignored", 0),
            ZLinkLocationWriteIntent.Takeover);

        await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await nodeA.ActorOwnership.NotifyActorJoinedSpotAsync(ActorId, RoutingId.From("spot-1")));
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var firstDispose = nodeA.Lifecycle.DisposeAsync().AsTask();
        var secondDispose = nodeA.Lifecycle.DisposeAsync().AsTask();
        Assert.Same(firstDispose, secondDispose);
        await Task.Delay(30);
        Assert.False(firstDispose.IsCompleted);

        releaseDeactivation.TrySetResult();
        await firstDispose.WaitAsync(TimeSpan.FromSeconds(5));
        await completed.Task.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task Spot_Handle_Resolver_Uses_The_Row_And_Misses_When_The_Lease_Expires()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var node = await fixture.NodeAsync("node-a");
        var spotRid = RoutingId.From("spot-9");

        var registration = new ZLinkFrameworkRegistration();
        registration.SpotMeshChannels.Add(
            "mesh", new ZLinkSpotMeshChannelRegistration { ChannelName = "mesh" });
        var spots = new ZLinkSpotMeshLocationResolver(registration, node.Resolvers);
        var resolver = new ZLinkLocationAddressResolvers(
            node.Resolvers,
            spots,
            new ZLinkSpotHandleRegistry());

        var status = await node.SpotLocations.ClaimAsync(
            "mesh",
            spotRid,
            "game",
            RoutingId.From("node-a"),
            ZLinkSpotKind.User,
            "tcp://127.0.0.1:9000",
            deactivate: null);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, status);

        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await resolver.ResolveSpotHandleAsync(spotRid, CancellationToken.None));
        Assert.Equal("mesh", handle.Snapshot.RouterChannelId);
        Assert.Equal(RoutingId.From("node-a"), handle.Snapshot.NodeRid);
        Assert.Equal(spotRid, handle.SpotRid);

        // No heartbeat: once the owner lease expires the row is stale and
        // the resolver reports a clean miss instead of a wrong node.
        fixture.Time.Advance(fixture.Options.OwnerLeaseTtl + TimeSpan.FromSeconds(1));

        Assert.Null(await resolver.ResolveSpotHandleAsync(spotRid, CancellationToken.None));
    }

    [Fact]
    public async Task Actor_Session_Route_Binds_Rebinds_With_Takeover_And_Removes()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var nodeA = await fixture.NodeAsync("node-a");
        var nodeB = await fixture.NodeAsync("node-b");
        var sessionRid = RoutingId.From("session-1");
        var key = new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, sessionRid.ToHex());

        await nodeA.SessionRoutes.BindAsync(sessionRid, ActorId, RoutingId.From("node-a"));
        var bound = await fixture.Store.ResolveRouteAsync(key);
        Assert.Equal(nodeA.Runtime.OwnerId, bound!.OwnerId);
        Assert.Equal(RoutingId.From("node-a"), bound.OwnerNodeRid);

        // A rebind moves the route to another node: an owner change, so the
        // new owner replaces the live row with Takeover.
        await nodeB.SessionRoutes.BindAsync(sessionRid, ActorId, RoutingId.From("node-b"));
        var rebound = await fixture.Store.ResolveRouteAsync(key);
        Assert.Equal(nodeB.Runtime.OwnerId, rebound!.OwnerId);
        Assert.Equal(RoutingId.From("node-b"), rebound.OwnerNodeRid);

        await nodeB.SessionRoutes.RemoveAsync(sessionRid);
        Assert.Null(await fixture.Store.ResolveRouteAsync(key));
    }

    [Fact]
    public async Task ActorSessionRouteLifecycle_SerializesBindAndRemove_AndRetriesRemoveFailure()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var releaseBind = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var routeStore = new ControlledRouteStore(fixture.Store)
        {
            UpdateGate = releaseBind,
            RemoveFailuresRemaining = 1
        };
        var node = await fixture.NodeAsync("node-a", routeStore: routeStore);
        var sessionRid = RoutingId.From("session-serialized");
        var key = new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, sessionRid.ToHex());

        node.SessionRoutes.OnActorSessionBound(sessionRid, ActorId, RoutingId.From("node-a"));
        await routeStore.UpdateStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        node.SessionRoutes.OnActorSessionUnbound(sessionRid);
        Assert.Equal(0, routeStore.RemoveCalls);

        releaseBind.TrySetResult();
        await WaitUntilAsync(() => routeStore.RemoveCalls >= 2, TimeSpan.FromSeconds(5));
        await WaitUntilAsync(
            async () => await fixture.Store.ResolveRouteAsync(key) is null,
            TimeSpan.FromSeconds(5));
        await node.Lifecycle.PauseBackgroundWorkAsync();

        Assert.Equal(2, routeStore.RemoveCalls);
    }

    [Fact]
    public async Task Actor_Reconnect_Refreshes_The_Location_And_Only_Rebinds_The_Session()
    {
        await using var fixture = await LifecycleFixture.CreateAsync();
        var nodeA = await fixture.NodeAsync("node-a");
        var nodeB = await fixture.NodeAsync("node-b");
        var nodeC = await fixture.NodeAsync("node-c");
        var key = new ZLinkActorLocationKey(ActorId);

        var activation = await nodeA.ActorOwnership.ExecuteActorClaimThenActivateAsync(
            ActorType, ActorId, RoutingId.From("node-a"),
            deactivate: null,
            activate: async cancellationToken =>
            {
                var claimed = await fixture.Store.ResolveActorAsync(key, cancellationToken);
                Assert.NotNull(claimed);
                Assert.Equal(nodeA.Runtime.OwnerId, claimed.OwnerId);
                Assert.Null(claimed.ActorRef);
                Assert.Null(await nodeA.Resolvers.ResolveActorRowAsync(key, cancellationToken));

                var actors = await nodeA.Query.ListActorLocationsAsync(
                    new ZLinkActorLocationFilter(),
                    cancellationToken: cancellationToken);
                Assert.DoesNotContain(actors.Items, actor => actor.ActorId == ActorId);
                return "instance-a";
            },
            CancellationToken.None);
        Assert.Equal("instance-a", activation.Activated);

        await nodeA.ActorOwnership.PublishActorRefAsync(
            ActorId,
            new ActorRef(RoutingId.From("node-a"), ActorId, 1));

        var visible = await nodeB.Query.ListActorLocationsAsync(new ZLinkActorLocationFilter());
        Assert.Equal(ActorId, Assert.Single(visible.Items).ActorId);

        // Node B has resolved the actor before and still caches node A.
        var cached = await nodeB.Resolvers.ResolveActorRowAsync(key);
        Assert.Equal(RoutingId.From("node-a"), cached!.NodeRid);

        // The actor moves to node C behind node B's cache.
        await nodeC.Runtime.WriteActorAsync(
            InMemoryLocationStoreTests.Actor("ignored", 0) with { NodeRid = RoutingId.From("node-c") },
            ZLinkLocationWriteIntent.Takeover);
        var moved = await fixture.Store.ResolveActorAsync(key);
        var refreshed = await nodeB.Resolvers.ResolveActorRowAsync(key);
        Assert.Equal(RoutingId.From("node-c"), refreshed!.NodeRid);

        // Reconnect at node B: the claim conflicts, the existing location
        // is re-read with Refresh (never from the stale cache), and no
        // local instance is activated.
        var activatedB = 0;
        var reconnect = await nodeB.ActorOwnership.ExecuteActorClaimThenActivateAsync<string>(
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
        await nodeB.SessionRoutes.BindAsync(
            sessionRid, ActorId, reconnect.ExistingLocation.NodeRid);
        var route = await fixture.Store.ResolveRouteAsync(
            new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, sessionRid.ToHex()));
        Assert.Equal(RoutingId.From("node-c"), route!.OwnerNodeRid);

        var after = await fixture.Store.ResolveActorAsync(key);
        Assert.Equal(moved!.Generation, after!.Generation);
        Assert.Equal(nodeC.Runtime.OwnerId, after.OwnerId);
    }

    private static async ValueTask CreateTrackedActorAsync(
        LifecycleNode node,
        Func<CancellationToken, ValueTask>? deactivate = null)
    {
        var activation = await node.ActorOwnership.ExecuteActorClaimThenActivateAsync(
            ActorType,
            ActorId,
            RoutingId.From("node-a"),
            deactivate,
            static _ => ValueTask.FromResult(new object()),
            CancellationToken.None);
        Assert.NotNull(activation.Activated);
        Assert.Null(activation.ExistingLocation);
    }

    private sealed class LifecycleFixture : IAsyncDisposable
    {
        private readonly List<LifecycleNode> _nodes = [];

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

        public async Task<LifecycleNode> NodeAsync(
            string nodeRid,
            IZLinkActorLocationStore? actorStore = null,
            IZLinkRouteLocationStore? routeStore = null)
        {
            _ = nodeRid;
            var runtime = new ZLinkLocationRuntime(
                Options,
                Store,
                Store,
                Store,
                actorStore ?? Store,
                routeStore ?? Store,
                Store,
                Time);
            // A single lease renewal instead of StartAsync keeps the
            // heartbeat loop out of the test so lease expiry is driven by
            // the manual clock alone.
            Assert.True(await runtime.RenewOwnerLeaseOnceAsync());
            var tracker = new ZLinkOwnerLeaseTracker(Store, Options, Time);
            var observed = new ZLinkObservedLocationGenerations();
            var resolvers = new ZLinkStoreLocationResolvers(
                Store, Store, Store, Store,
                tracker,
                observed);
            var query = new ZLinkLocationRuntimeQueryService(
                Options, Store, Store, Store, Store, tracker, runtime, observed);
            var node = new LifecycleNode(
                runtime,
                resolvers,
                query,
                new ZLinkLocationLifecycle(runtime, resolvers));
            _nodes.Add(node);
            return node;
        }

        public async ValueTask DisposeAsync()
        {
            for (var index = _nodes.Count - 1; index >= 0; index--)
            {
                await _nodes[index].Lifecycle.DisposeAsync();
                await _nodes[index].Runtime.DisposeAsync();
            }
            _nodes.Clear();
        }
    }

    private sealed record LifecycleNode(
        ZLinkLocationRuntime Runtime,
        ZLinkStoreLocationResolvers Resolvers,
        IZLinkLocationRuntimeQuery Query,
        ZLinkLocationLifecycle Lifecycle)
    {
        public ZLinkActorSessionRouteLifecycle SessionRoutes => Lifecycle.ActorSessionRoutes;

        public ZLinkSpotLocationLifecycle SpotLocations => Lifecycle.SpotLocations;

        public ZLinkActorOwnershipCoordinator ActorOwnership => Lifecycle.ActorOwnership;
    }

    private static async Task WaitUntilAsync(Func<bool> condition, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (!condition())
        {
            if (DateTime.UtcNow >= deadline) throw new TimeoutException("Condition was not met.");
            await Task.Delay(10);
        }
    }

    private static async Task WaitUntilAsync(Func<Task<bool>> condition, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (!await condition())
        {
            if (DateTime.UtcNow >= deadline) throw new TimeoutException("Condition was not met.");
            await Task.Delay(10);
        }
    }

    private sealed class ControlledRouteStore(IZLinkRouteLocationStore inner) : IZLinkRouteLocationStore
    {
        private int _removeCalls;

        public TaskCompletionSource? UpdateGate { get; init; }

        public TaskCompletionSource UpdateStarted { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public int RemoveFailuresRemaining { get; set; }

        public int RemoveCalls => Volatile.Read(ref _removeCalls);

        public async ValueTask<ZLinkLocationWriteResult> UpdateRouteAsync(
            ZLinkRouteLocation route,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default)
        {
            UpdateStarted.TrySetResult();
            if (UpdateGate is not null) await UpdateGate.Task.WaitAsync(cancellationToken);
            return await inner.UpdateRouteAsync(route, intent, cancellationToken);
        }

        public async ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
            ZLinkRouteLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref _removeCalls);
            if (RemoveFailuresRemaining > 0)
            {
                RemoveFailuresRemaining--;
                throw new InvalidOperationException("transient route remove failure");
            }

            return await inner.RemoveRouteAsync(key, owner, cancellationToken);
        }

        public ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
            ZLinkRouteLocationKey key,
            CancellationToken cancellationToken = default)
            => inner.ResolveRouteAsync(key, cancellationToken);

        public ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
            ZLinkRouteLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default)
            => inner.ListRoutesAsync(filter, page, cancellationToken);
    }

    private sealed class ControlledActorStore(IZLinkActorLocationStore inner) : IZLinkActorLocationStore
    {
        public bool RejectNextRenew { get; set; }

        public TaskCompletionSource? RenewGate { get; init; }

        public TaskCompletionSource? RemoveGate { get; init; }

        public TaskCompletionSource RenewStarted { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource RemoveStarted { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public int RemoveCalls { get; private set; }

        public Exception? RemoveFailure { get; set; }

        public ZLinkLocationOwnerToken? LastRemoveOwner { get; private set; }

        public async ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
            ZLinkActorLocation actor,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default)
        {
            if (intent == ZLinkLocationWriteIntent.Renew)
            {
                RenewStarted.TrySetResult();
                if (RenewGate is not null)
                    await RenewGate.Task.WaitAsync(cancellationToken);
                if (RejectNextRenew)
                {
                    RejectNextRenew = false;
                    return ZLinkLocationWriteResult.RejectedConflict;
                }
            }

            return await inner.UpdateActorAsync(actor, intent, cancellationToken);
        }

        public async ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default)
        {
            RemoveCalls++;
            LastRemoveOwner = owner;
            RemoveStarted.TrySetResult();
            if (RemoveGate is not null)
                await RemoveGate.Task.WaitAsync(cancellationToken);
            if (RemoveFailure is { } failure)
            {
                RemoveFailure = null;
                throw failure;
            }
            return await inner.RemoveActorAsync(key, owner, cancellationToken);
        }

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key,
            CancellationToken cancellationToken = default)
            => inner.ResolveActorAsync(key, cancellationToken);

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default)
            => inner.ListActorsAsync(filter, page, cancellationToken);
    }
}
