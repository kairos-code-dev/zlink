using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

/// <summary>
/// Draft 20.5 push sources: location-peer/spot/actor/route row events,
/// resolve misses, auto-connect desired set changes, and location-runtime
/// cache invalidations flow only when the matching source is registered.
/// </summary>
public sealed class LocationEventEmitterTests
{
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(15);

    [Fact]
    public async Task Row_Writes_And_Removes_Publish_Per_Kind_Events_With_The_Registered_Source()
    {
        var fixture = await FixtureAsync();

        var actor = InMemoryLocationStoreTests.Actor("ignored", 0);
        var claimed = await fixture.Runtime.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        await fixture.Runtime.RemoveActorAsync(
            new ZLinkActorLocationKey("actor-1"), claimed.Generation);

        var updated = Assert.IsType<ZLinkLocationActorEvent>(fixture.Publisher.Events[0]);
        Assert.Equal("actors", updated.SourceName);
        Assert.Equal(ZLinkLocationActorEventKind.RowUpdated, updated.Event);
        Assert.Equal(claimed.Generation, updated.Actor!.Generation);

        var removed = Assert.IsType<ZLinkLocationActorEvent>(fixture.Publisher.Events[1]);
        Assert.Equal(ZLinkLocationActorEventKind.RowRemoved, removed.Event);
        Assert.Equal("actor-1", removed.Key.ActorId);
        Assert.Null(removed.Actor);
    }

    [Fact]
    public async Task Failed_Writes_Publish_Nothing()
    {
        var fixture = await FixtureAsync();
        var actor = InMemoryLocationStoreTests.Actor("ignored", 0);
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor("other-owner", 0), ZLinkLocationWriteIntent.NewClaim);

        var rejected = await fixture.Runtime.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);

        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, rejected.Status);
        Assert.Empty(fixture.Publisher.Events.OfType<ZLinkLocationActorEvent>());
    }

    [Fact]
    public async Task Resolve_Miss_Publishes_A_Miss_Event()
    {
        var fixture = await FixtureAsync();

        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(new ZLinkActorLocationKey("ghost")));

        var miss = Assert.Single(fixture.Publisher.Events.OfType<ZLinkLocationActorEvent>());
        Assert.Equal(ZLinkLocationActorEventKind.ResolveMiss, miss.Event);
        Assert.Equal("ghost", miss.Key.ActorId);
    }

    [Fact]
    public async Task Reconcile_Diff_Publishes_DesiredSetChanged_On_The_Peer_Source()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdatePeerAsync(
            new ZLinkPeerLocation(
                ZLinkLocationAutoConnectType.ClientServer, "play", RoutingId.From("r1"),
                ZLinkLocationRole.Router, "tcp://r:1", 100, 0, null, null,
                "peer-owner", 0, default),
            ZLinkLocationWriteIntent.NewClaim);

        await fixture.Reconciler.TickAsync();

        var change = Assert.Single(
            fixture.Publisher.Events.OfType<ZLinkLocationPeerEvent>(),
            static @event => @event.Event == ZLinkLocationPeerEventKind.DesiredSetChanged);
        Assert.Equal("peers", change.SourceName);
        Assert.Equal(["tcp://r:1"], change.DesiredSetChange!.Value.ConnectedEndpoints);
        Assert.Empty(change.DesiredSetChange!.Value.DisconnectedEndpoints);

        // The local row publish also flows through the peer source.
        Assert.Contains(
            fixture.Publisher.Events.OfType<ZLinkLocationPeerEvent>(),
            static @event => @event.Event == ZLinkLocationPeerEventKind.RowUpdated);
    }

    [Fact]
    public async Task Unregistered_Sources_Publish_Nothing()
    {
        var fixture = await FixtureAsync(registerSources: false);

        var actor = InMemoryLocationStoreTests.Actor("ignored", 0);
        await fixture.Runtime.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(new ZLinkActorLocationKey("ghost")));
        await fixture.Reconciler.TickAsync();

        Assert.Empty(fixture.Publisher.Events);
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
            model.AddLocationRouteEvents("routes");
        }

        var publisher = new RecordingPublisher();
        var emitter = new ZLinkLocationEventEmitter(registration, publisher);

        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, store, time, emitter);
        await runtime.RenewOwnerLeaseOnceAsync();
        await store.RenewOwnerLeaseAsync("row-owner", RoutingId.From("node-9"), LeaseTtl);
        await store.RenewOwnerLeaseAsync("peer-owner", RoutingId.From("r1"), TimeSpan.FromMinutes(10));
        await store.RenewOwnerLeaseAsync("ignored", RoutingId.From("node-8"), TimeSpan.FromMinutes(10));
        await store.RenewOwnerLeaseAsync("other-owner", RoutingId.From("node-7"), TimeSpan.FromMinutes(10));

        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var observed = new ZLinkObservedLocationGenerations();
        var resolvers = new ZLinkStoreLocationResolvers(
            store, store, store, store, tracker, observed, emitter);

        var local = new ZLinkAutoConnectLocal(
            ZLinkLocationAutoConnectType.ClientServer, "play", ZLinkLocationRole.Dealer,
            RoutingId.From("local"), "tcp://l:1");
        var localRow = new ZLinkPeerLocation(
            local.AutoConnectType, local.MeshName, local.NodeRid, local.Role, local.Endpoint,
            100, 0, null, null, "ignored", 0, default);
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
        public void Connect(ZLinkAutoConnectTarget target)
        {
        }

        public void Disconnect(ZLinkAutoConnectTarget target)
        {
        }
    }
}
