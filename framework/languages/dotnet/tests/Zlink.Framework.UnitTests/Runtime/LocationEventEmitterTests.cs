using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

/// <summary>
/// Push sources: location-peer (MeshNode descriptor), spot and actor row
/// events, resolve misses and auto-connect desired set changes flow only
/// when the matching source is registered.
/// </summary>
public sealed class LocationEventEmitterTests
{
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(15);

    [Fact]
    public async Task Row_Writes_And_Removes_Publish_Per_Kind_Events_With_The_Registered_Source()
    {
        var fixture = await FixtureAsync();

        var actor = InMemoryLocationStoreTests.Actor("ignored");
        var claimed = await fixture.Runtime.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        await fixture.Runtime.RemoveActorAsync(
            new ZLinkActorLocationKey("play", "actor-1"), claimed.Generation);

        var updated = Assert.IsType<ZLinkLocationActorEvent.RowUpdated>(fixture.Publisher.Events[0]);
        Assert.Equal("actors", updated.SourceName);
        Assert.Equal("actor-1", updated.Actor.ActorId);

        var removed = Assert.IsType<ZLinkLocationActorEvent.RowRemoved>(fixture.Publisher.Events[1]);
        Assert.Equal("actor-1", removed.Key.ActorId);
    }

    [Fact]
    public async Task Failed_Writes_Publish_Nothing()
    {
        var fixture = await FixtureAsync();
        var actor = InMemoryLocationStoreTests.Actor("ignored");
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor("other-owner"), ZLinkLocationWriteIntent.NewClaim);

        var rejected = await fixture.Runtime.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);

        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, rejected.Status);
        Assert.Empty(fixture.Publisher.Events.OfType<ZLinkLocationActorEvent>());
    }

    [Fact]
    public async Task Resolve_Miss_Publishes_A_Miss_Event()
    {
        var fixture = await FixtureAsync();

        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(new ZLinkActorLocationKey("play", "ghost")));

        var miss = Assert.Single(fixture.Publisher.Events.OfType<ZLinkLocationActorEvent.ResolveMiss>());
        Assert.Equal("ghost", miss.Key.ActorId);
    }

    [Fact]
    public async Task Reconcile_Diff_Publishes_DesiredSetChanged_On_The_Peer_Source()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateMeshNodeAsync(
            InMemoryLocationStoreTests.MeshNode(
                "peer-owner",
                "tcp://r:1",
                "r1",
                leaseGeneration: 3),
            ZLinkLocationWriteIntent.NewClaim);

        await fixture.Reconciler.TickAsync();

        var change = Assert.Single(
            fixture.Publisher.Events.OfType<ZLinkLocationPeerEvent.DesiredSetChanged>());
        Assert.Equal("peers", change.SourceName);
        Assert.Equal(["tcp://r:1"], change.Change.ConnectedEndpoints);
        Assert.Empty(change.Change.DisconnectedEndpoints);

        // The local row publish also flows through the peer source.
        Assert.NotEmpty(
            fixture.Publisher.Events.OfType<ZLinkLocationPeerEvent.RowUpdated>());
    }

    [Fact]
    public async Task Unregistered_Sources_Publish_Nothing()
    {
        var fixture = await FixtureAsync(registerSources: false);

        var actor = InMemoryLocationStoreTests.Actor("ignored");
        await fixture.Runtime.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(new ZLinkActorLocationKey("play", "ghost")));
        await fixture.Reconciler.TickAsync();

        Assert.Empty(fixture.Publisher.Events);
    }

    [Fact]
    public async Task Mutation_Events_Advance_The_Shared_Version_Guard()
    {
        var observed = new ZLinkObservedLocationGenerations();
        var emitter = new ZLinkLocationEventEmitter(null, null, null, observed);
        var spot = InMemoryLocationStoreTests.Spot("owner", "spot-1") with { SpotGeneration = 2 };
        var actor = InMemoryLocationStoreTests.Actor("owner") with { MembershipEpoch = 2 };
        var descriptor = InMemoryLocationStoreTests.MeshNode("owner") with { DescriptorRevision = 2 };

        await emitter.SpotRowUpdatedAsync(spot, CancellationToken.None);
        await emitter.ActorRowUpdatedAsync(actor, CancellationToken.None);
        await emitter.DescriptorRowUpdatedAsync(descriptor, CancellationToken.None);

        Assert.False(observed.AcceptSpot(spot with { SpotGeneration = 1 }));
        Assert.False(observed.AcceptActor(actor with { MembershipEpoch = 1 }));
        Assert.False(observed.AcceptDescriptor(descriptor with { DescriptorRevision = 1 }));
    }

    private static async Task<EmitterFixture> FixtureAsync(bool registerSources = true)
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions
        {
            PollingInterval = TimeSpan.Zero,
            // Keep cache entries alive past the lease TTL so the dead-owner
            // invalidation path (not TTL expiry) is what drops them.
        };

        var registration = new ZLinkMonitoringRegistration();
        if (registerSources)
        {
            var model = new ZLinkMonitoringOptionsModel(registration);
            model.AddLocationRuntimeEvents("locations", TimeSpan.FromSeconds(1));
            model.AddLocationPeerEvents("peers");
            model.AddLocationSpotEvents("spots");
            model.AddLocationActorEvents("actors");
        }

        var publisher = new RecordingPublisher();
        var emitter = new ZLinkLocationEventEmitter(registration, publisher, new ZLinkSpotHandleRegistry());

        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, time, emitter);
        await runtime.RenewOwnerLeaseOnceAsync();
        await store.RenewOwnerLeaseAsync("row-owner", RoutingId.From("node-9"), LeaseTtl);
        await store.RenewOwnerLeaseAsync("peer-owner", RoutingId.From("r1"), TimeSpan.FromMinutes(10));
        await store.RenewOwnerLeaseAsync("ignored", RoutingId.From("node-8"), TimeSpan.FromMinutes(10));
        await store.RenewOwnerLeaseAsync("other-owner", RoutingId.From("node-7"), TimeSpan.FromMinutes(10));

        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var observed = new ZLinkObservedLocationGenerations();
        var resolvers = new ZLinkStoreLocationResolvers(
            store, store, store, tracker, observed, emitter);

        var local = new ZLinkAutoConnectLocal(
            ZLinkLocationAutoConnectType.ClientServer, "play", ZLinkLocationRole.Dealer,
            RoutingId.From("local"), "tcp://l:1");
        var localRow = InMemoryLocationStoreTests.MeshNode("ignored", "tcp://l:1", "local");
        var reconciler = new ZLinkAutoConnectReconciler(
            local, localRow, runtime, resolvers, new NullExecutor(), options, time, emitter);

        return new EmitterFixture(store, runtime, resolvers, reconciler, publisher, time);
    }

    private sealed record EmitterFixture(
        ZLinkInMemoryLocationStore Store,
        ZLinkLocationRuntime Runtime,
        ZLinkStoreLocationResolvers Resolvers,
        ZLinkAutoConnectReconciler Reconciler,
        RecordingPublisher Publisher,
        ManualTimeProvider Time);

    private sealed class RecordingPublisher : IZLinkRuntimeEventPublisher
    {
        public List<IZLinkRuntimeEvent> Events { get; } = [];

        public ValueTask PublishAsync<TEvent>(
            TEvent @event,
            CancellationToken cancellationToken)
            where TEvent : IZLinkRuntimeEvent
        {
            Events.Add(@event);
            return ValueTask.CompletedTask;
        }
    }

    private sealed class NullExecutor : IZLinkAutoConnectExecutor
    {
        public bool Connect(ZLinkAutoConnectTarget target) => true;

        public bool Disconnect(ZLinkAutoConnectTarget target) => true;
    }
}
