using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class AutoConnectLoopTests
{
    [Fact]
    public async Task Unchanged_Change_Stamp_Skips_The_List_Query()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, time);
        await runtime.RenewOwnerLeaseOnceAsync();
        await store.RenewOwnerLeaseAsync("peer-owner", RoutingId.From("peer-node"), TimeSpan.FromMinutes(10));

        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var resolvers = new ZLinkStoreLocationResolvers(options, store, store, store, store, tracker, time);
        var countingResolver = new CountingPeerResolver(resolvers);
        var local = new ZLinkAutoConnectLocal(
            ZLinkLocationAutoConnectType.ClientServer, "play", ZLinkLocationRole.Dealer,
            RoutingId.From("local"), "tcp://l:1");
        var localRow = new ZLinkPeerLocation(
            local.AutoConnectType, local.MeshName, local.NodeRid, local.Role, local.Endpoint,
            100, 0, null, null, "ignored", 0, default);
        var reconciler = new ZLinkAutoConnectReconciler(
            local, localRow, runtime, countingResolver, new NullExecutor(), options, time);
        var loop = new ZLinkAutoConnectLoop(reconciler, local, options, stampStore: store, timeProvider: time);

        // First tick always reads the list (and publishes the local row,
        // which bumps the stamp), so the second tick still reads once more
        // before the stamp settles.
        await loop.TickAsync();
        await loop.TickAsync();
        var reads = countingResolver.ListCalls;

        // No writes since the last tick: the stamp is unchanged and the
        // polling tick costs one stamp read, not a full list query.
        await loop.TickAsync();
        await loop.TickAsync();
        Assert.Equal(reads, countingResolver.ListCalls);

        // A peer write bumps the stamp and the next tick reads the list.
        await store.UpdatePeerAsync(
            new ZLinkPeerLocation(
                ZLinkLocationAutoConnectType.ClientServer, "play", RoutingId.From("r1"),
                ZLinkLocationRole.Router, "tcp://r:1", 100, 0, null, null, "peer-owner", 0, default),
            ZLinkLocationWriteIntent.NewClaim);
        await loop.TickAsync();
        Assert.Equal(reads + 1, countingResolver.ListCalls);
    }

    [Fact]
    public async Task Live_Owner_Set_Change_Reconciles_Even_When_The_Stamp_Is_Unchanged()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, time);
        await runtime.RenewOwnerLeaseOnceAsync();

        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var resolvers = new ZLinkStoreLocationResolvers(options, store, store, store, store, tracker, time);
        var executor = new RecordingExecutor();
        var local = new ZLinkAutoConnectLocal(
            ZLinkLocationAutoConnectType.ClientServer, "play", ZLinkLocationRole.Dealer,
            NodeRid: null, Endpoint: string.Empty);
        var reconciler = new ZLinkAutoConnectReconciler(
            local, localRow: null, runtime, resolvers, executor, options, time);
        var loop = new ZLinkAutoConnectLoop(
            reconciler, local, options, stampStore: store, timeProvider: time, leaseTracker: tracker);

        // A router row is written by an owner whose lease this node has not
        // seen yet: the tick lists the rows but the lease join drops them.
        // No further row write will ever bump the stamp.
        await store.UpdatePeerAsync(
            new ZLinkPeerLocation(
                ZLinkLocationAutoConnectType.ClientServer, "play", RoutingId.From("r1"),
                ZLinkLocationRole.Router, "tcp://r:1", 100, 0, null, null, "late-owner", 0, default),
            ZLinkLocationWriteIntent.NewClaim);
        await loop.TickAsync();
        Assert.Empty(executor.Connected);

        // The owner's lease appears (its runtime registered right after the
        // row was read). The stamp is unchanged, but the live owner set
        // changed, so the tick must reconcile and connect.
        await store.RenewOwnerLeaseAsync("late-owner", RoutingId.From("r1"), TimeSpan.FromSeconds(15));
        await loop.TickAsync();
        Assert.Equal("tcp://r:1", Assert.Single(executor.Connected).Endpoint);

        // The owner crashes: its lease expires without any row write. The
        // next tick must reconcile again and disconnect within one polling
        // interval (draft 14.4).
        time.Advance(TimeSpan.FromSeconds(16));
        await runtime.RenewOwnerLeaseOnceAsync();
        await loop.TickAsync();
        Assert.Equal("tcp://r:1", Assert.Single(executor.Disconnected).Endpoint);
    }

    private sealed class RecordingExecutor : IZLinkAutoConnectExecutor
    {
        public List<ZLinkAutoConnectTarget> Connected { get; } = [];

        public List<ZLinkAutoConnectTarget> Disconnected { get; } = [];

        public void Connect(ZLinkAutoConnectTarget target) => Connected.Add(target);

        public void Disconnect(ZLinkAutoConnectTarget target) => Disconnected.Add(target);
    }

    private sealed class CountingPeerResolver(IZLinkPeerLocationResolver inner) : IZLinkPeerLocationResolver
    {
        public int ListCalls { get; private set; }

        public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
            ZLinkPeerLocationFilter filter,
            ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
            CancellationToken cancellationToken = default)
        {
            ListCalls++;
            return inner.ListPeersAsync(filter, freshness, cancellationToken);
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
