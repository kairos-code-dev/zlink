using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class EntrySpotActorDispatchTests
{
    [Fact]
    public async Task CancelledCreateWaiter_DoesNotDetachTheSharedCreationTransaction()
    {
        var probe = new ControlledCreationProbe();
        var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddScoped<ControlledCreationProbeActorFactory>()
            .BuildServiceProvider();
        var node = new CapturingSpotNode();
        node.SetRoutingId(RoutingId.From("actor-node"));
        var registration = new ZLinkFrameworkRegistration();
        registration.SpotNodes["actor-node"] = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "actor-node",
            ActorFactories = { ["controlled"] = typeof(ControlledCreationProbeActorFactory) }
        };
        registration.ActorCatalog.Build(registration.SpotNodes.Values);
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));
        var sessions = new ZLinkActorSessionManager(runtime, services, () => node, null);
        using var cancelledWaiter = new CancellationTokenSource();

        var first = sessions.CreateAndBindActorAsync(
                "actor-create-shared",
                "controlled",
                cancelledWaiter.Token)
            .AsTask();
        await probe.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));
        cancelledWaiter.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => first);

        var second = sessions.CreateAndBindActorAsync(
                "actor-create-shared",
                "controlled",
                CancellationToken.None)
            .AsTask();
        Assert.False(second.IsCompleted);
        probe.Release.TrySetResult();

        var result = await second.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal("actor-create-shared", result.Actor.ActorId);
        Assert.Single(node.CreatedActors);
        Assert.True(sessions.TryGetCreatedActorState("actor-create-shared", out var state));
        Assert.Same(result.Actor, state.Actor);
    }

    [Fact]
    public async Task ActorCreation_PublishFailure_CompensatesClaimNativeActorAndRuntimeState()
    {
        var services = new ServiceCollection()
            .AddScoped<CreationProbeActorFactory>()
            .BuildServiceProvider();
        var node = new CapturingSpotNode();
        node.SetRoutingId(RoutingId.From("actor-node"));
        var registration = new ZLinkFrameworkRegistration
        {
            DefaultRequestTimeout = TimeSpan.FromSeconds(1)
        };
        registration.SpotNodes["actor-node"] = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "actor-node",
            ActorFactories = { ["probe"] = typeof(CreationProbeActorFactory) }
        };
        registration.ActorCatalog.Build(registration.SpotNodes.Values);
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));
        var lifecycle = new FailingPublishActorLifecycle();
        var teardownEvents = new List<string>();
        node.BeforeDestroy = _ => teardownEvents.Add("native-destroy");
        lifecycle.BeforeRelease = () => teardownEvents.Add("ownership-release");
        var state = new ZLinkActorRuntimeState("actor-create-fail");
        ZLinkActorContext EnsureContext() => state.GetOrCreateContext(() => new ZLinkActorContext(runtime, state));
        var coordinator = new ZLinkActorCreationCoordinator(
            runtime,
            services,
            () => node,
            lifecycle,
            _ => EnsureContext(),
            (actor, actorState) =>
            {
                actorState.BindActorInstance(actor);
                var context = EnsureContext();
                Assert.Same(context, actor.Context);
                if (actorState.TryBeginActorConfiguration()) actor.Configure();
                return context;
            },
            async (actorState, nativeActor, cancellationToken) =>
            {
                await node.DestroyActorAsync(
                    nativeActor,
                    registration.DefaultRequestTimeout,
                    cancellationToken);
                await lifecycle.ReleaseActorAsync(actorState.ActorId, cancellationToken);
                await actorState.ExecuteLockedAsync(
                    () => actorState.ClearAfterDestroy(),
                    CancellationToken.None);
            });

        var failure = await Assert.ThrowsAsync<InvalidOperationException>(async () =>
            await coordinator.CreateAndBindActorAsync(
                state,
                state.ActorId,
                "probe",
                ZLinkMessage.Empty,
                failIfExists: true,
                ZLinkActorClaimMode.NewOwner,
                CancellationToken.None));

        Assert.Equal("publish failed", failure.Message);
        Assert.Equal(1, lifecycle.ReleaseCalls);
        Assert.Single(node.DestroyedActors);
        Assert.Equal(["native-destroy", "ownership-release"], teardownEvents);
        Assert.Null(state.Actor);
        Assert.Null(state.ActorType);
        Assert.Null(state.NativeActorRef);
    }

    [Fact]
    public async Task ActorDestroy_NativeNotFound_IsTerminalAndThenReleasesOwnership()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var locationRuntime = new ZLinkLocationRuntime(
            options, store, store, store, store, store, store, time);
        Assert.True(await locationRuntime.RenewOwnerLeaseOnceAsync());
        var resolvers = new ZLinkStoreLocationResolvers(
            store,
            store,
            store,
            store,
            new ZLinkOwnerLeaseTracker(store, options, time),
            new ZLinkObservedLocationGenerations());
        using var lifecycle = new ZLinkLocationLifecycle(locationRuntime, resolvers);
        await lifecycle.ActorOwnership.ClaimActorAsync(
            "probe",
            "actor-destroy-not-found",
            RoutingId.From("actor-node"),
            null,
            CancellationToken.None);

        var services = new ServiceCollection().BuildServiceProvider();
        var node = new CapturingSpotNode
        {
            DestroyFailure = new ZlinkRequestException(ZlinkRequestException.ErrorCode.NotFound)
        };
        node.SetRoutingId(RoutingId.From("actor-node"));
        var registration = new ZLinkFrameworkRegistration
        {
            DefaultRequestTimeout = TimeSpan.FromSeconds(1)
        };
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));
        var sessions = new ZLinkActorSessionManager(runtime, services, () => node, lifecycle);
        var state = sessions.GetOrCreateState("actor-destroy-not-found");
        var context = state.GetOrCreateContext(() => new ZLinkActorContext(runtime, state));
        var actor = new CreationProbeActor(state.ActorId, context);
        state.BindActorInstance(actor);
        state.BindNativeActorRef(new ZLinkBackendActorRef(node.RoutingId, state.ActorId, 1));

        await sessions.DestroyActorAsync(node.RoutingId, actor);

        Assert.False(sessions.TryGetCreatedActorState(state.ActorId, out _));
        Assert.Single(node.DestroyedActors);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey(state.ActorId)));
    }

    [Fact]
    public async Task ActorDestroy_NativeFailure_RetainsRefAndOwnershipUntilRetry()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var locationRuntime = new ZLinkLocationRuntime(
            options, store, store, store, store, store, store, time);
        Assert.True(await locationRuntime.RenewOwnerLeaseOnceAsync());
        var resolvers = new ZLinkStoreLocationResolvers(
            store,
            store,
            store,
            store,
            new ZLinkOwnerLeaseTracker(store, options, time),
            new ZLinkObservedLocationGenerations());
        using var lifecycle = new ZLinkLocationLifecycle(locationRuntime, resolvers);
        await lifecycle.ActorOwnership.ClaimActorAsync(
            "probe",
            "actor-destroy-native-retry",
            RoutingId.From("actor-node"),
            null,
            CancellationToken.None);

        var services = new ServiceCollection().BuildServiceProvider();
        var node = new CapturingSpotNode
        {
            DestroyFailure = new InvalidOperationException("native destroy failed")
        };
        node.SetRoutingId(RoutingId.From("actor-node"));
        var registration = new ZLinkFrameworkRegistration
        {
            DefaultRequestTimeout = TimeSpan.FromSeconds(1)
        };
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));
        var sessions = new ZLinkActorSessionManager(runtime, services, () => node, lifecycle);
        var state = sessions.GetOrCreateState("actor-destroy-native-retry");
        var context = state.GetOrCreateContext(() => new ZLinkActorContext(runtime, state));
        var actor = new CreationProbeActor(state.ActorId, context);
        var nativeActor = new ZLinkBackendActorRef(node.RoutingId, state.ActorId, 1);
        state.BindActorInstance(actor);
        state.BindNativeActorRef(nativeActor);

        var failure = await Assert.ThrowsAsync<InvalidOperationException>(async () =>
            await sessions.DestroyActorAsync(node.RoutingId, actor));

        Assert.Equal("native destroy failed", failure.Message);
        Assert.Equal(nativeActor, state.NativeActorRef);
        Assert.NotNull(await store.ResolveActorAsync(new ZLinkActorLocationKey(state.ActorId)));
        Assert.Null(await sessions.FindActorAsync(state.ActorId));
        await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await state.GetOrStartActorCreationAsync(
                "probe",
                false,
                () => Task.FromResult<IZLinkActor>(actor),
                CancellationToken.None));
        Assert.Throws<ZLinkFrameworkException>(() => state.BindActorInstance(actor));
        Assert.Throws<ZLinkFrameworkException>(() => state.BindSession(
            node.RoutingId,
            RoutingId.From("session"),
            "binding"));
        await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await state.ExecuteDispatchAsync(
                CreateHeader("blocked"),
                _ => ValueTask.CompletedTask,
                CancellationToken.None));

        node.DestroyFailure = null;
        await sessions.DestroyActorAsync(node.RoutingId, actor);

        Assert.Equal(2, node.DestroyedActors.Count);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey(state.ActorId)));
    }

    [Fact]
    public async Task OwnershipLoss_NativeFailure_KeepsQuarantinedStateUntilReconciliationCompletes()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var locationRuntime = new ZLinkLocationRuntime(
            options, store, store, store, store, store, store, time);
        Assert.True(await locationRuntime.RenewOwnerLeaseOnceAsync());
        var resolvers = new ZLinkStoreLocationResolvers(
            store,
            store,
            store,
            store,
            new ZLinkOwnerLeaseTracker(store, options, time),
            new ZLinkObservedLocationGenerations());
        using var lifecycle = new ZLinkLocationLifecycle(locationRuntime, resolvers);
        await lifecycle.ActorOwnership.ClaimActorAsync(
            "probe",
            "actor-ownership-loss-retry",
            RoutingId.From("actor-node"),
            null,
            CancellationToken.None);

        var retryStarted = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var allowRetry = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var destroyAttempts = 0;
        var node = new CapturingSpotNode
        {
            DestroyHandler = async (_, cancellationToken) =>
            {
                if (Interlocked.Increment(ref destroyAttempts) == 1)
                    throw new InvalidOperationException("native destroy failed");

                retryStarted.TrySetResult();
                await allowRetry.Task.WaitAsync(cancellationToken);
            }
        };
        node.SetRoutingId(RoutingId.From("actor-node"));
        var services = new ServiceCollection().BuildServiceProvider();
        var registration = new ZLinkFrameworkRegistration
        {
            DefaultRequestTimeout = TimeSpan.FromSeconds(1)
        };
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));
        var sessions = new ZLinkActorSessionManager(runtime, services, () => node, lifecycle);
        var state = sessions.GetOrCreateState("actor-ownership-loss-retry");
        var context = state.GetOrCreateContext(() => new ZLinkActorContext(runtime, state));
        var actor = new CreationProbeActor(state.ActorId, context);
        var nativeActor = new ZLinkBackendActorRef(node.RoutingId, state.ActorId, 1);
        state.BindActorInstance(actor);
        state.BindNativeActorRef(nativeActor);

        await sessions.DeactivateActorOnOwnershipLossAsync(state.ActorId);
        var retry = sessions.DeactivateActorOnOwnershipLossAsync(state.ActorId).AsTask();
        await retryStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(nativeActor, state.NativeActorRef);
        Assert.True(state.IsTeardownPending);
        Assert.Null(await sessions.FindActorAsync(state.ActorId));
        Assert.NotNull(await store.ResolveActorAsync(new ZLinkActorLocationKey(state.ActorId)));

        allowRetry.TrySetResult();
        await retry.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.False(state.IsTeardownPending);
        Assert.Null(state.NativeActorRef);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey(state.ActorId)));
        Assert.Equal(2, node.DestroyedActors.Count);
    }

    [Fact]
    public async Task ActorDestroy_ReleaseFailure_RetainsStateForTheNextOwnedRetry()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var actorStore = new FailOnceRemoveActorStore(store);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var locationRuntime = new ZLinkLocationRuntime(
            options, store, store, store, actorStore, store, store, time);
        Assert.True(await locationRuntime.RenewOwnerLeaseOnceAsync());
        var resolvers = new ZLinkStoreLocationResolvers(
            store,
            store,
            store,
            store,
            new ZLinkOwnerLeaseTracker(store, options, time),
            new ZLinkObservedLocationGenerations());
        using var lifecycle = new ZLinkLocationLifecycle(locationRuntime, resolvers);
        await lifecycle.ActorOwnership.ClaimActorAsync(
            "probe",
            "actor-destroy-retry",
            RoutingId.From("actor-node"),
            null,
            CancellationToken.None);

        var services = new ServiceCollection().BuildServiceProvider();
        var node = new CapturingSpotNode();
        node.SetRoutingId(RoutingId.From("actor-node"));
        var registration = new ZLinkFrameworkRegistration
        {
            DefaultRequestTimeout = TimeSpan.FromSeconds(1)
        };
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));
        var sessions = new ZLinkActorSessionManager(runtime, services, () => node, lifecycle);
        var state = sessions.GetOrCreateState("actor-destroy-retry");
        var context = state.GetOrCreateContext(() => new ZLinkActorContext(runtime, state));
        var actor = new CreationProbeActor(state.ActorId, context);
        state.BindActorInstance(actor);
        state.BindNativeActorRef(new ZLinkBackendActorRef(node.RoutingId, state.ActorId, 1));

        await Assert.ThrowsAsync<InvalidOperationException>(async () =>
            await sessions.DestroyActorAsync(node.RoutingId, actor));

        Assert.False(sessions.TryGetCreatedActorState(state.ActorId, out _));
        Assert.NotNull(state.NativeActorRef);
        Assert.True(state.IsTeardownPending);
        Assert.Single(node.DestroyedActors);

        await sessions.DestroyActorAsync(node.RoutingId, actor);

        Assert.False(sessions.TryGetCreatedActorState(state.ActorId, out _));
        Assert.Single(node.DestroyedActors);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey(state.ActorId)));
    }

    [Fact]
    public async Task ConcurrentActorDestroy_CallersAwaitOneTeardownTransaction()
    {
        var services = new ServiceCollection().BuildServiceProvider();
        var node = new CapturingSpotNode();
        node.SetRoutingId(RoutingId.From("actor-node"));
        var destroyStarted = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseDestroy = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        node.DestroyHandler = async (_, cancellationToken) =>
        {
            destroyStarted.TrySetResult();
            await releaseDestroy.Task.WaitAsync(cancellationToken);
        };
        var registration = new ZLinkFrameworkRegistration
        {
            DefaultRequestTimeout = TimeSpan.FromSeconds(1)
        };
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));
        var sessions = new ZLinkActorSessionManager(runtime, services, () => node, null);
        var state = sessions.GetOrCreateState("actor-destroy-concurrent");
        var context = state.GetOrCreateContext(() => new ZLinkActorContext(runtime, state));
        var actor = new CreationProbeActor(state.ActorId, context);
        state.BindActorInstance(actor);
        state.BindNativeActorRef(new ZLinkBackendActorRef(node.RoutingId, state.ActorId, 1));

        var first = sessions.DestroyActorAsync(node.RoutingId, actor).AsTask();
        await destroyStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var second = sessions.DestroyActorAsync(node.RoutingId, actor).AsTask();
        releaseDestroy.TrySetResult();

        await Task.WhenAll(first, second).WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Single(node.DestroyedActors);
        Assert.False(state.IsTeardownPending);
    }

    [Fact]
    public async Task ActorDestroy_DuringBoundRequest_IsDeferredUntilReplyFinalization()
    {
        var node = new CapturingSpotNode();
        var (runtime, _) = await CreateStartedRuntimeAsync(node);
        try
        {
            var actorRef = new ZLinkBackendActorRef(
                RoutingId.From("entry-node"),
                "actor-destroy-after-reply",
                1);
            var actor = RegisterProbeActor(runtime, actorRef);
            node.BeforeNoBindReply = _ => node.LifecycleEvents.Enqueue("reply");
            node.BeforeDestroy = _ => node.LifecycleEvents.Enqueue("destroy");
            var parts = CreateActorRequestParts(
                actorRef,
                "destroy-request",
                "destroy",
                requestId: 81,
                flags: 1);

            await DispatchEntryActorPartsAsync(runtime, parts, CancellationToken.None);

            Assert.Single(node.DestroyedActors);
            Assert.Null(await runtime.FindActorAsync(actor.ActorId));
            Assert.Equal(new[] { "reply", "destroy" }, node.LifecycleEvents.ToArray());
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task ActorDestroy_DuringBoundRequest_RejectsPreCancelledCall()
    {
        var node = new CapturingSpotNode();
        var (runtime, _) = await CreateStartedRuntimeAsync(node);
        try
        {
            var actorRef = new ZLinkBackendActorRef(
                RoutingId.From("entry-node"),
                "actor-destroy-cancelled",
                1);
            var actor = RegisterProbeActor(runtime, actorRef);
            var activation = runtime.GetSpotNodeRuntime("entry").EntrySpotActivation
                             ?? throw new InvalidOperationException("Entry Spot activation was not created.");
            await using var dispatch = ZLinkBoundSessionDispatchScope.Enter(actor.ActorId);
            using var cancelled = new CancellationTokenSource();
            cancelled.Cancel();

            await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
                await activation.DestroyActorAsync(actor, cancelled.Token));
            await dispatch.DrainAsync(CancellationToken.None);

            Assert.Empty(node.DestroyedActors);
            Assert.Same(actor, await runtime.FindActorAsync(actor.ActorId));
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task StragglerForwarder_StopsFinalPartRetryWhenTheForwardingWindowCloses()
    {
        var node = new CapturingSpotNode();
        node.ForwardResults.Enqueue(true);
        for (var attempt = 0; attempt < 100; attempt++) node.ForwardResults.Enqueue(false);
        var (runtime, _) = await CreateStartedRuntimeAsync(node);
        try
        {
            var forwarder = new ZLinkActorStragglerForwarder(runtime, capacity: 1);
            using var body = Message.From(Encoding.UTF8.GetBytes("body"));
            var source = new ZLinkBackendActorRef(RoutingId.From("source-node"), "actor-forward", 1);
            var target = new ZLinkBackendActorRef(RoutingId.From("target-node"), "actor-forward", 2);
            var forwardingLease = new ZLinkActorForwardingLease(TimeProvider.System);
            forwardingLease.Commit(TimeSpan.FromSeconds(5));

            var disposedBody = Message.From(Encoding.UTF8.GetBytes("disposed"));
            disposedBody.Dispose();
            Assert.Throws<ObjectDisposedException>(() => forwarder.Enqueue(
                source,
                target,
                RoutingId.From("session-node"),
                RoutingId.From("session-1"),
                6,
                0,
                CreateHeader("forward-disposed"),
                disposedBody,
                forwardingLease));

            forwarder.Enqueue(
                source,
                target,
                RoutingId.From("session-node"),
                RoutingId.From("session-1"),
                7,
                0,
                CreateHeader("forward"),
                body,
                forwardingLease);

            await node.FinalPartAttempted.Task.WaitAsync(TimeSpan.FromSeconds(5));
            forwardingLease.Cancel();
            await Task.Delay(25);
            var attemptsAfterCutoff = node.ForwardedParts.Count;
            await Task.Delay(25);
            Assert.True(node.ForwardedParts.Count >= 2);
            Assert.True(node.ForwardedParts[0].HasMore);
            Assert.All(node.ForwardedParts.Skip(1), static part => Assert.False(part.HasMore));
            Assert.Equal(attemptsAfterCutoff, node.ForwardedParts.Count);

            using var nextBody = Message.From(Encoding.UTF8.GetBytes("next"));
            var nextLease = new ZLinkActorForwardingLease(TimeProvider.System);
            nextLease.Commit(TimeSpan.FromSeconds(5));
            await Task.Run(() => forwarder.Enqueue(
                    source,
                    target,
                    RoutingId.From("session-node"),
                    RoutingId.From("session-1"),
                    8,
                    0,
                    CreateHeader("forward-next"),
                    nextBody,
                    nextLease))
                .WaitAsync(TimeSpan.FromSeconds(5));
            nextLease.Cancel();
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task RuntimeStart_WaitsUntilThePreviousStopFinishesDisposal()
    {
        var node = new CapturingSpotNode();
        var disposeStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseDispose = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        node.DisposeHandler = async () =>
        {
            disposeStarted.TrySetResult();
            await releaseDispose.Task;
        };
        var (runtime, _) = await CreateStartedRuntimeAsync(node);

        using (runtime.EnterOperation())
            await Assert.ThrowsAsync<InvalidOperationException>(() =>
                runtime.StopAsync(CancellationToken.None).AsTask());
        Assert.True(runtime.IsStarted);

        using var inFlightOperation = runtime.EnterOperation();
        Task stop;
        using (ExecutionContext.SuppressFlow())
            stop = Task.Run(async () => await runtime.StopAsync(CancellationToken.None));
        await Task.Delay(25);
        Assert.False(disposeStarted.Task.IsCompleted);
        inFlightOperation.Dispose();
        await disposeStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.False(runtime.IsStarted);
        Assert.Null(runtime.Context);
        Assert.Throws<InvalidOperationException>(() =>
            runtime.GetSpotMonitoringSnapshot("entry"));
        var restart = runtime.StartAsync(CancellationToken.None).AsTask();
        await Task.Delay(25);
        Assert.False(restart.IsCompleted);

        releaseDispose.TrySetResult();
        await stop.WaitAsync(TimeSpan.FromSeconds(5));
        await restart.WaitAsync(TimeSpan.FromSeconds(5));
        node.DisposeHandler = null;
        await runtime.StopAsync(CancellationToken.None);
    }

    [Fact]
    public async Task RuntimeStop_AllowsAnAdmittedOperationToScheduleGenerationOwnedCleanup()
    {
        var node = new CapturingSpotNode();
        var (runtime, _) = await CreateStartedRuntimeAsync(node);
        using var operation = runtime.EnterOperation();
        Task stop;
        using (ExecutionContext.SuppressFlow())
            stop = Task.Run(async () => await runtime.StopAsync(CancellationToken.None));

        Assert.True(SpinWait.SpinUntil(() => !runtime.IsStarted, TimeSpan.FromSeconds(5)));
        var cleanupRan = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Assert.True(runtime.TryRunDetached(
            "generation-cleanup",
            _ =>
            {
                cleanupRan.TrySetResult();
                return ValueTask.CompletedTask;
            }));

        await cleanupRan.Task.WaitAsync(TimeSpan.FromSeconds(5));
        operation.Dispose();
        await stop.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task Drain_Remainder_Request_Count_Excludes_Non_Request_Operations()
    {
        var (runtime, _) = await CreateStartedRuntimeAsync(new CapturingSpotNode());
        try
        {
            using (runtime.EnterOperation())
            using (runtime.EnterOperation(countAsRequest: true))
            {
                var remainder = runtime.GetDrainRemainderCounts();

                Assert.Equal(1, remainder.Requests);
            }
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task RuntimeStop_RejectsOwnedStopBeforeWaitingForAnExternalStopGate()
    {
        var node = new CapturingSpotNode();
        var (runtime, _) = await CreateStartedRuntimeAsync(node);
        using var operation = runtime.EnterOperation();
        Task externalStop;
        using (ExecutionContext.SuppressFlow())
            externalStop = Task.Run(async () => await runtime.StopAsync(CancellationToken.None));

        Assert.True(SpinWait.SpinUntil(() => !runtime.IsStarted, TimeSpan.FromSeconds(5)));
        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            runtime.StopAsync(CancellationToken.None).AsTask());

        operation.Dispose();
        await externalStop.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task RuntimeRestart_DoesNotReuseActorStateFromTheStoppedGeneration()
    {
        var node = new CapturingSpotNode();
        var (runtime, _) = await CreateStartedRuntimeAsync(node);
        var previous = runtime.GetOrCreateActorState("generation-actor");

        await runtime.StopAsync(CancellationToken.None);
        Assert.True(previous.ContextInvalidated);
        await runtime.StartAsync(CancellationToken.None);

        var current = runtime.GetOrCreateActorState("generation-actor");
        Assert.NotSame(previous, current);
        await runtime.StopAsync(CancellationToken.None);
    }

    [Fact]
    public async Task EntrySpotDispatch_RejectedOwnedPayloadsAreDisposed()
    {
        var node = new CapturingSpotNode();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        var spot = new CapturingSpot();
        var runner = new ZLinkRuntimeTaskRunner(new ThrowingRuntimeErrorSink(), CancellationToken.None);
        await runner.StopAsync();
        var pump = new ZLinkEntrySpotDispatchPump(runtime, activation: null, runner);
        pump.Attach(spot);

        var actorParts = CreateActorRequestParts(actorRef, "request", "discard", requestId: 99, flags: 1);
        var actorBody = actorParts[1].Message;
        spot.RaiseDispatch(new ZLinkBackendSpotDispatchInfo(
            ZLinkBackendSpotDispatchEvent.ActorReadable,
            ActorParts: actorParts));

        Assert.Throws<ObjectDisposedException>(() => actorBody.AsReadOnlySpan());
        await runtime.StopAsync(CancellationToken.None);
    }

    [Fact]
    public async Task EntrySpotActorDispatch_ConcurrentActors_StartsOutsideEntrySpotSerialLine_AndKeepsSameActorOrdering()
    {
        var probe = new DispatchProbe();
        var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddTransient<ProbeActorSendHandler>()
            .BuildServiceProvider();
        var activation = CreateActivation(services);
        await using var _ = activation.ConfigureAwait(false);
        activation.Configure();

        Assert.True(activation.TryResolveActorPacket(
            typeof(ProbeActor),
            CreateHeader("first"),
            out var descriptor));
        Assert.NotNull(descriptor);

        var actorA = new ProbeActor("actor-a");
        var actorB = new ProbeActor("actor-b");
        var stateA = new ZLinkActorRuntimeState(actorA.ActorId);
        var stateB = new ZLinkActorRuntimeState(actorB.ActorId);

        var firstA = DispatchAsync(activation, descriptor, stateA, actorA, "first");
        await probe.ActorAFirstStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var firstB = DispatchAsync(activation, descriptor, stateB, actorB, "first");
        await probe.ActorBStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var secondA = DispatchAsync(activation, descriptor, stateA, actorA, "second");
        await Task.Delay(100);
        Assert.False(probe.ActorASecondStarted.Task.IsCompleted);

        probe.ReleaseActorAFirst.SetResult();
        await probe.ActorASecondStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await Task.WhenAll(firstA, firstB, secondA).WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(
            new[]
            {
                "actor-a:first:start",
                "actor-b:first:start",
                "actor-a:first:end",
                "actor-a:second:start"
            },
            probe.Events.ToArray());
    }

    [Fact]
    public async Task EntrySpotRouteDispatch_UsesRoutedMessagesAlreadyDrainedByBackendCallback()
    {
        var probe = new RouteDispatchProbe();
        var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddTransient<ProbeRouteHandler>()
            .BuildServiceProvider();
        var spot = new CapturingSpot();
        var (activation, runtime) = CreateActivationWithRuntime(services, spot);
        await using var _ = activation.ConfigureAwait(false);
        activation.Configure();

        var pump = new ZLinkEntrySpotDispatchPump(
            runtime,
            activation,
            new ZLinkRuntimeTaskRunner(new ThrowingRuntimeErrorSink(), CancellationToken.None));
        pump.Attach(spot);

        var received = CreateRoutedReceived("routed-ok");
        spot.RaiseDispatch(new ZLinkBackendSpotDispatchInfo(
            ZLinkBackendSpotDispatchEvent.RouteReadable,
            RoutedMessages: [received]));

        Assert.Equal(
            "routed-ok",
            await probe.Message.Task.WaitAsync(TimeSpan.FromSeconds(5)));
    }

    [Fact]
    public async Task EntrySpotActorDispatch_NoBindRequest_RepliesViaNoBind_AndDoesNotBindSession()
    {
        var node = new CapturingSpotNode();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);

            await DispatchEntryActorPartsAsync(
                runtime,
                CreateActorRequestParts(actorRef, "request", "ok", requestId: 42, flags: 1),
                CancellationToken.None);

            var reply = Assert.Single(node.NoBindReplies);
            Assert.Equal(actorRef, reply.Actor);
            Assert.Equal(RoutingId.From("source-node"), reply.SourceNodeRid);
            Assert.Equal(RoutingId.From("source-session"), reply.SourceSessionRid);
            Assert.Equal<ulong>(42, reply.RequestId);
            Assert.Equal<uint>(1, reply.Flags);
            Assert.Empty(node.BoundSessionReplies);
            Assert.False(runtime.TryGetActorBoundSession(actor.ActorId, out _));

            var decoded = DecodeReplyFrame<ProbeReply>(Assert.Single(reply.Parts));
            Assert.Equal(ZlinkStreamMessageKind.Response, decoded.Header.Kind);
            Assert.Equal("ok:actor-a", decoded.Payload.Value);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task EntrySpotActorDispatch_BoundRequest_UsesBoundSession_AndBindsSession()
    {
        var node = new CapturingSpotNode();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);

            await DispatchEntryActorPartsAsync(
                runtime,
                CreateActorRequestParts(actorRef, "request", "bound", requestId: 0, flags: 0),
                CancellationToken.None);

            Assert.Empty(node.NoBindReplies);
            var boundReply = Assert.Single(node.BoundSessionReplies);
            Assert.Equal(actorRef, boundReply.Actor);
            Assert.True(runtime.TryGetActorBoundSession(actor.ActorId, out var boundSession));
            Assert.Equal(RoutingId.From("source-session"), boundSession.SessionRid);

            var decoded = DecodeReplyFrame<ProbeReply>(Assert.Single(boundReply.Parts));
            Assert.Equal(ZlinkStreamMessageKind.Response, decoded.Header.Kind);
            Assert.Equal("bound:actor-a", decoded.Payload.Value);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task EntrySpotActorDispatch_NoBindHandlerException_RepliesNoBindError()
    {
        var node = new CapturingSpotNode();
        var observer = new CapturingMessageFlowObserver();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node, observer);
        try
        {
            RegisterProbeActor(runtime, actorRef);

            await DispatchEntryActorPartsAsync(
                runtime,
                CreateActorRequestParts(actorRef, "throw", "boom", requestId: 43, flags: 1),
                CancellationToken.None);

            var reply = Assert.Single(node.NoBindReplies);
            var decoded = DecodeReplyFrame<ZLinkStreamWireError>(Assert.Single(reply.Parts));
            Assert.Equal(ZlinkStreamMessageKind.Error, decoded.Header.Kind);
            Assert.Equal(nameof(InvalidOperationException), decoded.Payload.Code);
            Assert.Contains("boom", decoded.Payload.Message, StringComparison.Ordinal);
            Assert.Empty(node.BoundSessionReplies);
            Assert.False(runtime.TryGetActorBoundSession(actorRef.ActorId, out _));

            var observed = await observer.WaitAsync(TimeSpan.FromSeconds(2));
            Assert.Equal(ZLinkMessageFlowOutcome.Error, observed.Outcome);
            Assert.Equal(ZLinkDispatchErrorSurface.SpotActor, observed.Surface);
            Assert.Equal(ZLinkDispatchMessageKind.ActorRequest, observed.MessageKind);
            Assert.Equal(ZLinkDispatchErrorReason.HandlerException, observed.ErrorReason);
            Assert.Equal(ZLinkDispatchErrorAction.ReplyError, observed.ErrorAction);
            Assert.Equal("throw", observed.PacketName);
            Assert.Equal("actor-a", observed.ActorId);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task EntrySpotActorDispatch_NoBindMissingActor_RepliesNoBindError()
    {
        var node = new CapturingSpotNode();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        try
        {
            await DispatchEntryActorPartsAsync(
                runtime,
                CreateActorRequestParts(actorRef, "request", "missing", requestId: 44, flags: 1),
                CancellationToken.None);

            var reply = Assert.Single(node.NoBindReplies);
            var decoded = DecodeReplyFrame<ZLinkStreamWireError>(Assert.Single(reply.Parts));
            Assert.Equal(ZlinkStreamMessageKind.Error, decoded.Header.Kind);
            Assert.Contains("not available", decoded.Payload.Message, StringComparison.Ordinal);
            Assert.Empty(node.BoundSessionReplies);
            Assert.False(runtime.TryGetActorBoundSession(actorRef.ActorId, out _));
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task EntrySpotActorDispatch_MalformedHeader_DisposesFrame_AndContinuesBatch()
    {
        var node = new CapturingSpotNode();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        using var badHeader = Message.From([0x01, 0x02, 0x03]);
        using var badBody = Message.From("discarded-body");
        try
        {
            RegisterProbeActor(runtime, actorRef);
            var validParts = CreateActorRequestParts(actorRef, "request", "ok", requestId: 45, flags: 1);
            var parts = new List<ZLinkBackendActorPart>
            {
                new(
                    actorRef,
                    RoutingId.From("source-node"),
                    RoutingId.From("source-session"),
                    44,
                    1,
                    badHeader,
                    true),
                new(
                    actorRef,
                    RoutingId.From("source-node"),
                    RoutingId.From("source-session"),
                    44,
                    1,
                    badBody,
                    false)
            };
            parts.AddRange(validParts);

            await DispatchEntryActorPartsAsync(
                runtime,
                parts,
                CancellationToken.None);

            Assert.Throws<ObjectDisposedException>(() => badHeader.AsReadOnlySpan());
            Assert.Throws<ObjectDisposedException>(() => badBody.AsReadOnlySpan());
            var reply = Assert.Single(node.NoBindReplies);
            var decoded = DecodeReplyFrame<ProbeReply>(Assert.Single(reply.Parts));
            Assert.Equal("ok:actor-a", decoded.Payload.Value);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task EntrySpotJoin_NotConnected_DisposesNativeReplyPartsBeforeRemoteFallback()
    {
        var node = new CapturingSpotNode();
        using var leakedPart = Message.From("not-connected-reply");
        node.EntrySpotJoinReplyParts = [leakedPart];
        node.EntrySpotJoinResult = new ZLinkBackendActorJoinEntrySpotResult(
            RequestResult.NotConnected,
            0,
            new ZLinkBackendActorRef(RoutingId.From("actor-node"), "actor-a", 1),
            RoutingId.From("remote-node"),
            RoutingId.From("entry-spot"),
            0,
            0);
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);

            var exception = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
                await runtime.JoinActorEntrySpotAsync(
                    RoutingId.From("remote-node"),
                    actor,
                    ZLinkMessage.Empty,
                    CancellationToken.None));

            Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, exception.Kind);
            Assert.Throws<ObjectDisposedException>(() => leakedPart.AsReadOnlySpan());
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    private static async Task DispatchAsync(
        ZLinkEntrySpotActivation activation,
        ZLinkSpotActorPacketDescriptor descriptor,
        ZLinkActorRuntimeState state,
        ProbeActor actor,
        string name)
    {
        var header = CreateHeader(name);
        using var body = Message.From(Encoding.UTF8.GetBytes(name));
        await state.ExecuteDispatchAsync(
                header,
                ct => activation.InvokeActorPacketAsync(
                    descriptor,
                    actor,
                    header,
                    body,
                    ct),
                CancellationToken.None)
            .ConfigureAwait(false);
    }

    private static ValueTask DispatchEntryActorPartsAsync(
        ZLinkFrameworkRuntime runtime,
        IReadOnlyList<ZLinkBackendActorPart> parts,
        CancellationToken cancellationToken)
    {
        var pipeline = new ZLinkActorInboundPipeline(
            runtime,
            new ZLinkEntrySpotActorInboundEndpoint(runtime));
        var frames = ZLinkActorHandoffIngress.CaptureMovingFrames(runtime, parts);
        return pipeline.DispatchAsync(frames, cancellationToken);
    }

    private static ZlinkStreamHeader CreateHeader(string name)
    {
        return new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            name,
            ZlinkStreamMetadata.Empty);
    }

    private static ZLinkEntrySpotActivation CreateActivation(IServiceProvider services)
    {
        return CreateActivationWithRuntime(services, new CapturingSpot()).Activation;
    }

    private static async Task<(ZLinkFrameworkRuntime Runtime, ZLinkBackendActorRef ActorRef)> CreateStartedRuntimeAsync(
        CapturingSpotNode node,
        IZLinkMessageFlowObserver? messageFlowObserver = null)
    {
        var services = new ServiceCollection()
            .AddTransient<ProbeActorRequestHandler>()
            .AddTransient<ProbeActorDestroyRequestHandler>()
            .AddTransient<ProbeActorThrowingRequestHandler>()
            .BuildServiceProvider();
        var registration = new ZLinkFrameworkRegistration
        {
            DefaultRequestTimeout = TimeSpan.FromSeconds(1),
            ImplicitHandlerAutoRegistrationEnabled = false
        };
        if (messageFlowObserver is not null)
            registration.DispatchOptions.SetMessageFlowObserver(messageFlowObserver);
        registration.SpotNodes["entry"] = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "entry",
            RoutingId = RoutingId.From("entry-node"),
            Router = new ZLinkSpotRouterCapabilityRegistration { BindEndpoint = "inproc://entry" },
            EntrySpotType = typeof(ProbeEntrySpot),
            ActorFactories =
            {
                ["probe"] = typeof(ProbeActorFactory)
            }
        };
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));
        await runtime.StartAsync(CancellationToken.None);
        return (runtime, new ZLinkBackendActorRef(RoutingId.From("actor-node"), "actor-a", 1));
    }

    private static (ZLinkEntrySpotActivation Activation, ZLinkFrameworkRuntime Runtime) CreateActivationWithRuntime(
        IServiceProvider services,
        IZLinkBackendSpot spot)
    {
        var registration = new ZLinkFrameworkRegistration();
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new ThrowingBackendAdapterFactory(),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));

        var scope = services.CreateAsyncScope();
        var activation = new ZLinkEntrySpotActivation(
            runtime,
            services,
            scope,
            spot,
            typeof(ProbeEntrySpot),
            RoutingId.From("entry-node"),
            "entry",
            "entry-channel",
            TimeSpan.FromSeconds(5),
            TimeSpan.FromSeconds(1));
        activation.InitializeRuntimeResources();
        return (activation, runtime);
    }

    private static ProbeActor RegisterProbeActor(
        ZLinkFrameworkRuntime runtime,
        ZLinkBackendActorRef actorRef)
    {
        var actor = new ProbeActor(actorRef.ActorId);
        var state = runtime.GetOrCreateActorState(actor.ActorId);
        state.BindActorInstance(actor);
        state.BindNativeActorRef(actorRef);
        return actor;
    }

    private static IReadOnlyList<ZLinkBackendActorPart> CreateActorRequestParts(
        ZLinkBackendActorRef actorRef,
        string packetName,
        string value,
        ulong requestId,
        uint flags)
    {
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            new ZlinkStreamRequestSeq(7),
            packetName,
            ZlinkStreamMetadata.Empty,
            "corr-1");
        return
        [
            new ZLinkBackendActorPart(
                actorRef,
                RoutingId.From("source-node"),
                RoutingId.From("source-session"),
                requestId,
                flags,
                Message.From(ZLinkStreamProtocolDefaults.EncodeHeader(header).Span),
                true),
            new ZLinkBackendActorPart(
                actorRef,
                RoutingId.From("source-node"),
                RoutingId.From("source-session"),
                requestId,
                flags,
                Message.From(ZLinkEnvelopeCodec.EncodeJsonBytes(value, typeof(string))),
                false)
        ];
    }

    private static (ZlinkStreamHeader Header, T Payload) DecodeReplyFrame<T>(byte[] frame)
    {
        var headerSize = BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(0, 2));
        var payloadSize = BinaryPrimitives.ReadUInt32BigEndian(frame.AsSpan(2, 4));
        var header = ZLinkStreamProtocolDefaults.DecodeHeader(frame.AsMemory(6, headerSize));
        var payload = JsonSerializer.Deserialize<T>(
                          frame.AsSpan(6 + headerSize, checked((int)payloadSize)),
                          ZLinkJsonSerializerOptions.Default)
                      ?? throw new InvalidOperationException("Reply payload was null.");
        return (header, payload);
    }

    private static Received CreateRoutedReceived(string value)
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            "entry-channel",
            nameof(ProbeRouteMessage));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
            header,
            new ProbeRouteMessage(value),
            null);
        using var context = global::Systems.Zlink.Zlink.CreateContext();
        using var node = context.CreateSpotNode();
        using var sender = node.CreateSpot();
        using var receiver = node.CreateSpot();
        var nodeRid = RoutingId.From("route-source-node");
        var senderRid = RoutingId.From("route-source-spot");
        var receiverRid = RoutingId.From("route-receiver-spot");

        node.SetRoutingId(nodeRid);
        sender.SetRoutingId(senderRid);
        receiver.SetRoutingId(receiverRid);
        sender.SendToSpot(nodeRid, receiverRid)
            .Messages(parts)
            .Submit();

        var received = Received.Create();
        if (!WaitUntil(() => receiver.RecvRouted(received, RecvFlags.DontWait), TimeSpan.FromSeconds(5)))
            throw new TimeoutException("Timed out creating a routed Received message for the entry spot dispatch test.");
        return received;
    }

    private static bool WaitUntil(Func<bool> predicate, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow.Add(timeout);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate()) return true;
            Thread.Sleep(10);
        }

        return false;
    }

    private sealed class DispatchProbe
    {
        public ConcurrentQueue<string> Events { get; } = new();

        public TaskCompletionSource ActorAFirstStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ActorASecondStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ActorBStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseActorAFirst { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class ProbeActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context => throw new NotSupportedException();
    }

    private sealed class ProbeActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new ProbeActor(actorId));
        }
    }

    private sealed class CreationProbeActor(string actorId, IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public void Configure()
        {
        }
    }

    private sealed class CreationProbeActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new CreationProbeActor(actorId, context));
        }
    }

    private sealed class ControlledCreationProbe
    {
        public TaskCompletionSource Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Release { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class ControlledCreationProbeActorFactory(ControlledCreationProbe probe)
        : IZLinkActorFactory
    {
        public async ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            probe.Started.TrySetResult();
            await probe.Release.Task.WaitAsync(cancellationToken);
            return new CreationProbeActor(actorId, context);
        }
    }

    private sealed class FailingPublishActorLifecycle : IZLinkActorLocationLifecycle
    {
        public int ReleaseCalls { get; private set; }

        public Action? BeforeRelease { get; set; }

        public async ValueTask<ZLinkActorClaimActivation<TActor>> ExecuteActorClaimThenActivateAsync<TActor>(
            string actorType,
            string actorId,
            RoutingId nodeRid,
            Func<CancellationToken, ValueTask>? deactivate,
            Func<CancellationToken, ValueTask<TActor>> activate,
            CancellationToken cancellationToken,
            ZLinkActorClaimMode claimMode = ZLinkActorClaimMode.NewOwner)
            where TActor : class
        {
            _ = actorType;
            _ = actorId;
            _ = nodeRid;
            _ = deactivate;
            _ = claimMode;
            return new ZLinkActorClaimActivation<TActor>(
                await activate(cancellationToken),
                null);
        }

        public ValueTask<ZLinkActorClaimResult> ClaimActorAsync(
            string actorType,
            string actorId,
            RoutingId nodeRid,
            Func<CancellationToken, ValueTask>? deactivate,
            CancellationToken cancellationToken)
            => throw new NotSupportedException();

        public ValueTask PublishActorRefAsync(
            string actorId,
            ActorRef actorRef,
            CancellationToken cancellationToken = default)
            => ValueTask.FromException(new InvalidOperationException("publish failed"));

        public ValueTask ReleaseActorAsync(
            string actorId,
            CancellationToken cancellationToken = default)
        {
            BeforeRelease?.Invoke();
            ReleaseCalls++;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class FailOnceRemoveActorStore(IZLinkActorLocationStore inner)
        : IZLinkActorLocationStore
    {
        private int _removeAttempts;

        public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
            ZLinkActorLocation actor,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default)
            => inner.UpdateActorAsync(actor, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default)
        {
            if (Interlocked.Increment(ref _removeAttempts) == 1)
                return ValueTask.FromException<ZLinkLocationWriteResult>(
                    new InvalidOperationException("release failed"));
            return inner.RemoveActorAsync(key, owner, cancellationToken);
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

    private sealed class ProbeEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddPacket<ProbeRouteHandler>();
            Context.Handlers.AddHandler<ProbeActorSendHandler>("first");
            Context.Handlers.AddHandler<ProbeActorSendHandler>("second");
            Context.Handlers.AddHandler<ProbeActorRequestHandler>("request");
            Context.Handlers.AddHandler<ProbeActorDestroyRequestHandler>("destroy-request");
            Context.Handlers.AddHandler<ProbeActorThrowingRequestHandler>("throw");
        }
    }

    private sealed record ProbeRouteMessage(string Value);

    private sealed record ProbeReply(string Value);

    private sealed class RouteDispatchProbe
    {
        public TaskCompletionSource<string> Message { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class ProbeRouteHandler(RouteDispatchProbe probe)
        : IZLinkSpotPacketHandler<ProbeEntrySpot, ProbeRouteMessage>
    {
        public ValueTask HandleAsync(
            ProbeEntrySpot spot,
            ProbeRouteMessage message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            probe.Message.SetResult(message.Value);
            return ValueTask.CompletedTask;
        }
    }

    private sealed class ProbeActorSendHandler(DispatchProbe probe)
        : IZLinkEntrySpotActorSendHandler<ProbeEntrySpot, ProbeActor, string>
    {
        public async ValueTask HandleAsync(
            ProbeEntrySpot entrySpot,
            ProbeActor actor,
            ZLinkSpotActorSendContext context,
            string message,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            probe.Events.Enqueue($"{actor.ActorId}:{message}:start");

            if (actor.ActorId == "actor-a" && message == "first")
            {
                probe.ActorAFirstStarted.SetResult();
                await probe.ReleaseActorAFirst.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
                probe.Events.Enqueue($"{actor.ActorId}:{message}:end");
                return;
            }

            if (actor.ActorId == "actor-a" && message == "second")
                probe.ActorASecondStarted.SetResult();

            if (actor.ActorId == "actor-b")
                probe.ActorBStarted.SetResult();
        }
    }

    private sealed class ProbeActorRequestHandler
        : IZLinkEntrySpotActorRequestHandler<ProbeEntrySpot, ProbeActor, string, ProbeReply>
    {
        public ValueTask<ProbeReply> HandleAsync(
            ProbeEntrySpot entrySpot,
            ProbeActor actor,
            ZLinkSpotActorRequestContext context,
            string request,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            _ = cancellationToken;
            return ValueTask.FromResult(new ProbeReply($"{request}:{actor.ActorId}"));
        }
    }

    private sealed class ProbeActorDestroyRequestHandler
        : IZLinkEntrySpotActorRequestHandler<ProbeEntrySpot, ProbeActor, string, ProbeReply>
    {
        public async ValueTask<ProbeReply> HandleAsync(
            ProbeEntrySpot entrySpot,
            ProbeActor actor,
            ZLinkSpotActorRequestContext context,
            string request,
            CancellationToken cancellationToken)
        {
            _ = context;
            await entrySpot.Context.DestroyActorAsync(actor, cancellationToken);
            return new ProbeReply($"{request}:{actor.ActorId}");
        }
    }

    private sealed class ProbeActorThrowingRequestHandler
        : IZLinkEntrySpotActorRequestHandler<ProbeEntrySpot, ProbeActor, string, ProbeReply>
    {
        public ValueTask<ProbeReply> HandleAsync(
            ProbeEntrySpot entrySpot,
            ProbeActor actor,
            ZLinkSpotActorRequestContext context,
            string request,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = actor;
            _ = context;
            _ = cancellationToken;
            throw new InvalidOperationException($"boom:{request}");
        }
    }

    private sealed record CapturedActorReply(
        ZLinkBackendActorRef Actor,
        RoutingId SourceNodeRid,
        RoutingId SourceSessionRid,
        ulong RequestId,
        uint Flags,
        IReadOnlyList<byte[]> Parts);

    private sealed class CapturingSpot : IZLinkBackendSpot
    {
        private Action<ZLinkBackendSpotDispatchInfo>? _dispatchHandler;

        public object NativeInstance => this;

        public RoutingId RoutingId => RoutingId.From("entry-spot");

        public Func<ValueTask>? DisposeHandler { get; set; }

        public ValueTask DisposeAsync()
            => DisposeHandler is { } handler ? handler() : ValueTask.CompletedTask;

        public void SetRoutingId(RoutingId routingId) { }

        public void SetSubscription(string topic) { }

        public bool Subscribe(TopicMessage result, RecvFlags flags) => false;

        public bool RecvRoute(Received result, RecvFlags flags) => false;

        public void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler)
        {
            _dispatchHandler = handler;
        }

        public void RaiseDispatch(ZLinkBackendSpotDispatchInfo info)
        {
            (_dispatchHandler ?? throw new InvalidOperationException("Dispatch handler was not attached.")).Invoke(info);
        }

        public void OnSendReady(Action handler) { }

        public bool RequestToChannel(
            string channelName,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public bool RequestToChannel(
            string channelName,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public bool SendToChannel(string channelName, Message message, SendFlags flags) => false;

        public bool SendToChannel(string channelName, IReadOnlyList<Message> parts, SendFlags flags) => false;

        public bool Publish(string topic, Message message, SendFlags flags) => false;

        public bool Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags) => false;

        public bool SendToSpot(RoutingId targetRid, RoutingId targetSpotRid, Message message, SendFlags flags) => false;

        public bool SendToSpot(
            RoutingId targetRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            SendFlags flags) => false;

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId targetSpotRid,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public ZLinkBackendActorJoinRequest? RecvActorJoin(RecvFlags flags) => null;

        public ZLinkBackendSpotActorLifecycleEvent? RecvActorLifecycle(RecvFlags flags) => null;

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            Message reply) { }

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            IReadOnlyList<Message> parts) { }

        public void OnActorLifecycle(
            Action<ZLinkBackendSpotActorLifecycleInfo>? onJoin,
            Action<ZLinkBackendSpotActorLifecycleInfo>? onLeave) { }
    }

    private sealed class CapturingSpotNode : IZLinkBackendSpotNode
    {
        private readonly CapturingSpot _entrySpot = new();

        public Func<ValueTask>? DisposeHandler
        {
            get => _entrySpot.DisposeHandler;
            set => _entrySpot.DisposeHandler = value;
        }

        public List<CapturedActorReply> NoBindReplies { get; } = [];

        public List<(ZLinkBackendActorRef Actor, IReadOnlyList<byte[]> Parts)> BoundSessionReplies { get; } = [];

        public ZLinkBackendActorJoinEntrySpotResult? EntrySpotJoinResult { get; set; }

        public IReadOnlyList<Message> EntrySpotJoinReplyParts { get; set; } = [];

        public List<ZLinkBackendActorRef> DestroyedActors { get; } = [];

        public ConcurrentQueue<string> LifecycleEvents { get; } = new();

        public List<ZLinkBackendActorRef> CreatedActors { get; } = [];

        public Action<ZLinkBackendActorRef>? BeforeDestroy { get; set; }

        public Action<ZLinkBackendActorRef>? BeforeNoBindReply { get; set; }

        public Exception? DestroyFailure { get; set; }

        public Func<ZLinkBackendActorRef, CancellationToken, ValueTask>? DestroyHandler { get; set; }

        public ConcurrentQueue<bool> ForwardResults { get; } = new();

        public List<(bool HasMore, byte[] Payload)> ForwardedParts { get; } = [];

        public TaskCompletionSource FinalPartAttempted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public object NativeInstance => this;

        public RoutingId RoutingId { get; private set; }

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void SetRoutingId(RoutingId routingId)
        {
            RoutingId = routingId;
        }

        public void SetPublisherRoutingId(RoutingId routingId) { }

        public void SetSubscriberRoutingId(RoutingId routingId) { }

        public void SetRouterBind(string endpoint) { }

        public void SetPubBind(string endpoint) { }

        public void ConnectPeer(string endpoint) { }

        public void ConnectPeer(RoutingId peerRid, string endpoint) { }

        public void DisconnectPeer(string endpoint) { }

        public IZLinkBackendSpot CreateSpot() => new CapturingSpot();

        public IZLinkBackendSpot GetOrCreateSpot(RoutingId targetSpotRid, out bool created)
        {
            created = true;
            return new CapturingSpot();
        }

        public ZLinkSpotNodeStatus Status() => new(
            "entry",
            "inproc://entry",
            RoutingId,
            ZLinkSpotNodeState.Ready,
            0,
            0,
            0,
            0,
            0,
            0,
            0);

        public IReadOnlyList<ZLinkSpotNodePeerEntry> Peers() => [];

        public IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects() => [];

        public IZLinkBackendSpotRouteBridge CreateRouteBridge() => throw new NotSupportedException();

        public IZLinkBackendSpot EntrySpot() => _entrySpot;

        public ZLinkBackendActorRef CreateActor(string actorId, Message createRequest)
        {
            var actor = new ZLinkBackendActorRef(RoutingId, actorId, 1);
            CreatedActors.Add(actor);
            return actor;
        }

        public ZLinkBackendActorRef? ActorLookup(string actorId) => null;

        public bool JoinActor(
            ZLinkBackendActorRef actor,
            RoutingId destNodeRid,
            RoutingId destSpotRid,
            Message message,
            RequestCallback callback,
            TimeSpan? timeout) => false;

        public bool JoinActor(
            ZLinkBackendActorRef actor,
            RoutingId destNodeRid,
            RoutingId destSpotRid,
            IReadOnlyList<Message> parts,
            ActorJoinCallback callback,
            TimeSpan? timeout) => false;

        public bool JoinActorEntrySpot(
            ZLinkBackendActorRef actor,
            RoutingId destNodeRid,
            Message request,
            ActorJoinEntrySpotCallback callback,
            TimeSpan? timeout)
        {
            if (EntrySpotJoinResult is not { } result) return false;

            callback(result, EntrySpotJoinReplyParts);
            return true;
        }

        public async ValueTask DestroyActorAsync(
            ZLinkBackendActorRef actor,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            BeforeDestroy?.Invoke(actor);
            DestroyedActors.Add(actor);
            if (DestroyFailure is { } failure) throw failure;
            if (DestroyHandler is { } handler)
                await handler(actor, cancellationToken);
        }

        public bool SendActorBoundSession(
            ZLinkBackendActorRef actor,
            IReadOnlyList<Message> parts,
            SendFlags flags)
        {
            BoundSessionReplies.Add((actor, CopyParts(parts)));
            return true;
        }

        public bool SendToActor(
            ZLinkBackendActorRef actor,
            IReadOnlyList<Message> parts,
            SendFlags flags) => false;

        public ValueTask<IReadOnlyList<Message>> RequestToActorAsync(
            ZLinkBackendActorRef actor,
            IReadOnlyList<Message> parts,
            TimeSpan? timeout,
            CancellationToken cancellationToken) => throw new NotSupportedException();

        public void ReplyActorNoBind(
            ZLinkBackendActorRef actor,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid,
            ulong requestId,
            uint flags,
            IReadOnlyList<Message> parts)
        {
            BeforeNoBindReply?.Invoke(actor);
            NoBindReplies.Add(new CapturedActorReply(
                actor,
                sourceNodeRid,
                sourceSessionRid,
                requestId,
                flags,
                CopyParts(parts)));
        }

        public bool ForwardActorBoundSessionPart(
            ZLinkBackendActorRef actor,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid,
            Message message,
            bool hasMore,
            SendFlags flags)
        {
            var result = !ForwardResults.TryDequeue(out var configured) || configured;
            lock (ForwardedParts)
                ForwardedParts.Add((hasMore, message.AsReadOnlySpan().ToArray()));
            if (!hasMore) FinalPartAttempted.TrySetResult();
            return result;
        }

        public void BindRemoteActorBoundSession(
            ZLinkBackendActorRef actor,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid) { }

        public void CloseActorBoundSession(
            ZLinkBackendActorRef actor,
            TimeSpan timeout,
            CancellationToken cancellationToken) { }

        private static IReadOnlyList<byte[]> CopyParts(IReadOnlyList<Message> parts)
        {
            return parts.Select(static part => part.AsReadOnlySpan().ToArray()).ToArray();
        }
    }

    private sealed class CapturingBackendAdapterFactory(CapturingSpotNode node) : IZLinkBackendAdapterFactory
    {
        private readonly CapturingChannelBackendAdapter _channelAdapter = new();

        public IZLinkChannelBackendAdapter CreateChannelAdapter() => _channelAdapter;

        public IZLinkSpotBackendAdapter CreateSpotAdapter() => new CapturingSpotBackendAdapter(node);

        public IZLinkStreamBackendAdapter CreateStreamAdapter() => throw new NotSupportedException();

        public IZLinkMonitoringBackendAdapter CreateMonitoringAdapter() => throw new NotSupportedException();
    }

    private sealed class CapturingChannelBackendAdapter : IZLinkChannelBackendAdapter
    {
        public IZLinkBackendContext CreateContext() => new CapturingBackendContext();

        public IZLinkBackendDealerSocket CreateDealerSocket(IZLinkBackendContext context) =>
            throw new NotSupportedException();

        public IZLinkBackendRouterSocket CreateRouterSocket(IZLinkBackendContext context) =>
            throw new NotSupportedException();

        public IZLinkBackendPublisherSocket CreatePublisherSocket(IZLinkBackendContext context) =>
            throw new NotSupportedException();

        public IZLinkBackendSubscriberSocket CreateSubscriberSocket(IZLinkBackendContext context) =>
            throw new NotSupportedException();
    }

    private sealed class CapturingSpotBackendAdapter(CapturingSpotNode node) : IZLinkSpotBackendAdapter
    {
        public IZLinkBackendSpotNode CreateSpotNode(
            IZLinkBackendContext context,
            SpotNodeMode mode) => node;
    }

    private sealed class CapturingBackendContext : IZLinkBackendContext
    {
        public object NativeInstance => this;

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void Shutdown() { }
    }

    private sealed class CapturingMessageFlowObserver : IZLinkMessageFlowObserver
    {
        private readonly TaskCompletionSource<ZLinkMessageFlowEvent> _observed =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask OnMessageFlowAsync(
            ZLinkMessageFlowEvent flow,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            _observed.TrySetResult(flow);
            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkMessageFlowEvent> WaitAsync(TimeSpan timeout)
        {
            return await _observed.Task.WaitAsync(timeout);
        }
    }

    private sealed class ThrowingBackendAdapterFactory : IZLinkBackendAdapterFactory
    {
        public IZLinkChannelBackendAdapter CreateChannelAdapter() => throw new NotSupportedException();

        public IZLinkSpotBackendAdapter CreateSpotAdapter() => throw new NotSupportedException();

        public IZLinkStreamBackendAdapter CreateStreamAdapter() => throw new NotSupportedException();

        public IZLinkMonitoringBackendAdapter CreateMonitoringAdapter() => throw new NotSupportedException();
    }

    private sealed class ThrowingRuntimeErrorSink : IZLinkRuntimeErrorSink
    {
        public void ReportHandlerException(Exception exception)
        {
            throw new InvalidOperationException("Runtime handler failed.", exception);
        }

        public void ReportRuntimeTaskException(string name, Exception exception)
        {
            throw new InvalidOperationException($"Runtime task '{name}' failed.", exception);
        }
    }
}
