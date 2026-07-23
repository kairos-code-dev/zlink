using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class LocationRuntimeTests
{
    [Fact]
    public async Task Owner_Lease_Renew_Tracks_Health_And_Recovers_After_Store_Failure()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var flaky = new FlakyOwnerLeaseStore(store);
        var runtime = NewRuntime(store, flaky, time);

        Assert.True(await runtime.RenewOwnerLeaseOnceAsync());
        Assert.True(runtime.GetHealthSnapshot().Healthy);
        Assert.Null(runtime.LastError);

        // Fail-static: a store outage is recorded, never thrown, and the
        // next successful renew clears the error.
        flaky.Fail = true;
        Assert.False(await runtime.RenewOwnerLeaseOnceAsync());
        Assert.False(runtime.GetHealthSnapshot().Healthy);
        Assert.NotNull(runtime.LastError);

        flaky.Fail = false;
        Assert.True(await runtime.RenewOwnerLeaseOnceAsync());
        Assert.True(runtime.GetHealthSnapshot().Healthy);
        Assert.Null(runtime.LastError);
    }

    [Fact]
    public async Task Owner_Lease_Renew_Timeout_Applies_To_Fixed_Routing_Ids()
    {
        var store = new ZLinkInMemoryLocationStore();
        var hanging = new HangingOwnerLeaseStore(store);
        var runtime = new ZLinkLocationRuntime(
            new ZLinkLocationOptions
            {
                OwnerLeaseRenewTimeout = TimeSpan.FromMilliseconds(25)
            },
            store,
            store,
            store,
            store,
            hanging);

        var renewed = await runtime.RenewOwnerLeaseOnceAsync().AsTask()
            .WaitAsync(TimeSpan.FromSeconds(1));

        Assert.False(renewed);
        Assert.False(runtime.GetHealthSnapshot().Healthy);
        Assert.Contains("timeout", runtime.LastError, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Claim_Then_Activate_Race_Gives_One_Winner_Across_Runtimes()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var runtimeA = NewRuntime(store, store, time);
        var runtimeB = NewRuntime(store, store, time);
        await runtimeA.RenewOwnerLeaseOnceAsync();
        await runtimeB.RenewOwnerLeaseOnceAsync();

        var actor = InMemoryLocationStoreTests.Actor("ignored");

        // The runtime stamps its own owner id: writers never choose owners.
        var winner = await runtimeA.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        var loser = await runtimeB.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);

        Assert.Equal(ZLinkLocationWriteStatus.Stored, winner.Status);
        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, loser.Status);

        var row = await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-1"));
        Assert.Equal(runtimeA.OwnerId, row!.OwnerId);
    }

    [Fact]
    public async Task Takeover_Makes_Old_Owner_Writes_Stale_And_Raises_OwnershipLost()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var oldOwner = NewRuntime(store, store, time);
        var newOwner = NewRuntime(store, store, time);
        await oldOwner.RenewOwnerLeaseOnceAsync();
        await newOwner.RenewOwnerLeaseOnceAsync();

        var lost = new List<(ZLinkLocationKind Kind, string Key)>();
        oldOwner.OwnershipLost += (kind, key) => lost.Add((kind, key));

        var actor = InMemoryLocationStoreTests.Actor("ignored");
        _ = await oldOwner.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        await newOwner.WriteActorAsync(actor, ZLinkLocationWriteIntent.Takeover);

        // The old owner notices the silent takeover on its next row write
        // and must deactivate its local instance.
        var stale = await oldOwner.WriteActorAsync(actor, ZLinkLocationWriteIntent.Renew);

        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, stale.Status);
        var single = Assert.Single(lost);
        Assert.Equal(ZLinkLocationKind.Actor, single.Kind);
    }

    [Fact]
    public async Task Shutdown_Removes_Owner_Lease_Then_Bulk_Removes_Rows()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var runtime = NewRuntime(store, store, time);
        await runtime.StartAsync(RoutingId.From("node-1"));
        await runtime.WriteActorAsync(
            InMemoryLocationStoreTests.Actor("ignored"), ZLinkLocationWriteIntent.NewClaim);
        await runtime.WriteSpotAsync(
            InMemoryLocationStoreTests.Spot("ignored", "spot-1"), ZLinkLocationWriteIntent.NewClaim);
        await runtime.WriteDescriptorAsync(
            InMemoryLocationStoreTests.MeshNode("ignored"), ZLinkLocationWriteIntent.NewClaim);

        await runtime.StopAsync();

        var snapshot = await store.ListOwnerLeasesAsync();
        Assert.Empty(snapshot.Leases);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-1")));
        Assert.Null(await store.ResolveSpotAsync(new ZLinkSpotLocationKey("play", RoutingId.From("spot-1"))));
        Assert.Empty(await store.ListMeshNodesAsync("play"));
    }

    [Fact]
    public async Task StopAsync_Propagates_Caller_Cancellation_And_Remains_Safe_To_Dispose()
    {
        var inner = new ZLinkInMemoryLocationStore();
        var ownerStore = new CancelingOwnerCleanupStore(inner);
        var runtime = new ZLinkLocationRuntime(
            new ZLinkLocationOptions
            {
                HeartbeatInterval = TimeSpan.FromHours(1),
                OwnerLeaseTtl = TimeSpan.FromHours(2)
            },
            inner,
            inner,
            inner,
            inner,
            ownerStore);
        await runtime.StartAsync(RoutingId.From("cancel-stop-node"));
        using var cancellation = new CancellationTokenSource();

        var stop = runtime.StopAsync(cancellation.Token).AsTask();
        await ownerStore.RemovalStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => stop);
        Assert.False(runtime.GetHealthSnapshot().Healthy);
        Assert.Equal(1, ownerStore.RenewCalls);

        await runtime.StopAsync(CancellationToken.None);
        await runtime.DisposeAsync();
        Assert.Equal(1, ownerStore.RenewCalls);
    }

    [Fact]
    public async Task Drain_Cleanup_Failure_Keeps_Lease_Heartbeat_Until_Retry_Succeeds()
    {
        var inner = new ZLinkInMemoryLocationStore();
        var store = new FailOnceOwnerCleanupStore(inner);
        var options = new ZLinkLocationOptions
        {
            HeartbeatInterval = TimeSpan.FromMilliseconds(10),
            OwnerLeaseTtl = TimeSpan.FromSeconds(15)
        };
        var runtime = new ZLinkLocationRuntime(
            options,
            store,
            inner,
            inner,
            inner,
            store);
        await runtime.StartAsync(RoutingId.From("node-1"));

        await Assert.ThrowsAsync<InvalidOperationException>(async () =>
            await runtime.CleanupOwnerForDrainAsync(CancellationToken.None));
        await store.HeartbeatAfterFailure.Task.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.Single((await inner.ListOwnerLeasesAsync()).Leases);

        await runtime.CleanupOwnerForDrainAsync(CancellationToken.None);
        Assert.Empty((await inner.ListOwnerLeasesAsync()).Leases);
        await runtime.StopAsync();
    }

    [Fact]
    public async Task Drain_Cleanup_Removes_Every_Owner_Row_And_The_Owner_Lease()
    {
        var store = new ZLinkInMemoryLocationStore();
        var runtime = new ZLinkLocationRuntime(
            new ZLinkLocationOptions(),
            store,
            store,
            store,
            store,
            store);
        await runtime.StartAsync(RoutingId.From("node-drain"));
        await runtime.WriteActorAsync(
            InMemoryLocationStoreTests.Actor("ignored"),
            ZLinkLocationWriteIntent.NewClaim);
        await runtime.WriteSpotAsync(
            InMemoryLocationStoreTests.Spot("ignored", "spot-drain"),
            ZLinkLocationWriteIntent.NewClaim);
        await runtime.WriteDescriptorAsync(
            InMemoryLocationStoreTests.MeshNode("ignored"),
            ZLinkLocationWriteIntent.NewClaim);

        await runtime.CleanupOwnerForDrainAsync(CancellationToken.None);

        Assert.Empty((await store.ListOwnerLeasesAsync()).Leases);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-1")));
        Assert.Null(await store.ResolveSpotAsync(
            new ZLinkSpotLocationKey("play", RoutingId.From("spot-drain"))));
        Assert.Empty(await store.ListMeshNodesAsync("play"));
        await runtime.StopAsync();
    }

    [Fact]
    public async Task Startup_RequiresOwnerLease_AndCanRetryAfterTheStoreRecovers()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var flaky = new FlakyOwnerLeaseStore(store) { Fail = true };
        var runtime = NewRuntime(store, flaky, time);

        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            runtime.StartAsync(RoutingId.From("node-1")).AsTask());
        Assert.Empty((await store.ListOwnerLeasesAsync()).Leases);

        flaky.Fail = false;
        await runtime.StartAsync(RoutingId.From("node-1"));
        Assert.Single((await store.ListOwnerLeasesAsync()).Leases);
        await runtime.StopAsync();
    }

    [Fact]
    public async Task Restart_UsesFreshOwnerGeneration_AndReportsStoppedLeaseUnhealthy()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var runtime = NewRuntime(store, store, time);

        await runtime.StartAsync(RoutingId.From("stable-node"));
        var firstOwner = runtime.OwnerId;
        Assert.True(runtime.GetHealthSnapshot().Healthy);

        await runtime.StopAsync();
        Assert.False(runtime.GetHealthSnapshot().Healthy);

        await runtime.StartAsync(RoutingId.From("stable-node"));
        Assert.NotEqual(firstOwner, runtime.OwnerId);
        Assert.True(runtime.GetHealthSnapshot().Healthy);
        await runtime.StopAsync();
    }

    [Fact]
    public async Task DisposeAsync_WaitsForInFlightHeartbeatBeforeCleaningOwnerResources()
    {
        var inner = new ZLinkInMemoryLocationStore();
        var store = new BlockingHeartbeatStore(inner);
        var runtime = new ZLinkLocationRuntime(
            new ZLinkLocationOptions
            {
                HeartbeatInterval = TimeSpan.FromMilliseconds(5),
                OwnerLeaseTtl = TimeSpan.FromSeconds(15)
            },
            inner,
            inner,
            inner,
            inner,
            store);
        await runtime.StartAsync(RoutingId.From("dispose-node"));
        await store.HeartbeatStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var firstDispose = runtime.DisposeAsync().AsTask();
        var secondDispose = runtime.DisposeAsync().AsTask();
        await Task.Delay(25);
        Assert.False(firstDispose.IsCompleted);
        Assert.False(secondDispose.IsCompleted);

        store.ReleaseHeartbeat.TrySetResult();
        await Task.WhenAll(firstDispose, secondDispose).WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(2, store.RenewCalls);
        Assert.Empty((await inner.ListOwnerLeasesAsync()).Leases);
    }

    [Fact]
    public async Task DisposeAsync_PreventsQueuedStartFromCreatingANewRuntimeGeneration()
    {
        var inner = new ZLinkInMemoryLocationStore();
        var store = new BlockingHeartbeatStore(inner);
        var runtime = new ZLinkLocationRuntime(
            new ZLinkLocationOptions
            {
                HeartbeatInterval = TimeSpan.FromMilliseconds(5),
                OwnerLeaseTtl = TimeSpan.FromSeconds(15)
            },
            inner,
            inner,
            inner,
            inner,
            store);
        await runtime.StartAsync(RoutingId.From("dispose-race-node"));
        await store.HeartbeatStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var stop = runtime.StopAsync().AsTask();
        await Task.Delay(25);
        Assert.False(stop.IsCompleted);
        var queuedStart = runtime.StartAsync(RoutingId.From("forbidden-generation")).AsTask();
        var dispose = runtime.DisposeAsync().AsTask();
        var repeatedDispose = runtime.DisposeAsync().AsTask();
        Assert.Same(dispose, repeatedDispose);

        store.ReleaseHeartbeat.TrySetResult();
        await stop.WaitAsync(TimeSpan.FromSeconds(5));
        await Assert.ThrowsAsync<ObjectDisposedException>(() => queuedStart);
        await dispose.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(2, store.RenewCalls);
        Assert.Empty((await inner.ListOwnerLeasesAsync()).Leases);
        await Assert.ThrowsAsync<ObjectDisposedException>(async () =>
            await runtime.StartAsync(RoutingId.From("after-dispose")));
    }

    [Fact]
    public async Task SynchronousServiceProviderDispose_CannotSilentlyPartiallyDisposeLocationRuntime()
    {
        var store = new ZLinkInMemoryLocationStore();
        var runtime = new ZLinkLocationRuntime(
            new ZLinkLocationOptions(),
            store,
            store,
            store,
            store,
            store);
        var provider = new ServiceCollection()
            .AddSingleton(_ => runtime)
            .BuildServiceProvider();
        _ = provider.GetRequiredService<ZLinkLocationRuntime>();

        var failure = Assert.Throws<InvalidOperationException>(provider.Dispose);
        Assert.Contains("IAsyncDisposable", failure.Message, StringComparison.Ordinal);

        await runtime.DisposeAsync();
    }

    [Fact]
    public async Task InMemory_Registration_Resolves_Resolvers_Query_And_Shared_Store()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options => options.UseTestLocationStore());
        await using var provider = services.BuildServiceProvider();

        Assert.NotNull(provider.GetRequiredService<IZLinkMeshNodeLocationResolver>());
        Assert.NotNull(provider.GetRequiredService<IZLinkSpotHandleResolver>());
        Assert.NotNull(provider.GetRequiredService<IZLinkActorSpotHandleResolver>());
        Assert.NotNull(provider.GetRequiredService<IZLinkLocationRuntimeQuery>());

        // The five store roles must share one physical store: in-memory
        // registration maps every interface onto one instance.
        var meshNodeStore = provider.GetRequiredService<IZLinkMeshNodeLocationStore>();
        var leaseStore = provider.GetRequiredService<IZLinkOwnerLeaseStore>();
        Assert.Same(meshNodeStore, leaseStore);
    }

    private static ZLinkLocationRuntime NewRuntime(
        ZLinkInMemoryLocationStore store,
        IZLinkOwnerLeaseStore ownerLeaseStore,
        ManualTimeProvider time) =>
        new(new ZLinkLocationOptions(), store, store, store, store, ownerLeaseStore, time);

    private sealed class FlakyOwnerLeaseStore(IZLinkOwnerLeaseStore inner) : IZLinkOwnerLeaseStore
    {
        public bool Fail { get; set; }

        public ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
            string ownerId,
            RoutingId nodeRid,
            TimeSpan leaseTtl,
            CancellationToken cancellationToken = default) =>
            Fail
                ? throw new InvalidOperationException("store unreachable")
                : inner.RenewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl, cancellationToken);

        public ValueTask<bool> RemoveOwnerLeaseAsync(
            string ownerId,
            CancellationToken cancellationToken = default) =>
            inner.RemoveOwnerLeaseAsync(ownerId, cancellationToken);

        public ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
            CancellationToken cancellationToken = default) =>
            inner.ListOwnerLeasesAsync(cancellationToken);
    }

    private sealed class HangingOwnerLeaseStore(IZLinkOwnerLeaseStore inner) : IZLinkOwnerLeaseStore
    {
        private readonly TaskCompletionSource<ZLinkOwnerLeaseRenewal> renewal =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
            string ownerId,
            RoutingId nodeRid,
            TimeSpan leaseTtl,
            CancellationToken cancellationToken = default) =>
            new(renewal.Task);

        public ValueTask<bool> RemoveOwnerLeaseAsync(
            string ownerId,
            CancellationToken cancellationToken = default) =>
            inner.RemoveOwnerLeaseAsync(ownerId, cancellationToken);

        public ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
            CancellationToken cancellationToken = default) =>
            inner.ListOwnerLeasesAsync(cancellationToken);
    }

    private sealed class BlockingHeartbeatStore(IZLinkOwnerLeaseStore inner) : IZLinkOwnerLeaseStore
    {
        private int _renewCalls;

        public TaskCompletionSource HeartbeatStarted { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseHeartbeat { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public int RenewCalls => Volatile.Read(ref _renewCalls);

        public async ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
            string ownerId,
            RoutingId nodeRid,
            TimeSpan leaseTtl,
            CancellationToken cancellationToken = default)
        {
            if (Interlocked.Increment(ref _renewCalls) == 2)
            {
                HeartbeatStarted.TrySetResult();
                await ReleaseHeartbeat.Task.ConfigureAwait(false);
            }

            return await inner.RenewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl, cancellationToken)
                .ConfigureAwait(false);
        }

        public ValueTask<bool> RemoveOwnerLeaseAsync(
            string ownerId,
            CancellationToken cancellationToken = default) =>
            inner.RemoveOwnerLeaseAsync(ownerId, cancellationToken);

        public ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
            CancellationToken cancellationToken = default) =>
            inner.ListOwnerLeasesAsync(cancellationToken);
    }

    private sealed class CancelingOwnerCleanupStore(IZLinkOwnerLeaseStore inner) : IZLinkOwnerLeaseStore
    {
        private int _renewCalls;

        public TaskCompletionSource RemovalStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int RenewCalls => Volatile.Read(ref _renewCalls);

        public ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
            string ownerId,
            RoutingId nodeRid,
            TimeSpan leaseTtl,
            CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref _renewCalls);
            return inner.RenewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl, cancellationToken);
        }

        public async ValueTask<bool> RemoveOwnerLeaseAsync(
            string ownerId,
            CancellationToken cancellationToken = default)
        {
            RemovalStarted.TrySetResult();
            await Task.Delay(Timeout.InfiniteTimeSpan, cancellationToken).ConfigureAwait(false);
            return false;
        }

        public ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
            CancellationToken cancellationToken = default) =>
            inner.ListOwnerLeasesAsync(cancellationToken);
    }

    private sealed class FailOnceOwnerCleanupStore(IZLinkLocationStore inner) : IZLinkLocationStore
    {
        private int _fail = 1;
        private int _renewals;

        public TaskCompletionSource HeartbeatAfterFailure { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask<long> RemoveAllByOwnerAsync(
            string ownerId,
            CancellationToken cancellationToken = default) =>
            Interlocked.Exchange(ref _fail, 0) != 0
                ? throw new InvalidOperationException("owner cleanup unavailable")
                : inner.RemoveAllByOwnerAsync(ownerId, cancellationToken);

        public ValueTask<ZLinkActorTransferWriteResult> PrepareActorTransferAsync(
            ZLinkActorTransferPrepareRequest request,
            CancellationToken cancellationToken = default) =>
            inner.PrepareActorTransferAsync(request, cancellationToken);

        public ValueTask<ZLinkActorTransferWriteResult> CommitActorTransferAsync(
            string meshName, string actorId, Guid transferId, string recoveryOwnerId,
            CancellationToken cancellationToken = default) =>
            inner.CommitActorTransferAsync(meshName, actorId, transferId, recoveryOwnerId, cancellationToken);

        public ValueTask<ZLinkActorTransferWriteResult> ActivateActorTransferAsync(
            string meshName, string actorId, Guid transferId, string recoveryOwnerId,
            CancellationToken cancellationToken = default) =>
            inner.ActivateActorTransferAsync(meshName, actorId, transferId, recoveryOwnerId, cancellationToken);

        public ValueTask<ZLinkActorTransferWriteResult> AbortActorTransferAsync(
            string meshName, string actorId, Guid transferId, string recoveryOwnerId,
            CancellationToken cancellationToken = default) =>
            inner.AbortActorTransferAsync(meshName, actorId, transferId, recoveryOwnerId, cancellationToken);

        public ValueTask<ZLinkActorTransferWriteResult> TakeOverActorTransferAsync(
            string meshName, string actorId, Guid transferId, string successorOwnerId,
            TimeSpan recoveryLeaseTtl, CancellationToken cancellationToken = default) =>
            inner.TakeOverActorTransferAsync(
                meshName, actorId, transferId, successorOwnerId, recoveryLeaseTtl, cancellationToken);

        public ValueTask<ZLinkActorTransferRecord?> ResolveActorTransferAsync(
            string meshName, string actorId, CancellationToken cancellationToken = default) =>
            inner.ResolveActorTransferAsync(meshName, actorId, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
            ZLinkMeshNodeDescriptor descriptor, ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            inner.UpdateMeshNodeAsync(descriptor, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
            ZLinkMeshNodeDescriptorKey key, ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemoveMeshNodeAsync(key, owner, cancellationToken);

        public ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
            string meshName, CancellationToken cancellationToken = default) =>
            inner.ListMeshNodesAsync(meshName, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
            ZLinkSpotLocation spot, ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            inner.UpdateSpotAsync(spot, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteStatus> RemoveSpotAsync(
            ZLinkSpotLocationKey key, ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemoveSpotAsync(key, owner, cancellationToken);

        public ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
            ZLinkSpotLocationKey key, CancellationToken cancellationToken = default) =>
            inner.ResolveSpotAsync(key, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
            ZLinkActorLocation actor, ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            inner.UpdateActorAsync(actor, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
            ZLinkActorLocationKey key, ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemoveActorAsync(key, owner, cancellationToken);

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key, CancellationToken cancellationToken = default) =>
            inner.ResolveActorAsync(key, cancellationToken);

        public ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
            string ownerId, RoutingId nodeRid, TimeSpan leaseTtl,
            CancellationToken cancellationToken = default)
        {
            if (Interlocked.Increment(ref _renewals) >= 2)
                HeartbeatAfterFailure.TrySetResult();
            return inner.RenewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl, cancellationToken);
        }

        public ValueTask<bool> RemoveOwnerLeaseAsync(
            string ownerId, CancellationToken cancellationToken = default) =>
            inner.RemoveOwnerLeaseAsync(ownerId, cancellationToken);

        public ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
            CancellationToken cancellationToken = default) =>
            inner.ListOwnerLeasesAsync(cancellationToken);

        public ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
            ZLinkAuthorityKey key,
            CancellationToken cancellationToken = default) =>
            inner.ReadAuthorityAsync(key, cancellationToken);

        public ValueTask<ZLinkAuthorityCompareExchangeResult>
            CompareExchangeAuthorityAsync(
                ZLinkAuthorityKey key,
                ZLinkAuthorityExpectation expectation,
                ZLinkAuthorityMutation mutation,
                CancellationToken cancellationToken = default) =>
            inner.CompareExchangeAuthorityAsync(
                key, expectation, mutation, cancellationToken);

        public ValueTask<ZLinkAuthorityScanResult> ListAuthoritiesAsync(
            string prefix,
            ZLinkAuthorityScanCursor? cursor,
            int limit,
            CancellationToken cancellationToken = default) =>
            inner.ListAuthoritiesAsync(prefix, cursor, limit, cancellationToken);

        public ValueTask<ZLinkObjectReserveResult> ReserveAsync(
            ZLinkObjectReservationRequest request,
            CancellationToken cancellationToken = default) =>
            inner.ReserveAsync(request, cancellationToken);

        public ValueTask<ZLinkObjectCommitResult> CommitAsync(
            ZLinkObjectReservation reservation,
            ReadOnlyMemory<byte> readyPayload,
            CancellationToken cancellationToken = default) =>
            inner.CommitAsync(reservation, readyPayload, cancellationToken);

        public ValueTask<ZLinkObjectAbortResult> AbortAsync(
            ZLinkObjectReservation reservation,
            CancellationToken cancellationToken = default) =>
            inner.AbortAsync(reservation, cancellationToken);

        public ValueTask<ZLinkAggregatePrepareResult> PrepareAggregateAsync(
            ZLinkAggregatePrepareRequest request,
            CancellationToken cancellationToken = default) =>
            inner.PrepareAggregateAsync(request, cancellationToken);

        public ValueTask<ZLinkAggregateCommitResult> CommitAggregateAsync(
            ZLinkAggregateFence fence,
            CancellationToken cancellationToken = default) =>
            inner.CommitAggregateAsync(fence, cancellationToken);

        public ValueTask<ZLinkAggregateAbortResult> AbortAggregateAsync(
            ZLinkAggregateFence fence,
            CancellationToken cancellationToken = default) =>
            inner.AbortAggregateAsync(fence, cancellationToken);
    }
}
