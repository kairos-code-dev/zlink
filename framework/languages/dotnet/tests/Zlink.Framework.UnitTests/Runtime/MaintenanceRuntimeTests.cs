using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Service;

namespace Zlink.Framework.UnitTests;

public sealed class MaintenanceRuntimeTests
{
    [Fact]
    public void Manual_topology_reason_and_registration_gate_are_frozen()
    {
        Assert.Equal(9, (int)ZLinkFrameworkTerminationReason.ManualTopologyUnsupported);
        var routeMesh = new ZLinkFrameworkRegistration();
        routeMesh.SpotNodes.Add(
            "play",
            new ZLinkSpotNodeRegistration
            {
                SpotNodeName = "play",
                Router = new ZLinkSpotRouterCapabilityRegistration
                {
                    AcquisitionMode = ZLinkPeerAcquisitionMode.Manual
                }
            });

        Assert.True(ZLinkFrameworkRuntime.HasUnsupportedManualTopology(routeMesh));

        var clientServer = new ZLinkFrameworkRegistration();
        clientServer.Channels.Add("orders", new ZLinkChannelRegistration
        {
            ChannelName = "orders",
            Client = new ZLinkChannelClientCapabilityRegistration
            {
                AcquisitionMode = ZLinkPeerAcquisitionMode.Manual
            }
        });
        Assert.True(ZLinkFrameworkRuntime.HasUnsupportedManualTopology(clientServer));

        var subscriber = new ZLinkFrameworkRegistration();
        subscriber.Channels.Add("events", new ZLinkChannelRegistration
        {
            ChannelName = "events",
            Subscriber = new ZLinkChannelSubscriberCapabilityRegistration
            {
                AcquisitionMode = ZLinkPeerAcquisitionMode.Manual
            }
        });
        Assert.True(ZLinkFrameworkRuntime.HasUnsupportedManualTopology(subscriber));

        var storelessPublisher = new ZLinkFrameworkRegistration();
        storelessPublisher.Channels.Add("events", new ZLinkChannelRegistration
        {
            ChannelName = "events",
            Publisher = new ZLinkChannelPublisherCapabilityRegistration()
        });
        Assert.True(ZLinkFrameworkRuntime.HasUnsupportedManualTopology(storelessPublisher));

        Assert.False(ZLinkFrameworkRuntime.HasUnsupportedManualTopology(
            new ZLinkFrameworkRegistration()));
    }

    [Fact]
    public void Automatic_peer_readiness_requires_exact_generation_and_admitted_state()
    {
        var rid = RoutingId.From("replacement");
        var local = RoutingId.From("local");
        ZLinkRouteMeshPeerIdentity[] descriptors = [new(rid, 7, Draining: false)];
        MeshNodePeer[] stale =
        [
            new(1, MeshPeerSource.Discovery, MeshPeerState.Admitted, rid, 6, 1,
                "tcp://127.0.0.1:1", 0, 0, 0)
        ];
        IReadOnlySet<RoutingId> localNodes = new HashSet<RoutingId> { local };
        Assert.False(ZLinkFrameworkRuntime.HasExactPeerReadiness(descriptors, stale, localNodes));

        MeshNodePeer[] exact =
        [
            stale[0] with { LifecycleGeneration = 7 }
        ];
        Assert.True(ZLinkFrameworkRuntime.HasExactPeerReadiness(descriptors, exact, localNodes));
        Assert.False(ZLinkFrameworkRuntime.HasExactPeerReadiness(
            descriptors,
            [exact[0] with { State = MeshPeerState.Connecting }],
            localNodes));
        Assert.False(ZLinkFrameworkRuntime.HasExactPeerReadiness(
            [descriptors[0] with { Draining = true }],
            Array.Empty<MeshNodePeer>(),
            localNodes));
        Assert.False(ZLinkFrameworkRuntime.HasExactPeerReadiness(
            Array.Empty<ZLinkRouteMeshPeerIdentity>(),
            Array.Empty<MeshNodePeer>(),
            localNodes));
        Assert.False(ZLinkFrameworkRuntime.HasExactPeerReadiness(
            [new ZLinkRouteMeshPeerIdentity(local, 7, Draining: false)],
            [exact[0] with { RoutingId = local }],
            localNodes));
        var secondLocal = RoutingId.From("local-2");
        Assert.False(ZLinkFrameworkRuntime.HasExactPeerReadiness(
            [
                new ZLinkRouteMeshPeerIdentity(local, 7, Draining: false),
                new ZLinkRouteMeshPeerIdentity(secondLocal, 7, Draining: false)
            ],
            [
                exact[0] with { RoutingId = local },
                exact[0] with { RoutingId = secondLocal }
            ],
            new HashSet<RoutingId> { local, secondLocal }));
    }

    [Fact]
    public void Retire_target_compatibility_requires_version_wave_type_and_headroom()
    {
        var source = new ZLinkFrameworkRegistration
        {
            ApplicationVersion = 5,
            MaintenanceWave = "blue"
        };
        ZLinkObjectCapability[] required =
        [
            new(
                ZLinkPlacementObjectKind.UserSpot,
                "room",
                ZLinkObjectMaintenancePolicyKind.Snapshot,
                HasSnapshotAdapter: true,
                Limit: 10),
            new(
                ZLinkPlacementObjectKind.Actor,
                "player",
                ZLinkObjectMaintenancePolicyKind.Recreate,
                HasSnapshotAdapter: false,
                Limit: 0)
        ];
        var inventory = new ZLinkSpotRetireInventory(
            "mesh",
            RoutingId.From("source"),
            1,
            new ZLinkLocationOwnerToken("source-owner", 1),
            "spot",
            "room",
            typeof(object),
            InstanceSpot: false,
            ObjectGeneration: 1,
            ActorIds: ["actor"],
            RequiredCapabilities: required);
        var target = new ZLinkMeshNodeDescriptor(
            "mesh",
            RoutingId.From("target"),
            2,
            1,
            "tcp://127.0.0.1:1",
            new Dictionary<string, int>(),
            "plain",
            "target-owner",
            2,
            DateTimeOffset.UtcNow)
        {
            ApplicationVersion = 6,
            MaintenanceWave = "green",
            State = ZLinkFrameworkRuntimeState.Serving,
            ObjectRole = ZLinkMeshNodeObjectRole.Server,
            PlacementWeight = 100,
            ObjectCapabilities = required,
            Capacity = new ZLinkPlacementCapacity(
                new ZLinkPopulationCapacity(1, 0, 3),
                new ZLinkPopulationCapacity(1, 0, 3),
                [new ZLinkSpotTypeCapacity(
                    ZLinkPlacementObjectKind.UserSpot,
                    "room",
                    1,
                    0,
                    3)]),
            ActivationConcurrency = new ZLinkActivationConcurrency(1, 3)
        };

        Assert.True(ZLinkSpotRetireTargetRuntime.IsCompatibleTarget(
            target,
            source,
            inventory,
            ZLinkPlacementObjectKind.UserSpot));
        Assert.False(ZLinkSpotRetireTargetRuntime.IsCompatibleTarget(
            target with { ApplicationVersion = 4 }, source, inventory,
            ZLinkPlacementObjectKind.UserSpot));
        Assert.False(ZLinkSpotRetireTargetRuntime.IsCompatibleTarget(
            target with { MaintenanceWave = "blue" }, source, inventory,
            ZLinkPlacementObjectKind.UserSpot));
        Assert.False(ZLinkSpotRetireTargetRuntime.IsCompatibleTarget(
            target with { ObjectCapabilities = required[..1] }, source, inventory,
            ZLinkPlacementObjectKind.UserSpot));
        Assert.False(ZLinkSpotRetireTargetRuntime.IsCompatibleTarget(
            target with
            {
                Capacity = target.Capacity with
                {
                    Actors = new ZLinkPopulationCapacity(2, 0, 2)
                }
            }, source, inventory, ZLinkPlacementObjectKind.UserSpot));
        Assert.True(ZLinkSpotRetireTargetRuntime.HasHeadroom(
            new ZLinkPopulationCapacity(100, 100, 0),
            1));

        var plan = new ZLinkRetirePreflightPlan();
        var oneActor = new ZLinkCapacityVector(1, 0, null);
        var boundedTarget = target with
        {
            Capacity = target.Capacity with
            {
                Actors = new ZLinkPopulationCapacity(1, 0, 3)
            },
            ActivationConcurrency = new ZLinkActivationConcurrency(0, 2)
        };
        Assert.True(plan.TryReserve(boundedTarget, oneActor));
        Assert.True(plan.TryReserve(boundedTarget, oneActor));
        Assert.False(plan.TryReserve(boundedTarget, oneActor));
    }

    [Fact]
    public async Task Shutdown_bypasses_retire_topology_preflight()
    {
        var preflightCalls = 0;
        using var fixture = Create(_ =>
        {
            Interlocked.Increment(ref preflightCalls);
            return ValueTask.FromResult<ZLinkFrameworkTerminationReason?>(
                ZLinkFrameworkTerminationReason.ManualTopologyUnsupported);
        });
        fixture.Runtime.MarkServing();
        fixture.Executor.Complete.TrySetResult(null);

        var result = await fixture.Runtime.ShutdownAsync();

        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, result.Outcome);
        Assert.Equal(0, preflightCalls);
    }

    [Fact]
    public async Task Retire_remains_retiring_until_relocation_detaches()
    {
        using var fixture = Create();
        fixture.Runtime.MarkServing();

        var retire = fixture.Runtime.RetireAsync().AsTask();
        await fixture.Executor.Started.Task.WaitAsync(TimeSpan.FromSeconds(1));

        Assert.Equal(ZLinkFrameworkRuntimeState.Retiring, fixture.Runtime.State);
        Assert.False(fixture.Runtime.Snapshot().WorkSealed);
        Assert.True(fixture.Drain.IsReady);

        fixture.Executor.Complete.TrySetResult(null);
        await retire;
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, fixture.Runtime.State);
        Assert.False(fixture.Drain.IsReady);
    }

    [Fact]
    public async Task Retire_publishes_descriptor_after_preflight_and_blocks_on_store_failure()
    {
        var sequence = new List<string>();
        using var fixture = Create(
            _ =>
            {
                sequence.Add("preflight");
                return ValueTask.FromResult<ZLinkFrameworkTerminationReason?>(null);
            },
            _ =>
            {
                sequence.Add("publish-retiring");
                return ValueTask.FromResult(false);
            });
        fixture.Runtime.MarkServing();

        var result = await fixture.Runtime.RetireAsync();

        Assert.Equal(["preflight", "publish-retiring"], sequence);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Blocked, result.Outcome);
        Assert.Equal(ZLinkFrameworkTerminationReason.StoreUnavailable, result.Reason);
        Assert.Equal(ZLinkFrameworkRuntimeState.Serving, fixture.Runtime.State);
        Assert.True(fixture.Drain.IsReady);
        Assert.Equal(0, fixture.Executor.ExecuteCount);
    }

    [Fact]
    public async Task RetiringRollbackFailureSealsAndForceStopsAfterTeardown()
    {
        using var fixture = Create(
            publishRetiring: _ => ValueTask.FromException<bool>(
                new ZLinkRetiringPublicationRollbackException(
                    [new InvalidOperationException("rollback not confirmed")])));
        fixture.Runtime.MarkServing();

        var result = await fixture.Runtime.RetireAsync();

        Assert.Equal(ZLinkFrameworkTerminationIntent.Retire, result.EffectiveIntent);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.ForceStopped, result.Outcome);
        Assert.Equal(ZLinkFrameworkTerminationReason.TeardownFailed, result.Reason);
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, fixture.Runtime.State);
        Assert.False(fixture.Drain.IsReady);
        Assert.Equal(0, fixture.Executor.ExecuteCount);
    }

    [Fact]
    public async Task Exact_peer_snapshot_deadline_is_blocked_as_target_unavailable()
    {
        using var fixture = Create(async cancellationToken =>
        {
            var ready = await ZLinkFrameworkRuntime.WaitForExactPeerReadinessAsync(
                static () => false,
                TimeSpan.FromMilliseconds(1),
                cancellationToken);
            return ready ? null : ZLinkFrameworkTerminationReason.TargetUnavailable;
        });
        fixture.Runtime.MarkServing();

        var result = await fixture.Runtime.RetireAsync(TimeSpan.FromMilliseconds(20));

        Assert.Equal(ZLinkFrameworkTerminationOutcome.Blocked, result.Outcome);
        Assert.Equal(ZLinkFrameworkTerminationReason.TargetUnavailable, result.Reason);
        Assert.Equal(ZLinkFrameworkRuntimeState.Serving, fixture.Runtime.State);
    }

    [Fact]
    public async Task Framework_Registration_Exposes_Host_Wide_Maintenance_Runtime()
    {
        var registrations = new ServiceCollection();
        registrations.AddZLinkFramework(_ => { });
        await using var services = registrations.BuildServiceProvider();

        var runtime = services.GetRequiredService<IZLinkFrameworkRuntime>();
        var result = await runtime.ShutdownAsync(TimeSpan.FromSeconds(1));

        Assert.Equal(ZLinkFrameworkTerminationIntent.Shutdown, result.EffectiveIntent);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, result.Outcome);
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, runtime.State);
    }

    [Fact]
    public async Task Retire_Preflight_Block_Does_Not_Seal_And_Can_Retry()
    {
        var attempts = 0;
        using var fixture = Create(
            _ => ValueTask.FromResult<ZLinkFrameworkTerminationReason?>(
                Interlocked.Increment(ref attempts) == 1
                    ? ZLinkFrameworkTerminationReason.TargetUnavailable
                    : null));
        fixture.Runtime.MarkServing();

        var blocked = await fixture.Runtime.RetireAsync();

        Assert.Equal(ZLinkFrameworkTerminationOutcome.Blocked, blocked.Outcome);
        Assert.Equal(ZLinkFrameworkTerminationReason.TargetUnavailable, blocked.Reason);
        Assert.Equal(ZLinkFrameworkRuntimeState.Serving, fixture.Runtime.State);
        Assert.False(fixture.Runtime.Snapshot().WorkSealed);
        Assert.Equal(0, fixture.Executor.ExecuteCount);

        fixture.Executor.Complete.TrySetResult(null);
        var completed = await fixture.Runtime.RetireAsync();

        Assert.Equal(ZLinkFrameworkTerminationIntent.Retire, completed.EffectiveIntent);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, completed.Outcome);
        Assert.Equal(1, fixture.Executor.ExecuteCount);
        Assert.Equal(
            ZLinkFrameworkTerminationIntent.Retire,
            fixture.Executor.Intent);
    }

    [Fact]
    public async Task Shutdown_During_Preflight_Wins_The_Maintenance_Barrier()
    {
        var preflight = new TaskCompletionSource<ZLinkFrameworkTerminationReason?>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        using var fixture = Create(
            cancellationToken => new ValueTask<ZLinkFrameworkTerminationReason?>(
                preflight.Task.WaitAsync(cancellationToken)));
        fixture.Runtime.MarkServing();

        var retire = fixture.Runtime.RetireAsync().AsTask();
        var shutdown = fixture.Runtime.ShutdownAsync().AsTask();
        preflight.TrySetResult(null);
        fixture.Executor.Complete.TrySetResult(null);

        var first = await retire;
        var second = await shutdown;
        Assert.Equal(ZLinkFrameworkTerminationIntent.Shutdown, first.EffectiveIntent);
        Assert.Equal(first, second);
        Assert.Equal(1, fixture.Executor.ExecuteCount);
        Assert.Equal(
            ZLinkFrameworkTerminationIntent.Shutdown,
            fixture.Executor.Intent);
    }

    [Fact]
    public async Task Shutdown_During_Retiring_Publication_Wins_Before_Publication_Commits()
    {
        var publicationStarted = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        using var fixture = Create(
            publishRetiring: async cancellationToken =>
            {
                publicationStarted.TrySetResult();
                await Task.Delay(Timeout.InfiniteTimeSpan, cancellationToken);
                return true;
            });
        fixture.Runtime.MarkServing();

        var retire = fixture.Runtime.RetireAsync().AsTask();
        await publicationStarted.Task.WaitAsync(TimeSpan.FromSeconds(1));
        var shutdown = fixture.Runtime.ShutdownAsync().AsTask();
        fixture.Executor.Complete.TrySetResult(null);

        var first = await retire;
        var second = await shutdown;
        Assert.Equal(ZLinkFrameworkTerminationIntent.Shutdown, first.EffectiveIntent);
        Assert.Equal(first, second);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, first.Outcome);
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, fixture.Runtime.State);
        Assert.Equal(1, fixture.Executor.ExecuteCount);
        Assert.Equal(
            ZLinkFrameworkTerminationIntent.Shutdown,
            fixture.Executor.Intent);
    }

    [Fact]
    public async Task Shutdown_After_Retiring_Publication_Joins_Retire()
    {
        using var fixture = Create();
        fixture.Runtime.MarkServing();

        var retire = fixture.Runtime.RetireAsync().AsTask();
        await fixture.Executor.Started.Task.WaitAsync(TimeSpan.FromSeconds(1));
        var shutdown = fixture.Runtime.ShutdownAsync().AsTask();
        fixture.Executor.Complete.TrySetResult(null);

        var first = await retire;
        var second = await shutdown;
        Assert.Equal(ZLinkFrameworkTerminationIntent.Retire, first.EffectiveIntent);
        Assert.Equal(first, second);
    }

    [Fact]
    public async Task Caller_Cancellation_Ends_Only_That_Waiter()
    {
        using var fixture = Create();
        fixture.Runtime.MarkServing();
        var shared = fixture.Runtime.ShutdownAsync().AsTask();
        using var waiterCancellation = new CancellationTokenSource();
        var canceledWaiter = fixture.Runtime.ShutdownAsync(
            cancellationToken: waiterCancellation.Token).AsTask();

        waiterCancellation.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => canceledWaiter);
        Assert.False(shared.IsCompleted);

        fixture.Executor.Complete.TrySetResult(null);
        var result = await shared;
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, result.Outcome);
    }

    [Fact]
    public async Task Shutdown_Force_Stop_Has_One_Terminal_Result()
    {
        using var fixture = Create();
        fixture.Executor.Complete.TrySetResult(ZLinkDrainForceReason.DeadlineExceeded);

        var result = await fixture.Runtime.ShutdownAsync();
        var repeated = await fixture.Runtime.RetireAsync();

        Assert.Equal(ZLinkFrameworkTerminationIntent.Shutdown, result.EffectiveIntent);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.ForceStopped, result.Outcome);
        Assert.Equal(ZLinkFrameworkTerminationReason.DeadlineExceeded, result.Reason);
        Assert.Equal(result, repeated);
        Assert.Equal(result, fixture.Runtime.Snapshot().TerminalResult);
    }

    [Fact]
    public async Task RelocationTerminalFailureCompletesCleanupInStoppedState()
    {
        using var fixture = Create();
        fixture.Executor.Complete.TrySetResult(
            ZLinkDrainForceReason.RelocationFailed);

        var result = await fixture.Runtime.ShutdownAsync();
        var repeated = await fixture.Runtime.ShutdownAsync();

        Assert.Equal(ZLinkFrameworkTerminationOutcome.ForceStopped, result.Outcome);
        Assert.Equal(ZLinkFrameworkTerminationReason.RelocationFailed, result.Reason);
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, fixture.Runtime.State);
        Assert.Equal(result, repeated);
        Assert.Equal(result, fixture.Runtime.Snapshot().TerminalResult);
    }

    [Fact]
    public async Task Observation_Is_Ordered_And_Terminal_State_Is_Visible()
    {
        using var fixture = Create();
        await using var observer = fixture.Runtime.ObserveAsync(capacity: 2).GetAsyncEnumerator();
        var next = observer.MoveNextAsync().AsTask();

        fixture.Runtime.MarkServing();

        Assert.True(await next);
        Assert.Equal("zlink.runtime.host.termination_changed", observer.Current.Identifier);
        Assert.Equal(ZLinkFrameworkRuntimeState.Serving, observer.Current.State);
        Assert.Equal<ulong>(1, observer.Current.Sequence);

        fixture.Executor.Complete.TrySetResult(null);
        await fixture.Runtime.ShutdownAsync();
        var snapshot = fixture.Runtime.Snapshot();
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, snapshot.State);
        Assert.True(snapshot.WorkSealed);
        Assert.True(snapshot.Sequence >= 4);
    }

    [Fact]
    public async Task Capacity_One_Observer_Retains_Terminal_Event()
    {
        using var fixture = Create();
        await using var observer = fixture.Runtime.ObserveAsync(capacity: 1).GetAsyncEnumerator();
        var first = observer.MoveNextAsync().AsTask();
        fixture.Runtime.MarkServing();
        Assert.True(await first);

        fixture.Executor.Complete.TrySetResult(null);
        await fixture.Runtime.ShutdownAsync();

        Assert.True(await observer.MoveNextAsync());
        Assert.Equal(
            ZLinkFrameworkTerminationOutcome.Stopped,
            observer.Current.Outcome);
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, observer.Current.State);
    }

    [Fact]
    public void Snapshot_Reports_Framework_Owned_Pending_Counts()
    {
        using var fixture = Create(
            pending: static () => new ZLinkDrainRemainderCounts(
                Actors: 2,
                Spots: 3,
                Requests: 5,
                Sessions: 7));

        var snapshot = fixture.Runtime.Snapshot();

        Assert.Equal<ulong>(5, snapshot.PendingRequestCount);
        Assert.Equal<ulong>(5, snapshot.PendingRelocationCount);
        Assert.Equal<ulong>(7, snapshot.PendingStreamBarrierCount);
    }

    private static Fixture Create(
        Func<CancellationToken, ValueTask<ZLinkFrameworkTerminationReason?>>? preflight = null,
        Func<CancellationToken, ValueTask<bool>>? publishRetiring = null,
        Func<ZLinkDrainRemainderCounts>? pending = null)
    {
        var executor = new MaintenanceExecutor();
        var drain = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events: null);
        var runtime = new ZLinkFrameworkMaintenanceRuntime(
            drain,
            preflight ?? (static _ =>
                ValueTask.FromResult<ZLinkFrameworkTerminationReason?>(null)),
            publishRetiring ?? (static _ => ValueTask.FromResult(true)),
            pending ?? (static () => new ZLinkDrainRemainderCounts(0, 0, 0, 0)));
        return new Fixture(runtime, drain, executor);
    }

    private sealed record Fixture(
        ZLinkFrameworkMaintenanceRuntime Runtime,
        ZLinkDrainCoordinator Drain,
        MaintenanceExecutor Executor) : IDisposable
    {
        public void Dispose()
        {
            Runtime.Dispose();
            Drain.Dispose();
        }
    }

    private sealed class MaintenanceExecutor : IZLinkDrainExecutor
    {
        public TaskCompletionSource<ZLinkDrainForceReason?> Complete { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int ExecuteCount { get; private set; }

        public ZLinkFrameworkTerminationIntent? Intent { get; private set; }

        public async ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
            TimeSpan deadline,
            CancellationToken deadlineToken)
        {
            return await ExecuteAsync(
                    ZLinkFrameworkTerminationIntent.Shutdown,
                    deadline,
                    deadlineToken)
                .ConfigureAwait(false);
        }

        public async ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
            ZLinkFrameworkTerminationIntent intent,
            TimeSpan deadline,
            CancellationToken deadlineToken)
        {
            _ = deadline;
            Intent = intent;
            ExecuteCount++;
            Started.TrySetResult();
            return await Complete.Task.WaitAsync(deadlineToken).ConfigureAwait(false);
        }

        public ValueTask ForceStopAsync(
            ZLinkDrainForceReason reason,
            CancellationToken cancellationToken)
        {
            _ = reason;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }
    }
}
