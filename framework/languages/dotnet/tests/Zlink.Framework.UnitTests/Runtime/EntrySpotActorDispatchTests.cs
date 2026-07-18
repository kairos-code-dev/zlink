using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Diagnostics.Metrics;
using System.Reflection;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Runtime.Protocol;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests.Runtime;

[Collection(RuntimeMetricsCollection.Name)]
public sealed partial class EntrySpotActorDispatchTests
{
    [Fact]
    public async Task EntrySpot_Configuration_Is_Independent_And_RoutingId_Is_Applied_Before_Bind()
    {
        var services = new ServiceCollection().BuildServiceProvider();
        var node = new CapturingSpotNode();
        var registration = new ZLinkFrameworkRegistration();
        registration.SpotNodes["entry"] = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "entry",
            RoutingId = RoutingId.From("entry-node"),
            Router = new ZLinkSpotRouterCapabilityRegistration
            {
                BindEndpoint = "inproc://entry"
            },
            EntrySpotOptions =
            {
                RoutingId = RoutingId.From("configured-entry")
            }
        };
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration));

        await runtime.StartAsync(CancellationToken.None);
        try
        {
            Assert.Null(registration.SpotNodes["entry"].EntrySpotType);
            Assert.Equal(RoutingId.From("configured-entry"), node.EntryRoutingId);
            // RouteMesh 10.0.0 MeshNode startup ordering (spec 21-mesh-node §3):
            // routing id, then ROUTER bind (+ channels + Start), then entry-spot
            // configuration. The 9.x order applied the entry-spot routing id before
            // the router bind; the node now binds/starts before any entry-spot use.
            Assert.Equal(
                new[]
                {
                    "node-rid:entry-node",
                    "router-bind:inproc://entry",
                    "entry-facade",
                    "entry-rid:configured-entry"
                },
                node.InitializationEvents.Take(4));
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task SpotNode_Initializer_Applies_Publisher_And_Subscriber_Role_Config()
    {
        var services = new ServiceCollection().BuildServiceProvider();
        var node = new CapturingSpotNode();
        var registration = new ZLinkFrameworkRegistration();
        registration.SpotNodes["pubsub"] = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "pubsub",
            RoutingId = RoutingId.From("pubsub-node"),
            // RouteMesh 10.0.0: publisher config is node-level (SpotPublisherConfig,
            // read by the initializer via ApplyRoleConfig); subscriber config stays
            // under the PubSub capability.
            SpotPublisherConfig =
            {
                SendHighWaterMark = 17,
                SendTimeout = TimeSpan.FromMilliseconds(21),
                Linger = TimeSpan.FromMilliseconds(34),
                NoDrop = true
            },
            PubSub = new ZLinkSpotPubSubCapabilityRegistration
            {
                SubscriberConfig =
                {
                    ReceiveHighWaterMark = 55,
                    ReceiveTimeout = TimeSpan.FromMilliseconds(89),
                    Linger = TimeSpan.FromMilliseconds(144)
                }
            }
        };
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration));

        await runtime.StartAsync(CancellationToken.None);
        try
        {
            Assert.Equal(17, node.PublisherConfig?.SendHighWaterMark);
            Assert.Equal(TimeSpan.FromMilliseconds(34), node.PublisherConfig?.Linger);
            Assert.True(node.PublisherConfig?.NoDrop);
            Assert.Equal(55, node.SubscriberConfig?.ReceiveHighWaterMark);
            Assert.Equal(TimeSpan.FromMilliseconds(89), node.SubscriberConfig?.ReceiveTimeout);
            Assert.Equal(TimeSpan.FromMilliseconds(144), node.SubscriberConfig?.Linger);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Actor_Creation_Observes_The_Configured_EntrySpot_RoutingId()
    {
        var services = new ServiceCollection()
            .AddScoped<CreationProbeActorFactory>()
            .BuildServiceProvider();
        var node = new CapturingSpotNode();
        var registration = new ZLinkFrameworkRegistration
        {
            ImplicitHandlerAutoRegistrationEnabled = false
        };
        registration.SpotNodes["entry"] = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "entry",
            RoutingId = RoutingId.From("entry-node"),
            Router = new ZLinkSpotRouterCapabilityRegistration
            {
                BindEndpoint = "inproc://entry-actor"
            },
            EntrySpotType = typeof(ProbeEntrySpot),
            EntrySpotOptions =
            {
                RoutingId = RoutingId.From("configured-entry")
            },
            ActorFactories =
            {
                ["probe"] = typeof(CreationProbeActorFactory)
            }
        };
        registration.ActorCatalog.Build(registration.SpotNodes.Values);
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration));
        await runtime.StartAsync(CancellationToken.None);
        try
        {
            var created = await runtime.CreateActorAsync("actor-entry-rid", "probe");

            Assert.Equal("actor-entry-rid", created.Actor.ActorId);
            Assert.Equal(
                RoutingId.From("configured-entry"),
                Assert.Single(node.CreatedActorEntryRids));
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

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
        var sessions = new ZLinkActorSessionManager(
            runtime, services, () => node, null, new ZLinkBoundSessionService(runtime));
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
        ZLinkActorContext EnsureContext() => state.GetOrCreateContext(
            () => new ZLinkActorContext(runtime, state, new ZLinkBoundSessionService(runtime)));
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
        await using var lifecycle = new ZLinkLocationLifecycle(locationRuntime, resolvers);
        await CreateTrackedActorOwnershipAsync(
            lifecycle.ActorOwnership,
            "probe",
            "actor-destroy-not-found",
            RoutingId.From("actor-node"),
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
        var sessions = new ZLinkActorSessionManager(
            runtime, services, () => node, lifecycle, new ZLinkBoundSessionService(runtime));
        var state = sessions.GetOrCreateState("actor-destroy-not-found");
        var context = state.GetOrCreateContext(
            () => new ZLinkActorContext(runtime, state, new ZLinkBoundSessionService(runtime)));
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
        await using var lifecycle = new ZLinkLocationLifecycle(locationRuntime, resolvers);
        await CreateTrackedActorOwnershipAsync(
            lifecycle.ActorOwnership,
            "probe",
            "actor-destroy-native-retry",
            RoutingId.From("actor-node"),
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
        var sessions = new ZLinkActorSessionManager(
            runtime, services, () => node, lifecycle, new ZLinkBoundSessionService(runtime));
        var state = sessions.GetOrCreateState("actor-destroy-native-retry");
        var context = state.GetOrCreateContext(
            () => new ZLinkActorContext(runtime, state, new ZLinkBoundSessionService(runtime)));
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
        await using var lifecycle = new ZLinkLocationLifecycle(locationRuntime, resolvers);
        await CreateTrackedActorOwnershipAsync(
            lifecycle.ActorOwnership,
            "probe",
            "actor-ownership-loss-retry",
            RoutingId.From("actor-node"),
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
        var sessions = new ZLinkActorSessionManager(
            runtime, services, () => node, lifecycle, new ZLinkBoundSessionService(runtime));
        var state = sessions.GetOrCreateState("actor-ownership-loss-retry");
        var context = state.GetOrCreateContext(
            () => new ZLinkActorContext(runtime, state, new ZLinkBoundSessionService(runtime)));
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
        await using var lifecycle = new ZLinkLocationLifecycle(locationRuntime, resolvers);
        await CreateTrackedActorOwnershipAsync(
            lifecycle.ActorOwnership,
            "probe",
            "actor-destroy-retry",
            RoutingId.From("actor-node"),
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
        var sessions = new ZLinkActorSessionManager(
            runtime, services, () => node, lifecycle, new ZLinkBoundSessionService(runtime));
        var state = sessions.GetOrCreateState("actor-destroy-retry");
        var context = state.GetOrCreateContext(
            () => new ZLinkActorContext(runtime, state, new ZLinkBoundSessionService(runtime)));
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
        var sessions = new ZLinkActorSessionManager(
            runtime, services, () => node, null, new ZLinkBoundSessionService(runtime));
        var state = sessions.GetOrCreateState("actor-destroy-concurrent");
        var context = state.GetOrCreateContext(
            () => new ZLinkActorContext(runtime, state, new ZLinkBoundSessionService(runtime)));
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

            using var overflowBody = Message.From(Encoding.UTF8.GetBytes("overflow"));
            var overflow = await Assert.ThrowsAsync<ZLinkFrameworkException>(() => Task.Run(() =>
                    forwarder.Enqueue(
                        source,
                        target,
                        RoutingId.From("session-node"),
                        RoutingId.From("session-overflow"),
                        8,
                        0,
                        CreateHeader("forward-overflow"),
                        overflowBody,
                        forwardingLease))
                .WaitAsync(TimeSpan.FromSeconds(1)));
            Assert.Equal(ZLinkFrameworkErrorKind.ActorLocationStale, overflow.Kind);

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
                    9,
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
    public async Task EntrySpot_Timer_And_Route_Handler_Share_One_Serial_Line()
    {
        var probe = new EntryTimerSerialProbe();
        var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddTransient<BlockingProbeRouteHandler>()
            .AddTransient<EntryTimerProbeHandler>()
            .BuildServiceProvider();
        var spot = new CapturingSpot();
        var (activation, _) = CreateActivationWithRuntime(
            services,
            spot,
            typeof(TimerProbeEntrySpot));
        await using var cleanup = activation.ConfigureAwait(false);
        activation.Configure();

        var route = activation.DispatchRouteAsync(
            CreateRoutedReceived("hold"),
            CancellationToken.None).AsTask();
        await probe.RouteStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        await using var timer = await activation.AddTimer<EntryTimerProbeHandler>(
            "entry.serial",
            TimeSpan.FromMilliseconds(1));
        var early = await Task.WhenAny(
            probe.TimerStarted.Task,
            Task.Delay(TimeSpan.FromMilliseconds(50)));
        Assert.NotSame(probe.TimerStarted.Task, early);

        probe.ReleaseRoute.TrySetResult();
        await route.WaitAsync(TimeSpan.FromSeconds(5));
        await probe.TimerStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var events = probe.Events.ToArray();
        Assert.Equal(new[] { "route:start", "route:end" }, events[..2]);
        Assert.All(events[2..], entry => Assert.Equal("timer:start", entry));
    }

    [Fact]
    public async Task User_spot_concurrent_dispose_callers_wait_for_native_cleanup()
    {
        var services = new ServiceCollection().BuildServiceProvider();
        var registration = new ZLinkFrameworkRegistration();
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new ThrowingBackendAdapterFactory(),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));
        var cleanupStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseCleanup = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var nativeSpot = new CapturingSpot
        {
            DisposeHandler = async () =>
            {
                cleanupStarted.TrySetResult();
                await releaseCleanup.Task.ConfigureAwait(false);
            }
        };
        var activation = new ZLinkSpotActivation(
            runtime,
            services.CreateAsyncScope(),
            nativeSpot,
            RoutingId.From("node"),
            "node",
            "channel",
            TimeSpan.FromSeconds(1),
            TimeSpan.FromSeconds(1));
        activation.AttachSpot(new EmptyUserSpot(activation));

        var first = activation.DisposeAsync().AsTask();
        await cleanupStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var second = activation.DisposeAsync().AsTask();
        Assert.False(first.IsCompleted);
        Assert.False(second.IsCompleted);

        releaseCleanup.TrySetResult();
        await Task.WhenAll(first, second).WaitAsync(TimeSpan.FromSeconds(5));
        await activation.DisposeAsync();
    }

    [Fact]
    public async Task Entry_spot_concurrent_dispose_callers_wait_for_scope_cleanup_failure()
    {
        var cleanup = new BlockingScopeCleanup();
        var services = new ServiceCollection()
            .AddSingleton(cleanup)
            .AddScoped<BlockingScopeDependency>()
            .BuildServiceProvider();
        var (activation, _) = CreateActivationWithRuntime(
            services,
            new CapturingSpot(),
            typeof(ScopeCleanupEntrySpot));

        var first = activation.DisposeAsync().AsTask();
        await cleanup.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var second = activation.DisposeAsync().AsTask();
        Assert.False(first.IsCompleted);
        Assert.False(second.IsCompleted);

        cleanup.Release.TrySetResult();
        await Assert.ThrowsAsync<InvalidOperationException>(() => first);
        await Assert.ThrowsAsync<InvalidOperationException>(() => second);
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => activation.DisposeAsync().AsTask());
        Assert.Equal(1, cleanup.DisposeCount);
    }

    [Fact]
    public async Task Entry_spot_concurrent_dispose_callers_wait_for_blocked_serial_handler()
    {
        var probe = new BlockingDisposeRouteProbe();
        var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddTransient<BlockingDisposeRouteHandler>()
            .BuildServiceProvider();
        var (activation, _) = CreateActivationWithRuntime(
            services,
            new CapturingSpot(),
            typeof(BlockingDisposeEntrySpot));
        activation.Configure();

        var dispatch = activation.DispatchRouteAsync(
            CreateRoutedReceived("blocked-dispose"),
            CancellationToken.None).AsTask();
        await probe.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var first = activation.DisposeAsync().AsTask();
        var second = activation.DisposeAsync().AsTask();
        Assert.False(first.IsCompleted);
        Assert.False(second.IsCompleted);

        probe.Release.TrySetResult();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => dispatch.WaitAsync(TimeSpan.FromSeconds(5)));
        await Task.WhenAll(first, second).WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task Spot_Node_And_Catalog_Repeated_Dispose_Callers_Share_Finalization()
    {
        var cleanupStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseCleanup = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var node = new CapturingSpotNode
        {
            DisposeHandler = async () =>
            {
                cleanupStarted.TrySetResult();
                await releaseCleanup.Task.ConfigureAwait(false);
            }
        };
        var (runtime, _) = await CreateStartedRuntimeAsync(node);
        try
        {
            var state = GetPrivateField<ZLinkFrameworkRuntimeState>(runtime, "_state");
            var target = state.SpotNodes["entry"];
            var catalog = GetPrivateField<ZLinkSpotNodeCatalog>(target, "_spots");

            var firstCatalog = catalog.DisposeAsync().AsTask();
            var secondCatalog = catalog.DisposeAsync().AsTask();
            Assert.Same(firstCatalog, secondCatalog);
            await Task.WhenAll(firstCatalog, secondCatalog).WaitAsync(TimeSpan.FromSeconds(5));

            var firstNode = target.DisposeAsync().AsTask();
            await cleanupStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
            var secondNode = target.DisposeAsync().AsTask();
            Assert.Same(firstNode, secondNode);
            Assert.False(secondNode.IsCompleted);

            releaseCleanup.TrySetResult();
            await Task.WhenAll(firstNode, secondNode).WaitAsync(TimeSpan.FromSeconds(5));
        }
        finally
        {
            releaseCleanup.TrySetResult();
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Spot_catalog_dispose_closes_creation_admission_and_waits_for_accepted_create()
    {
        var probe = new BlockingSpotCreateProbe();
        var node = new CapturingSpotNode();
        var (runtime, _) = await CreateStartedRuntimeAsync(
            node,
            userSpotType: typeof(BlockingCreateSpot),
            blockingCreateProbe: probe);
        try
        {
            var creation = runtime.CreateAsync<BlockingCreateSpot>().AsTask();
            await probe.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));
            var state = GetPrivateField<ZLinkFrameworkRuntimeState>(runtime, "_state");
            var catalog = GetPrivateField<ZLinkSpotNodeCatalog>(state.SpotNodes["entry"], "_spots");

            var firstDispose = catalog.DisposeAsync().AsTask();
            var secondDispose = catalog.DisposeAsync().AsTask();
            Assert.Same(firstDispose, secondDispose);
            Assert.False(firstDispose.IsCompleted);
            await Assert.ThrowsAsync<ObjectDisposedException>(async () =>
                await runtime.CreateAsync<BlockingCreateSpot>());
            await Assert.ThrowsAsync<ObjectDisposedException>(async () =>
                await runtime.GetOrCreateAsync<BlockingCreateSpot>(RoutingId.From("post-close")));

            probe.Release.TrySetResult();
            _ = await creation.WaitAsync(TimeSpan.FromSeconds(5));
            await Task.WhenAll(firstDispose, secondDispose).WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Single(node.CreatedSpots);
            Assert.Equal(1, node.CreatedSpots[0].DisposeCount);
        }
        finally
        {
            probe.Release.TrySetResult();
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task CreateAsync_And_CloseAsync_Keep_UserSpotGauge_Balanced()
    {
        var deltas = new List<long>();
        using var listener = new MeterListener
        {
            InstrumentPublished = (instrument, owner) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework
                    && instrument.Name == "zlink.spot.count")
                    owner.EnableMeasurementEvents(instrument);
            }
        };
        listener.SetMeasurementEventCallback<long>((_, value, tags, _) =>
        {
            foreach (var tag in tags)
                if (tag.Key == "kind" && Equals(tag.Value, "user"))
                    deltas.Add(value);
        });
        listener.Start();

        var node = new CapturingSpotNode();
        var (runtime, _) = await CreateStartedRuntimeAsync(
            node,
            userSpotType: typeof(EmptyUserSpot));
        try
        {
            var created = await runtime.CreateAsync<EmptyUserSpot>();

            Assert.Equal(1, deltas.Sum());
            Assert.True(await runtime.CloseAsync(created.SpotRid));
            Assert.Equal(0, deltas.Sum());
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task CloseAsync_DoesNotCloseSpotWhenConcurrentJoinCommitsFirst()
    {
        var probe = new BlockingActorJoinProbe();
        var node = new CapturingSpotNode();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(
            node,
            userSpotType: typeof(BlockingActorJoinSpot),
            blockingActorJoinProbe: probe);
        try
        {
            var actor = Assert.IsType<ProbeActor>(
                (await runtime.CreateActorAsync(actorRef.ActorId, "probe")).Actor);
            var created = await runtime.CreateAsync<BlockingActorJoinSpot>();
            var state = GetPrivateField<ZLinkFrameworkRuntimeState>(runtime, "_state");
            var catalog = GetPrivateField<ZLinkSpotNodeCatalog>(state.SpotNodes["entry"], "_spots");
            var activations = GetPrivateField<Dictionary<RoutingId, ZLinkSpotActivation>>(catalog, "_spots");
            var activation = activations[created.SpotRid];

            var join = activation.JoinActorAsync(
                    actor,
                    ZLinkMessage.Empty,
                    CancellationToken.None)
                .AsTask();
            await probe.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));

            var close = runtime.CloseAsync(created.SpotRid).AsTask();
            Assert.False(close.IsCompleted);

            probe.Release.TrySetResult();
            Assert.True((await join.WaitAsync(TimeSpan.FromSeconds(5))).Accepted);
            Assert.False(await close.WaitAsync(TimeSpan.FromSeconds(5)));
            Assert.NotNull(await catalog.GetAsync(created.SpotRid, CancellationToken.None));
        }
        finally
        {
            probe.Release.TrySetResult();
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Spot_catalog_dispose_waits_for_accepted_get_or_create_and_shares_cleanup_failure()
    {
        var probe = new BlockingSpotCreateProbe();
        var cleanupFailure = new InvalidOperationException("user spot cleanup failed");
        var node = new CapturingSpotNode
        {
            CreatedSpotFactory = () => new CapturingSpot
            {
                DisposeHandler = () => ValueTask.FromException(cleanupFailure)
            }
        };
        var (runtime, _) = await CreateStartedRuntimeAsync(
            node,
            userSpotType: typeof(BlockingCreateSpot),
            blockingCreateProbe: probe);
        try
        {
            var creation = runtime.GetOrCreateAsync<BlockingCreateSpot>(RoutingId.From("blocked-create")).AsTask();
            await probe.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));
            var state = GetPrivateField<ZLinkFrameworkRuntimeState>(runtime, "_state");
            var catalog = GetPrivateField<ZLinkSpotNodeCatalog>(state.SpotNodes["entry"], "_spots");
            var firstDispose = catalog.DisposeAsync().AsTask();
            var secondDispose = catalog.DisposeAsync().AsTask();
            Assert.Same(firstDispose, secondDispose);
            Assert.False(firstDispose.IsCompleted);
            await Assert.ThrowsAsync<ObjectDisposedException>(async () =>
                await runtime.GetOrCreateAsync<BlockingCreateSpot>(RoutingId.From("blocked-create")));

            probe.Release.TrySetResult();
            _ = await creation.WaitAsync(TimeSpan.FromSeconds(5));
            var firstFailure = await Assert.ThrowsAsync<InvalidOperationException>(() => firstDispose);
            var secondFailure = await Assert.ThrowsAsync<InvalidOperationException>(() => secondDispose);
            Assert.Same(cleanupFailure, firstFailure);
            Assert.Same(cleanupFailure, secondFailure);
            Assert.Same(firstDispose, catalog.DisposeAsync().AsTask());
            Assert.Single(node.CreatedSpots);
            Assert.Equal(1, node.CreatedSpots[0].DisposeCount);
        }
        finally
        {
            probe.Release.TrySetResult();
            await Assert.ThrowsAsync<InvalidOperationException>(async () =>
                await runtime.StopAsync(CancellationToken.None));
        }
    }

    [Fact]
    public async Task GetOrCreateAsync_CallerCancellationOnlyStopsThatCallersWait()
    {
        var probe = new BlockingSpotCreateProbe();
        var node = new CapturingSpotNode();
        var spotRid = RoutingId.From("shared-create-cancellation");
        var (runtime, _) = await CreateStartedRuntimeAsync(
            node,
            userSpotType: typeof(BlockingCreateSpot),
            blockingCreateProbe: probe);
        using var ownerCancellation = new CancellationTokenSource();
        try
        {
            var owner = runtime.GetOrCreateAsync<BlockingCreateSpot>(
                    spotRid,
                    ZLinkMessage.Empty,
                    ownerCancellation.Token)
                .AsTask();
            await probe.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));

            var waiter = runtime.GetOrCreateAsync<BlockingCreateSpot>(spotRid).AsTask();
            ownerCancellation.Cancel();

            await Assert.ThrowsAnyAsync<OperationCanceledException>(
                () => owner.WaitAsync(TimeSpan.FromSeconds(5)));
            Assert.False(waiter.IsCompleted);

            probe.Release.TrySetResult();
            var result = await waiter.WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Equal(spotRid, result.SpotRid);
            Assert.Equal(ZLinkSpotCreateState.Existing, result.State);
            Assert.Single(node.CreatedSpots);
        }
        finally
        {
            probe.Release.TrySetResult();
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Current_Spot_Publish_Emits_Sent_With_Spot_Rid_And_Current_Flow()
    {
        var root = Path.Combine(Path.GetTempPath(), $"zlink-current-publish-{Guid.NewGuid():N}");
        var logPath = Path.Combine(root, "flow.log");
        var node = new CapturingSpotNode();
        node.EntrySpotBackend.PublishAccepted = true;
        var (runtime, _) = await CreateStartedRuntimeAsync(node);
        runtime.Registration.DispatchOptions.MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions);
        runtime.Registration.DispatchOptions.TraceLogFile(logPath);

        try
        {
            var state = await runtime.GetStartedStateForRoutingAsync(CancellationToken.None);
            var activation = Assert.IsType<ZLinkEntrySpotActivation>(state.SpotNodes["entry"].EntrySpotActivation);
            _ = await activation.Outbound
                .Publish("events", new ProbeRouteMessage("published"))
                .SubmitAsync();

            var header = Assert.IsType<ZLinkEnvelopeHeader>(node.EntrySpotBackend.PublishedHeader);
            Assert.Null(header.CorrelationId);
            Assert.True(ZlinkStreamFlowId.IsValid(header.FlowId));
            Assert.Equal(ZLinkFlowOrigin.Application, header.FlowOrigin);
            var line = Assert.Single(File.ReadAllLines(logPath));
            Assert.Contains("phase=sent", line, StringComparison.Ordinal);
            Assert.Contains("surface=SpotSubscription", line, StringComparison.Ordinal);
            Assert.Contains($"spot={activation.SpotRid}", line, StringComparison.Ordinal);
            Assert.DoesNotContain("corr=", line, StringComparison.Ordinal);
            Assert.Contains($"flow={header.FlowId}", line, StringComparison.Ordinal);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
            if (Directory.Exists(root)) Directory.Delete(root, true);
        }
    }

    [Fact]
    public async Task External_Spot_Publish_Emits_Internal_Publisher_Rid_Without_Correlation()
    {
        var root = Path.Combine(Path.GetTempPath(), $"zlink-external-publish-{Guid.NewGuid():N}");
        var logPath = Path.Combine(root, "flow.log");
        var node = new CapturingSpotNode();
        var (runtime, _) = await CreateStartedRuntimeAsync(node, includePubSub: true);
        runtime.Registration.DispatchOptions.MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions);
        runtime.Registration.DispatchOptions.TraceLogFile(logPath);

        try
        {
            _ = await new ZLinkSpotPublisherClientService(runtime)
                .PublishSpot("entry", "events", new ProbeRouteMessage("published"))
                .SubmitAsync();

            var publisher = Assert.Single(node.CreatedSpots);
            var header = Assert.IsType<ZLinkEnvelopeHeader>(publisher.PublishedHeader);
            Assert.Null(header.CorrelationId);
            Assert.True(ZlinkStreamFlowId.IsValid(header.FlowId));
            Assert.Equal(ZLinkFlowOrigin.Application, header.FlowOrigin);
            var line = Assert.Single(File.ReadAllLines(logPath));
            Assert.Contains("phase=sent", line, StringComparison.Ordinal);
            Assert.Contains($"spot={node.PublisherRoutingId}", line, StringComparison.Ordinal);
            Assert.DoesNotContain("corr=", line, StringComparison.Ordinal);
            Assert.Contains($"flow={header.FlowId}", line, StringComparison.Ordinal);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
            if (Directory.Exists(root)) Directory.Delete(root, true);
        }
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
    public async Task BoundSession_Submit_Rejects_Missing_Binding_On_The_Caller()
    {
        var node = new CapturingSpotNode();
        var (runtime, _) = await CreateStartedRuntimeAsync(node);
        try
        {
            var boundSession = new ZLinkBoundSessionService(runtime).Create("missing-actor");

            var exception = Assert.Throws<ZLinkFrameworkException>(() =>
                boundSession.Send(new ProbeRouteMessage("push")).Submit());

            Assert.Equal(ZLinkFrameworkErrorKind.ActorSessionNotBound, exception.Kind);
            Assert.Empty(node.BoundSessionReplies);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Actor_Send_Submit_Rejects_Nonblocking_Transport_Failure_On_The_Caller()
    {
        var node = new CapturingSpotNode();
        var (runtime, actor) = await CreateStartedRuntimeAsync(node);
        try
        {
            var client = new ZLinkActorClient(runtime);
            var publicActor = new ActorRef(actor.NodeRid, actor.ActorId, actor.Generation);

            var exception = Assert.Throws<ZLinkFrameworkException>(() =>
                client.SendToActor(publicActor, new ProbeRouteMessage("send")).Submit());

            Assert.Equal(ZLinkFrameworkErrorKind.RouteNotConnected, exception.Kind);
            Assert.Equal(SendFlags.DontWait, node.LastActorSendFlags);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Actor_Send_Uses_A_Router_Only_Spot_Node()
    {
        var node = new CapturingSpotNode { ActorSendAccepted = true };
        var (runtime, actor) = await CreateStartedRuntimeAsync(node, includeActorFactory: false);
        try
        {
            var client = new ZLinkActorClient(runtime);
            var publicActor = new ActorRef(actor.NodeRid, actor.ActorId, actor.Generation);

            client.SendToActor(publicActor, new ProbeRouteMessage("send")).Submit();

            Assert.Single(node.ActorSends);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Actor_Send_Skips_A_PubSub_Only_Spot_Node_Registered_First()
    {
        var routedNode = new CapturingSpotNode { ActorSendAccepted = true };
        var pubSubNode = new CapturingSpotNode { ActorSendAccepted = true };
        var (runtime, actor) = await CreateStartedRuntimeAsync(
            routedNode,
            includeActorFactory: false,
            pubSubOnlyNode: pubSubNode);
        try
        {
            var client = new ZLinkActorClient(runtime);
            var publicActor = new ActorRef(actor.NodeRid, actor.ActorId, actor.Generation);

            client.SendToActor(publicActor, new ProbeRouteMessage("send")).Submit();

            Assert.Single(routedNode.ActorSends);
            Assert.Empty(pubSubNode.ActorSends);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Observer_Only_Actor_Send_Creates_One_Wire_Flow_And_Emits_Sent()
    {
        var node = new CapturingSpotNode { ActorSendAccepted = true };
        var observer = new CapturingMessageFlowObserver();
        var (runtime, actor) = await CreateStartedRuntimeAsync(
            node,
            observer,
            ZLinkMessageFlowLogMode.Off);
        try
        {
            Assert.Equal(
                ZLinkMessageFlowLogMode.Off,
                runtime.Registration.DispatchOptions.Diagnostics.EffectiveMessageFlow);
            Assert.True(runtime.Flow.CaptureEnabled);
            Assert.Null(ZLinkFlowContext.Current);
            var client = new ZLinkActorClient(runtime);
            var publicActor = new ActorRef(actor.NodeRid, actor.ActorId, actor.Generation);

            client.SendToActor(publicActor, new ProbeRouteMessage("send")).Submit();

            Assert.Null(ZLinkFlowContext.Current);
            var sentPacket = Assert.Single(node.ActorSends);
            var header = ZLinkStreamProtocolDefaults.DecodeHeader(sentPacket.Parts[0]);
            Assert.True(ZlinkStreamFlowId.IsValid(header.FlowId));
            Assert.Equal(ZlinkStreamFlowOrigin.Application, header.FlowOrigin);
            var sent = await observer.WaitAsync(TimeSpan.FromSeconds(2));
            Assert.Equal(ZLinkMessageFlowOutcome.Sent, sent.Outcome);
            Assert.Equal(ZLinkDispatchErrorSurface.SpotActor, sent.Surface);
            Assert.Equal(ZLinkDispatchMessageKind.ActorSend, sent.MessageKind);
            Assert.Equal(header.FlowId, sent.FlowId);
            Assert.Equal(ZLinkFlowOrigin.Application, sent.FlowOrigin);
            Assert.Equal(header.CorrelationId, sent.CorrelationId);
            Assert.Equal(actor.ActorId, sent.ActorId);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Actor_Client_First_Application_Request_Creates_One_Wire_Flow_And_Emits_Sent()
    {
        var node = new CapturingSpotNode
        {
            ActorRequestHandler = parts =>
            {
                var requestHeader = ZLinkStreamProtocolDefaults.DecodeHeader(parts[0].AsReadOnlyMemory());
                var responseHeader = requestHeader with
                {
                    Kind = ZlinkStreamMessageKind.Response,
                    Name = string.Empty
                };
                return
                [
                    Message.From(ZLinkStreamProtocolDefaults.EncodeHeader(responseHeader).Span),
                    Message.From(ZLinkEnvelopeCodec.EncodeJsonBytes(new ProbeReply("reply")))
                ];
            }
        };
        var observer = new CapturingMessageFlowObserver();
        var (runtime, actor) = await CreateStartedRuntimeAsync(node, observer);
        try
        {
            Assert.Null(ZLinkFlowContext.Current);
            var client = new ZLinkActorClient(runtime);
            var publicActor = new ActorRef(actor.NodeRid, actor.ActorId, actor.Generation);

            var reply = await client.RequestToActor(publicActor, new ProbeRouteMessage("request"))
                .Async<ProbeReply>();

            Assert.Equal("reply", reply.Value);
            Assert.Null(ZLinkFlowContext.Current);
            var sentPacket = Assert.Single(node.ActorRequests);
            var header = ZLinkStreamProtocolDefaults.DecodeHeader(sentPacket.Parts[0]);
            Assert.True(ZlinkStreamFlowId.IsValid(header.FlowId));
            Assert.Equal(ZlinkStreamFlowOrigin.Application, header.FlowOrigin);
            var sent = await observer.WaitAsync(TimeSpan.FromSeconds(2));
            Assert.Equal(ZLinkMessageFlowOutcome.Sent, sent.Outcome);
            Assert.Equal(ZLinkDispatchErrorSurface.SpotActor, sent.Surface);
            Assert.Equal(ZLinkDispatchMessageKind.ActorRequest, sent.MessageKind);
            Assert.Equal(header.FlowId, sent.FlowId);
            Assert.Equal(ZLinkFlowOrigin.Application, sent.FlowOrigin);
            Assert.Equal(header.CorrelationId, sent.CorrelationId);
            Assert.Equal(actor.ActorId, sent.ActorId);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Retained_BoundSession_First_Application_Send_Creates_One_Wire_Flow_And_Emits_Sent()
    {
        var node = new CapturingSpotNode();
        var observer = new CapturingMessageFlowObserver();
        var (runtime, actor) = await CreateStartedRuntimeAsync(node, observer);
        try
        {
            runtime.GetOrCreateActorState(actor.ActorId).BindNativeActorRef(actor);
            runtime.BindActorSession(
                actor.ActorId,
                RoutingId.From("session-node"),
                RoutingId.From("session-rid"),
                ZLinkActorBoundSessionBindingToken.Native(RoutingId.From("session-rid")));
            var retained = new ZLinkBoundSessionService(runtime).Create(actor.ActorId);
            Assert.Null(ZLinkFlowContext.Current);

            retained.Send(new ProbeRouteMessage("push")).Submit();

            Assert.Null(ZLinkFlowContext.Current);
            var boundPush = Assert.Single(node.BoundSessionReplies);
            var frame = Assert.Single(boundPush.Parts);
            var header = DecodeFrameHeader(frame);
            Assert.True(ZlinkStreamFlowId.IsValid(header.FlowId));
            Assert.Equal(ZlinkStreamFlowOrigin.Application, header.FlowOrigin);
            var sent = await observer.WaitAsync(TimeSpan.FromSeconds(2));
            Assert.Equal(ZLinkMessageFlowOutcome.Sent, sent.Outcome);
            Assert.Equal(ZLinkDispatchErrorSurface.StreamSession, sent.Surface);
            Assert.Equal(ZLinkDispatchMessageKind.Send, sent.MessageKind);
            Assert.Equal(header.FlowId, sent.FlowId);
            Assert.Equal(ZLinkFlowOrigin.Application, sent.FlowOrigin);
            Assert.Equal(header.CorrelationId, sent.CorrelationId);
            Assert.Equal(actor.ActorId, sent.ActorId);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task BoundSession_Backpressure_Queues_Until_Node_SendReady()
    {
        var node = new CapturingSpotNode { BoundSessionSendAccepted = false };
        var (runtime, actor) = await CreateStartedRuntimeAsync(node);
        try
        {
            runtime.GetOrCreateActorState(actor.ActorId).BindNativeActorRef(actor);
            runtime.BindActorSession(
                actor.ActorId,
                RoutingId.From("session-node"),
                RoutingId.From("session-rid"),
                ZLinkActorBoundSessionBindingToken.Native(RoutingId.From("session-rid")));
            var retained = new ZLinkBoundSessionService(runtime).Create(actor.ActorId);

            retained.Send(new ProbeRouteMessage("queued")).Submit();
            Assert.Empty(node.BoundSessionReplies);

            node.BoundSessionSendAccepted = true;
            node.SignalSendReady();

            Assert.True(SpinWait.SpinUntil(
                () => node.BoundSessionReplies.Count == 1,
                TimeSpan.FromSeconds(2)));
            Assert.True(node.BoundSessionSendAttempts >= 2);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Off_Host_Preserves_An_Inbound_Flow_On_The_Next_Actor_Outbound()
    {
        const string inboundFlowId = "0196f7c2-4cb4-7cc8-89d4-2d6aee6fca2d";
        var node = new CapturingSpotNode { ActorSendAccepted = true };
        var (runtime, actor) = await CreateStartedRuntimeAsync(
            node,
            messageFlowMode: ZLinkMessageFlowLogMode.Off);
        try
        {
            var client = new ZLinkActorClient(runtime);
            var publicActor = new ActorRef(actor.NodeRid, actor.ActorId, actor.Generation);
            using (ZLinkFlowContext.Enter(
                       inboundFlowId,
                       ZLinkFlowOrigin.Application,
                       createIfAbsent: false,
                       ZLinkFlowOrigin.Inbound))
                client.SendToActor(publicActor, new ProbeRouteMessage("relay")).Submit();

            Assert.Null(ZLinkFlowContext.Current);
            var sentPacket = Assert.Single(node.ActorSends);
            var header = ZLinkStreamProtocolDefaults.DecodeHeader(sentPacket.Parts[0]);
            Assert.Equal(inboundFlowId, header.FlowId);
            Assert.Equal(ZlinkStreamFlowOrigin.Application, header.FlowOrigin);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task Off_Host_Without_An_Ambient_Flow_Does_Not_Create_Actor_Wire_Flow()
    {
        var node = new CapturingSpotNode { ActorSendAccepted = true };
        var (runtime, actor) = await CreateStartedRuntimeAsync(
            node,
            messageFlowMode: ZLinkMessageFlowLogMode.Off);
        try
        {
            var client = new ZLinkActorClient(runtime);
            var publicActor = new ActorRef(actor.NodeRid, actor.ActorId, actor.Generation);

            Assert.Null(ZLinkFlowContext.Current);
            client.SendToActor(publicActor, new ProbeRouteMessage("off-no-flow")).Submit();

            Assert.Null(ZLinkFlowContext.Current);
            var sentPacket = Assert.Single(node.ActorSends);
            var header = ZLinkStreamProtocolDefaults.DecodeHeader(sentPacket.Parts[0]);
            Assert.Null(header.FlowId);
            Assert.Null(header.FlowOrigin);
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
    public async Task EntrySpotActorDispatch_BoundMissingHandler_RepliesWithAnErrorFrame()
    {
        var node = new CapturingSpotNode();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        try
        {
            RegisterProbeActor(runtime, actorRef);

            await DispatchEntryActorPartsAsync(
                runtime,
                CreateActorRequestParts(actorRef, "missing-handler", "missing", requestId: 0, flags: 0),
                CancellationToken.None);

            var boundReply = Assert.Single(node.BoundSessionReplies);
            var decoded = DecodeReplyFrame<ZLinkStreamWireError>(Assert.Single(boundReply.Parts));
            Assert.Equal(ZlinkStreamMessageKind.Error, decoded.Header.Kind);
            Assert.Equal(
                ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound.ToString(),
                decoded.Payload.Code);
            Assert.Contains("No Spot actor request handler", decoded.Payload.Message, StringComparison.Ordinal);
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

            var observed = await observer.WaitAsync(
                ZLinkMessageFlowOutcome.Error,
                TimeSpan.FromSeconds(2));
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
    public async Task EntrySpotActorDispatch_MalformedRequestPayload_ReportsPayloadDecodeFailure()
    {
        var node = new CapturingSpotNode();
        var observer = new CapturingMessageFlowObserver();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node, observer);
        try
        {
            RegisterProbeActor(runtime, actorRef);

            await DispatchEntryActorPartsAsync(
                runtime,
                CreateActorRequestParts(
                    actorRef,
                    "request",
                    "ignored",
                    requestId: 46,
                    flags: 1,
                    malformedPayload: true),
                CancellationToken.None);

            var reply = Assert.Single(node.NoBindReplies);
            var decoded = DecodeReplyFrame<ZLinkStreamWireError>(Assert.Single(reply.Parts));
            Assert.Equal(ZlinkStreamMessageKind.Error, decoded.Header.Kind);
            Assert.Equal(nameof(JsonException), decoded.Payload.Code);
            Assert.Empty(node.BoundSessionReplies);

            var observed = await observer.WaitAsync(
                ZLinkMessageFlowOutcome.Error,
                TimeSpan.FromSeconds(2));
            Assert.Equal(ZLinkDispatchErrorSurface.SpotActor, observed.Surface);
            Assert.Equal(ZLinkDispatchMessageKind.ActorRequest, observed.MessageKind);
            Assert.Equal(ZLinkDispatchErrorReason.PayloadDecodeFailed, observed.ErrorReason);
            Assert.Equal(ZLinkDispatchErrorAction.ReplyError, observed.ErrorAction);
            Assert.Equal("request", observed.PacketName);
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

    [Fact]
    public async Task LocalEntrySpotJoin_Rejection_DestroysTheNativeActorCreatedByThisAttempt()
    {
        var node = new CapturingSpotNode();
        ConfigureNotConnectedEntryJoin(node);
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);

            var result = await runtime.JoinActorEntrySpotAsync(
                RoutingId.From("entry-node"),
                actor,
                ZLinkMessage.Empty,
                CancellationToken.None);

            Assert.IsType<ZLinkActorJoinResult.Rejected>(result);
            Assert.Single(node.CreatedActors);
            Assert.Equal(node.CreatedActors, node.DestroyedActors);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task LocalEntrySpotJoin_MissingActivation_DoesNotCreateANativeActor()
    {
        var node = new CapturingSpotNode();
        ConfigureNotConnectedEntryJoin(node);
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        var state = GetPrivateField<ZLinkFrameworkRuntimeState>(runtime, "_state");
        var target = state.SpotNodes["entry"];
        var activation = GetPrivateField<ZLinkEntrySpotActivation>(target, "_entrySpotActivation");
        SetPrivateField<ZLinkEntrySpotActivation?>(target, "_entrySpotActivation", null);
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);

            var failure = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
                await runtime.JoinActorEntrySpotAsync(
                    RoutingId.From("entry-node"),
                    actor,
                    ZLinkMessage.Empty,
                    CancellationToken.None));

            Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, failure.Kind);
            Assert.Empty(node.CreatedActors);
            Assert.Empty(node.DestroyedActors);
        }
        finally
        {
            SetPrivateField(target, "_entrySpotActivation", activation);
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task LocalEntrySpotJoin_HandlerFailure_DestroysTheNativeActorBeforePropagating()
    {
        var node = new CapturingSpotNode();
        ConfigureNotConnectedEntryJoin(node);
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(
            node,
            entrySpotType: typeof(LocalJoinProbeEntrySpot));
        runtime.Services.GetRequiredService<LocalEntryJoinProbe>().Handler = _ =>
            ValueTask.FromException<ZLinkSpotActorJoinResult>(new InvalidOperationException("admission failed"));
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);

            var failure = await Assert.ThrowsAsync<InvalidOperationException>(async () =>
                await runtime.JoinActorEntrySpotAsync(
                    RoutingId.From("entry-node"),
                    actor,
                    ZLinkMessage.Empty,
                    CancellationToken.None));

            Assert.Equal("admission failed", failure.Message);
            Assert.Equal(node.CreatedActors, node.DestroyedActors);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task LocalEntrySpotJoin_HandlerCancellation_DestroysTheNativeActorBeforePropagating()
    {
        var node = new CapturingSpotNode();
        ConfigureNotConnectedEntryJoin(node);
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(
            node,
            entrySpotType: typeof(LocalJoinProbeEntrySpot));
        runtime.Services.GetRequiredService<LocalEntryJoinProbe>().Handler = _ =>
            ValueTask.FromCanceled<ZLinkSpotActorJoinResult>(new CancellationToken(canceled: true));
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);

            await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
                await runtime.JoinActorEntrySpotAsync(
                    RoutingId.From("entry-node"),
                    actor,
                    ZLinkMessage.Empty,
                    CancellationToken.None));

            Assert.Equal(node.CreatedActors, node.DestroyedActors);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task LocalEntrySpotJoin_CleanupFailure_UsesGenerationOwnedReconciliation()
    {
        var node = new CapturingSpotNode();
        ConfigureNotConnectedEntryJoin(node);
        var attempts = 0;
        node.DestroyHandler = (_, _) => Interlocked.Increment(ref attempts) == 1
            ? ValueTask.FromException(new InvalidOperationException("destroy failed"))
            : ValueTask.CompletedTask;
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);

            var failure = await Assert.ThrowsAsync<InvalidOperationException>(async () =>
                await runtime.JoinActorEntrySpotAsync(
                    RoutingId.From("entry-node"),
                    actor,
                    ZLinkMessage.Empty,
                    CancellationToken.None));

            Assert.Contains("quarantined", failure.Message, StringComparison.Ordinal);
            Assert.True(SpinWait.SpinUntil(() => Volatile.Read(ref attempts) >= 2, TimeSpan.FromSeconds(5)));
            Assert.Equal(2, attempts);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task LocalEntrySpotJoin_PreExistingNativeActor_IsNeverDestroyed()
    {
        var node = new CapturingSpotNode();
        ConfigureNotConnectedEntryJoin(node);
        var existing = new ZLinkBackendActorRef(RoutingId.From("entry-node"), "actor-a", 7);
        node.ActorLookupResult = existing;
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(
            node,
            entrySpotType: typeof(LocalJoinProbeEntrySpot));
        runtime.Services.GetRequiredService<LocalEntryJoinProbe>().Handler = _ =>
            ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);

            var result = await runtime.JoinActorEntrySpotAsync(
                RoutingId.From("entry-node"),
                actor,
                ZLinkMessage.Empty,
                CancellationToken.None);

            var accepted = Assert.IsType<ZLinkActorJoinResult.Accepted>(result);
            Assert.Equal(existing.ToNative(), accepted.Actor);
            Assert.Empty(node.CreatedActors);
            Assert.Empty(node.DestroyedActors);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task NativeEntrySpotJoin_LateReplyAfterCancellation_IsDisposed()
    {
        var node = new CapturingSpotNode { DeferEntrySpotJoinCallback = true };
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        using var cancellation = new CancellationTokenSource();
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);
            var join = runtime.JoinActorEntrySpotAsync(
                    RoutingId.From("remote-node"),
                    actor,
                    ZLinkMessage.Empty,
                    cancellation.Token)
                .AsTask();
            Assert.True(SpinWait.SpinUntil(
                () => node.DeferredEntrySpotJoinCallback is not null,
                TimeSpan.FromSeconds(5)));

            cancellation.Cancel();
            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => join);

            using var lateReply = Message.From("late-entry-reply");
            node.DeferredEntrySpotJoinCallback!(
                new ZLinkBackendActorJoinEntrySpotResult(
                    RequestResult.Ok,
                    0,
                    actorRef,
                    RoutingId.From("remote-node"),
                    RoutingId.From("entry-spot"),
                    0,
                    0),
                [lateReply]);
            Assert.Throws<ObjectDisposedException>(() => lateReply.AsReadOnlySpan());
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task NativeUserSpotJoin_LateReplyAfterCancellation_IsDisposed()
    {
        var node = new CapturingSpotNode { DeferActorJoinCallback = true };
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node, includeJoinTarget: true);
        using var cancellation = new CancellationTokenSource();
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);
            var target = await runtime.CreateAsync<JoinTargetSpot>();
            var state = GetPrivateField<ZLinkFrameworkRuntimeState>(runtime, "_state");
            var spots = GetPrivateField<ZLinkSpotRuntimeManager>(runtime, "_spots");
            var actorSessions = GetPrivateField<ZLinkActorSessionManager>(runtime, "_actorSessionManager");
            var joiner = new ZLinkActorRemoteJoiner(
                runtime,
                runtime.Registration,
                runtime.Services,
                spots,
                actorSessions);
            var join = joiner.JoinAsync(
                    state,
                    target.SpotRid,
                    actor,
                    actorRef,
                    node,
                    ZLinkMessage.Empty,
                    cancellation.Token)
                .AsTask();
            Assert.True(SpinWait.SpinUntil(
                () => node.DeferredActorJoinCallback is not null,
                TimeSpan.FromSeconds(5)));

            cancellation.Cancel();
            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => join);

            using var lateReply = Message.From("late-user-spot-reply");
            node.DeferredActorJoinCallback!(
                new ZLinkBackendActorJoinResult(
                    RequestResult.Ok,
                    0,
                    actorRef,
                    target.SpotRid,
                    0,
                    0),
                [lateReply]);
            Assert.Throws<ObjectDisposedException>(() => lateReply.AsReadOnlySpan());
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task NativeActorJoinCompletion_NormalWinnerRetainsReplyOwnershipForDecoder()
    {
        using var completion = new ZLinkNativeReplyCompletion<int>(CancellationToken.None);
        using var reply = Message.From("normal-reply");

        completion.Complete(7, [reply]);
        var result = await completion.Task;

        Assert.Equal(7, result.Result);
        Assert.Same(reply, Assert.Single(result.Reply));
        Assert.Equal("normal-reply", System.Text.Encoding.UTF8.GetString(reply.AsReadOnlySpan()));
    }

    [Fact]
    public async Task NativeUserSpotJoin_BackendThrow_DisposesRequestPartsAndPreservesException()
    {
        var failure = new InvalidOperationException("native join submit failed");
        var node = new CapturingSpotNode { ActorJoinSubmitFailure = failure };
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node, includeJoinTarget: true);
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);
            var target = await runtime.CreateAsync<JoinTargetSpot>();
            var state = GetPrivateField<ZLinkFrameworkRuntimeState>(runtime, "_state");
            var spots = GetPrivateField<ZLinkSpotRuntimeManager>(runtime, "_spots");
            var actorSessions = GetPrivateField<ZLinkActorSessionManager>(runtime, "_actorSessionManager");
            var joiner = new ZLinkActorRemoteJoiner(
                runtime,
                runtime.Registration,
                runtime.Services,
                spots,
                actorSessions);

            var thrown = await Assert.ThrowsAsync<InvalidOperationException>(async () =>
                await joiner.JoinAsync(
                    state,
                    target.SpotRid,
                    actor,
                    actorRef,
                    node,
                    ZLinkMessage.Empty,
                    CancellationToken.None));

            Assert.Same(failure, thrown);
            var submitted = Assert.IsAssignableFrom<IReadOnlyList<Message>>(node.ActorJoinSubmittedParts);
            Assert.NotEmpty(submitted);
            Assert.All(submitted, part =>
                Assert.Throws<ObjectDisposedException>(() => part.AsReadOnlySpan()));
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task ActorRemoteJoiner_Join_Records_Target_Node_As_SourceRid_Without_PeerRid()
    {
        var observer = new CapturingMessageFlowObserver();
        var node = new CapturingSpotNode();
        ZLinkEnvelopeHeader? requestHeader = null;
        ZLinkEnvelopeHeader? replyHeader = null;
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(
            node,
            observer,
            includeJoinTarget: true);
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);
            var created = await runtime.CreateAsync<JoinTargetSpot>();
            node.ActorJoinHandler = parts =>
            {
                var decodedRequestHeader = ZLinkEnvelopeCodec.DecodeHeader(parts);
                requestHeader = decodedRequestHeader;
                var targetActor = new ZLinkBackendActorRef(
                    RoutingId.From("joined-node"),
                    actor.ActorId,
                    actorRef.Generation + 1);
                var result = new ZLinkBackendActorJoinResult(
                    RequestResult.Ok,
                    0,
                    targetActor,
                    created.SpotRid,
                    1,
                    0);
                var reply = ZLinkSpotReplyEnvelope.EncodeActorJoinReplyParts(
                    "entry-channel",
                    decodedRequestHeader.MessageName,
                    ZLinkMessage.From("accepted"),
                    typeof(ZLinkMessage),
                    runtime.Registration.Codecs);
                replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply);
                return (result, reply);
            };
            var state = GetPrivateField<ZLinkFrameworkRuntimeState>(runtime, "_state");
            var spots = GetPrivateField<ZLinkSpotRuntimeManager>(runtime, "_spots");
            var actorSessions = GetPrivateField<ZLinkActorSessionManager>(runtime, "_actorSessionManager");
            var joiner = new ZLinkActorRemoteJoiner(
                runtime,
                runtime.Registration,
                runtime.Services,
                spots,
                actorSessions);

            Assert.Null(ZLinkFlowContext.Current);
            var result = await joiner.JoinAsync(
                state,
                created.SpotRid,
                actor,
                actorRef,
                node,
                ZLinkMessage.From("join"),
                CancellationToken.None);

            Assert.IsType<ZLinkActorJoinResult.Accepted>(result);
            Assert.Null(ZLinkFlowContext.Current);
            var capturedRequestHeader = Assert.IsType<ZLinkEnvelopeHeader>(requestHeader);
            var capturedReplyHeader = Assert.IsType<ZLinkEnvelopeHeader>(replyHeader);
            Assert.Equal(ZLinkMessageKind.Command, capturedRequestHeader.Kind);
            Assert.Equal(ZLinkMessageKind.Command, capturedReplyHeader.Kind);
            Assert.True(ZlinkStreamFlowId.IsValid(capturedRequestHeader.FlowId));
            Assert.Equal(ZLinkFlowOrigin.Application, capturedRequestHeader.FlowOrigin);
            Assert.Equal(capturedRequestHeader.FlowId, capturedReplyHeader.FlowId);
            Assert.Equal(capturedRequestHeader.FlowOrigin, capturedReplyHeader.FlowOrigin);
            Assert.Null(capturedRequestHeader.CorrelationId);
            Assert.Null(capturedReplyHeader.CorrelationId);
            for (var attempt = 0; attempt < 100
                                  && observer.Events.Count(flow => flow.Outcome is
                                      ZLinkMessageFlowOutcome.Sent or ZLinkMessageFlowOutcome.ReplyReceived) < 2;
                 attempt++)
                await Task.Delay(5);
            var flowEvents = observer.Events
                .Where(flow => flow.Outcome is
                    ZLinkMessageFlowOutcome.Sent or ZLinkMessageFlowOutcome.ReplyReceived)
                .ToArray();
            Assert.Equal(2, flowEvents.Length);
            Assert.Equal(
                [ZLinkMessageFlowOutcome.Sent, ZLinkMessageFlowOutcome.ReplyReceived],
                flowEvents.Select(flow => flow.Outcome));
            Assert.All(flowEvents, flow =>
            {
                Assert.Equal(node.LastActorJoinTargetNodeRid.ToString(), flow.SourceRid);
                Assert.Null(flow.PeerRid);
                Assert.Equal(created.SpotRid.ToString(), flow.SpotRid);
                Assert.Equal(actor.ActorId, flow.ActorId);
                Assert.Equal(capturedRequestHeader.FlowId, flow.FlowId);
                Assert.Equal(ZLinkFlowOrigin.Application, flow.FlowOrigin);
                Assert.Null(flow.CorrelationId);
            });
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task ActorJoinDispatcher_Preserves_Wire_Flow_Through_Handler_And_Reply()
    {
        const string flowId = "0196f7c2-4cb4-7cc8-89d4-2d6aee6fca2d";
        var node = new CapturingSpotNode();
        var (runtime, actorRef) = await CreateStartedRuntimeAsync(node);
        try
        {
            var actor = RegisterProbeActor(runtime, actorRef);
            var spot = new ActorJoinFlowProbeSpot();
            var actorJoins = new ZLinkSpotActorJoinRegistry();
            actorJoins.Bind(spot);
            var actors = new ZLinkSpotActorMembership();
            actors.Add(actor);
            await using var handlerInstances = new ZLinkScopedHandlerInstanceOwner(runtime.Services);
            var invoker = new ZLinkSpotHandlerInvoker(
                handlerInstances,
                spot,
                runtime.Registration.Codecs,
                ZLinkStreamProtocolDefaults.CreateLz4CompressionCodec());
            var nativeSpot = new CapturingSpot();
            var dispatcher = new ZLinkSpotActorJoinDispatcher(
                runtime,
                nativeSpot,
                "join-channel",
                actorJoins,
                actors,
                () => invoker);
            var requestHeader = new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Command,
                "join-channel",
                typeof(ZLinkMessage).Name,
                ZLinkEnvelopeCodec.DefaultContentType,
                null,
                null,
                null,
                null,
                null)
            {
                FlowId = flowId,
                FlowOrigin = ZLinkFlowOrigin.Application
            };
            var parts = ZLinkEnvelopeCodec.EncodeParts(
                requestHeader,
                ZLinkMessage.From("join-request"),
                typeof(ZLinkMessage),
                runtime.Registration.Codecs);
            var request = new ZLinkBackendActorJoinRequest(
                actorRef,
                actorRef,
                RoutingId.From("source-node"),
                RoutingId.From("target-spot"),
                1,
                parts[0],
                parts);

            Assert.Null(ZLinkFlowContext.Current);
            try
            {
                await dispatcher.DispatchAsync(request, CancellationToken.None);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }

            Assert.Null(ZLinkFlowContext.Current);
            Assert.Equal(flowId, spot.ObservedFlowId);
            Assert.Equal(ZLinkFlowOrigin.Application, spot.ObservedFlowOrigin);
            Assert.Equal("join-request", spot.ObservedRequest);
            Assert.Equal(0, nativeSpot.ActorJoinResultCode);
            var replyHeader = Assert.IsType<ZLinkEnvelopeHeader>(nativeSpot.ActorJoinReplyHeader);
            Assert.Equal(ZLinkMessageKind.Command, replyHeader.Kind);
            Assert.Equal(flowId, replyHeader.FlowId);
            Assert.Equal(ZLinkFlowOrigin.Application, replyHeader.FlowOrigin);
            Assert.Null(replyHeader.CorrelationId);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    private static T GetPrivateField<T>(object instance, string name)
    {
        var field = instance.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)
                    ?? throw new InvalidOperationException($"Private field '{name}' was not found.");
        return Assert.IsType<T>(field.GetValue(instance));
    }

    private static void SetPrivateField<T>(object instance, string name, T value)
    {
        var field = instance.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)
                    ?? throw new InvalidOperationException($"Private field '{name}' was not found.");
        field.SetValue(instance, value);
    }

    private static async ValueTask CreateTrackedActorOwnershipAsync(
        ZLinkActorOwnershipCoordinator ownership,
        string actorType,
        string actorId,
        RoutingId nodeRid,
        CancellationToken cancellationToken)
    {
        var activation = await ownership.ExecuteActorClaimThenActivateAsync(
            actorType,
            actorId,
            nodeRid,
            deactivate: null,
            activate: static _ => ValueTask.FromResult(new object()),
            cancellationToken: cancellationToken);
        Assert.NotNull(activation.Activated);
        Assert.Null(activation.ExistingLocation);
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
        IZLinkMessageFlowObserver? messageFlowObserver = null,
        ZLinkMessageFlowLogMode? messageFlowMode = null,
        bool includeJoinTarget = false,
        bool includePubSub = false,
        Type? entrySpotType = null,
        Type? userSpotType = null,
        BlockingSpotCreateProbe? blockingCreateProbe = null,
        BlockingActorJoinProbe? blockingActorJoinProbe = null,
        bool includeActorFactory = true,
        CapturingSpotNode? pubSubOnlyNode = null)
    {
        var services = new ServiceCollection()
            .AddSingleton<FlowJoinProbe>()
            .AddSingleton<LocalEntryJoinProbe>()
            .AddSingleton(blockingCreateProbe ?? new BlockingSpotCreateProbe())
            .AddSingleton(blockingActorJoinProbe ?? new BlockingActorJoinProbe())
            .AddTransient<ProbeActorFactory>()
            .AddTransient<ProbeActorRequestHandler>()
            .AddTransient<ProbeActorFlowJoinRequestHandler>()
            .AddTransient<ProbeActorDestroyRequestHandler>()
            .AddTransient<ProbeActorThrowingRequestHandler>()
            .BuildServiceProvider();
        var registration = new ZLinkFrameworkRegistration
        {
            DefaultRequestTimeout = TimeSpan.FromSeconds(1),
            ImplicitHandlerAutoRegistrationEnabled = false
        };
        if (messageFlowMode is { } mode)
            registration.DispatchOptions.MessageFlow(mode);
        if (messageFlowObserver is not null)
            registration.DispatchOptions.SetMessageFlowObserver(messageFlowObserver);
        if (pubSubOnlyNode is not null)
            registration.SpotNodes["pubsub"] = new ZLinkSpotNodeRegistration
            {
                SpotNodeName = "pubsub",
                RoutingId = RoutingId.From("pubsub-node"),
                PubSub = new ZLinkSpotPubSubCapabilityRegistration()
            };
        registration.SpotNodes["entry"] = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "entry",
            RoutingId = RoutingId.From("entry-node"),
            Router = new ZLinkSpotRouterCapabilityRegistration { BindEndpoint = "inproc://entry" },
            EntrySpotType = entrySpotType ?? typeof(ProbeEntrySpot),
        };
        if (includeActorFactory)
            registration.SpotNodes["entry"].ActorFactories["probe"] = typeof(ProbeActorFactory);
        if (includePubSub)
            registration.SpotNodes["entry"].PubSub = new ZLinkSpotPubSubCapabilityRegistration();
        if (includeJoinTarget)
            registration.SpotNodes["entry"].SpotFactories.Add(typeof(JoinTargetSpot));
        if (userSpotType is not null)
            registration.SpotNodes["entry"].SpotFactories.Add(userSpotType);
        registration.ActorCatalog.Build(registration.SpotNodes.Values);
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new CapturingBackendAdapterFactory(node, pubSubOnlyNode),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));
        await runtime.StartAsync(CancellationToken.None);
        return (runtime, new ZLinkBackendActorRef(RoutingId.From("actor-node"), "actor-a", 1));
    }

    private static void ConfigureNotConnectedEntryJoin(CapturingSpotNode node)
    {
        node.EntrySpotJoinResult = new ZLinkBackendActorJoinEntrySpotResult(
            RequestResult.NotConnected,
            0,
            new ZLinkBackendActorRef(RoutingId.From("actor-node"), "actor-a", 1),
            RoutingId.From("entry-node"),
            RoutingId.From("entry-spot"),
            0,
            0);
    }

    private static (ZLinkEntrySpotActivation Activation, ZLinkFrameworkRuntime Runtime) CreateActivationWithRuntime(
        IServiceProvider services,
        IZLinkBackendSpot spot,
        Type? entrySpotType = null)
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
            entrySpotType ?? typeof(ProbeEntrySpot),
            RoutingId.From("entry-node"),
            "entry",
            "entry-channel",
            TimeSpan.FromSeconds(5),
            new ZLinkSpotOutboundTransport(
                spot,
                TimeSpan.FromSeconds(1),
                CancellationToken.None));
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
        uint flags,
        bool malformedPayload = false)
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
                malformedPayload
                    ? Message.From("{")
                    : Message.From(ZLinkEnvelopeCodec.EncodeJsonBytes(value, typeof(string))),
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

    private static ZlinkStreamHeader DecodeFrameHeader(byte[] frame)
    {
        var headerSize = BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(0, 2));
        return ZLinkStreamProtocolDefaults.DecodeHeader(frame.AsMemory(6, headerSize));
    }

    // RouteMesh 10.0.0 delivers routed traffic to the entry spot dispatch pump as a
    // framework-owned ZLinkBackendRouteReceived (the node pump drains Core claims
    // into these records). The record owns the encoded parts and is disposed by the
    // dispatch path, so we mint fresh parts per call and hand them straight over.
    private static ZLinkBackendRouteReceived CreateRoutedReceived(string value)
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            "entry-channel",
            nameof(ProbeRouteMessage));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
            header,
            new ProbeRouteMessage(value),
            null);
        return new ZLinkBackendRouteReceived(
            parts,
            sourceNodeRid: RoutingId.From("route-source-node"),
            spotRid: RoutingId.From("route-receiver-spot"),
            requestSeq: null,
            reply: null);
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

    private sealed class ProbeActor(string actorId, IZLinkActorContext? context = null) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context => context ?? throw new NotSupportedException();
    }

    private sealed class ProbeActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new ProbeActor(actorId, context));
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
            Context.Handlers.AddHandler<ProbeActorFlowJoinRequestHandler>("flow-join");
            Context.Handlers.AddHandler<ProbeActorDestroyRequestHandler>("destroy-request");
            Context.Handlers.AddHandler<ProbeActorThrowingRequestHandler>("throw");
        }
    }

    private sealed class EmptyUserSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;
    }

    private sealed class BlockingCreateSpot(
        IZLinkSpotContext context,
        BlockingSpotCreateProbe probe) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public async ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            _ = request;
            _ = cancellationToken;
            probe.Started.TrySetResult();
            await probe.Release.Task.ConfigureAwait(false);
            return ZLinkSpotCreateResponse.Accept();
        }
    }

    private sealed class BlockingSpotCreateProbe
    {
        public TaskCompletionSource Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Release { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class BlockingActorJoinSpot(
        IZLinkSpotContext context,
        BlockingActorJoinProbe probe) : IZLinkSpot<ProbeActor>
    {
        public IZLinkSpotContext Context { get; } = context;

        public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            string actorId,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            _ = actorId;
            _ = request;
            _ = cancellationToken;
            probe.Started.TrySetResult();
            await probe.Release.Task.ConfigureAwait(false);
            return ZLinkSpotActorJoinResult.Accept();
        }

        public ValueTask OnJoinedActorAsync(ProbeActor actor, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;

        public ValueTask OnLeaveActorAsync(ProbeActor actor, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class BlockingActorJoinProbe
    {
        public TaskCompletionSource Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Release { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class ScopeCleanupEntrySpot(
        IZLinkEntrySpotContext context,
        BlockingScopeDependency dependency) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        private BlockingScopeDependency Dependency { get; } = dependency;
    }

    private sealed class BlockingDisposeEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddPacket<BlockingDisposeRouteHandler>();
        }
    }

    private sealed class BlockingDisposeRouteHandler(BlockingDisposeRouteProbe probe)
        : IZLinkSpotPacketHandler<BlockingDisposeEntrySpot, ProbeRouteMessage>
    {
        public async ValueTask HandleAsync(
            BlockingDisposeEntrySpot spot,
            ProbeRouteMessage message,
            CancellationToken cancellationToken)
        {
            probe.Started.TrySetResult();
            await probe.Release.Task.ConfigureAwait(false);
        }
    }

    private sealed class BlockingDisposeRouteProbe
    {
        public TaskCompletionSource Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Release { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class BlockingScopeDependency(BlockingScopeCleanup cleanup) : IAsyncDisposable
    {
        public async ValueTask DisposeAsync()
        {
            _ = Interlocked.Increment(ref cleanup.DisposeCount);
            cleanup.Started.TrySetResult();
            await cleanup.Release.Task.ConfigureAwait(false);
            throw new InvalidOperationException("scope cleanup failed");
        }
    }

    private sealed class BlockingScopeCleanup
    {
        public TaskCompletionSource Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Release { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int DisposeCount;
    }

    private sealed class LocalEntryJoinProbe
    {
        public Func<CancellationToken, ValueTask<ZLinkSpotActorJoinResult>> Handler { get; set; } =
            _ => ValueTask.FromResult(ZLinkSpotActorJoinResult.Reject());
    }

    private sealed class LocalJoinProbeEntrySpot(
        IZLinkEntrySpotContext context,
        LocalEntryJoinProbe probe) : IZLinkEntrySpot<ProbeActor>
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
        }

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            string actorId,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            _ = actorId;
            _ = request;
            return probe.Handler(cancellationToken);
        }

        public ValueTask OnJoinedActorAsync(ProbeActor actor, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;

        public ValueTask OnLeaveActorAsync(ProbeActor actor, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class JoinTargetSpot(
        IZLinkSpotContext context,
        FlowJoinProbe flowJoinProbe) : IZLinkSpot<ProbeActor>
    {
        public IZLinkSpotContext Context { get; } = context;

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            string actorId,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            _ = actorId;
            _ = request;
            _ = cancellationToken;
            flowJoinProbe.JoinFlow = ZLinkFlowContext.Current;
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
        }

        public ValueTask OnJoinedActorAsync(ProbeActor actor, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;

        public ValueTask OnLeaveActorAsync(ProbeActor actor, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class ActorJoinFlowProbeSpot : IZLinkSpot<ProbeActor>
    {
        public IZLinkSpotContext Context => throw new NotSupportedException();

        public string? ObservedFlowId { get; private set; }

        public ZLinkFlowOrigin? ObservedFlowOrigin { get; private set; }

        public string? ObservedRequest { get; private set; }

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            string actorId,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            _ = actorId;
            _ = cancellationToken;
            var flow = ZLinkFlowContext.Current;
            ObservedFlowId = flow?.FlowId;
            ObservedFlowOrigin = flow?.Origin;
            ObservedRequest = request.Decode<string>();
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept("join-reply"));
        }

        public ValueTask OnJoinedActorAsync(ProbeActor actor, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;

        public ValueTask OnLeaveActorAsync(ProbeActor actor, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class TimerProbeEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddPacket<BlockingProbeRouteHandler>();
        }
    }

    private sealed class EntryTimerSerialProbe
    {
        public ConcurrentQueue<string> Events { get; } = new();
        public TaskCompletionSource RouteStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        public TaskCompletionSource ReleaseRoute { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        public TaskCompletionSource TimerStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class BlockingProbeRouteHandler(EntryTimerSerialProbe probe)
        : IZLinkSpotPacketHandler<TimerProbeEntrySpot, ProbeRouteMessage>
    {
        public async ValueTask HandleAsync(
            TimerProbeEntrySpot spot,
            ProbeRouteMessage message,
            CancellationToken cancellationToken)
        {
            probe.Events.Enqueue("route:start");
            probe.RouteStarted.TrySetResult();
            await probe.ReleaseRoute.Task.WaitAsync(cancellationToken);
            probe.Events.Enqueue("route:end");
        }
    }

    private sealed class EntryTimerProbeHandler(EntryTimerSerialProbe probe)
        : IZLinkSpotTimerHandler<TimerProbeEntrySpot>
    {
        public ValueTask HandleAsync(
            TimerProbeEntrySpot spot,
            ZLinkTimerTick tick,
            CancellationToken cancellationToken)
        {
            probe.Events.Enqueue("timer:start");
            probe.TimerStarted.TrySetResult();
            return ValueTask.CompletedTask;
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

    private sealed class ProbeActorFlowJoinRequestHandler(FlowJoinProbe probe)
        : IZLinkEntrySpotActorRequestHandler<ProbeEntrySpot, ProbeActor, string, ProbeReply>
    {
        public async ValueTask<ProbeReply> HandleAsync(
            ProbeEntrySpot entrySpot,
            ProbeActor actor,
            ZLinkSpotActorRequestContext context,
            string request,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            var result = await actor.Context.JoinSpot(probe.TargetSpotRid, request)
                .Async(cancellationToken);
            Assert.IsType<ZLinkActorJoinResult.Accepted>(result);
            return new ProbeReply($"{request}:{actor.ActorId}");
        }
    }

    private sealed class FlowJoinProbe
    {
        public RoutingId TargetSpotRid { get; set; }

        public ZLinkFlowValue? JoinFlow { get; set; }
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

        public RoutingId RoutingId { get; private set; } = RoutingId.From("entry-spot");

        public Action<RoutingId>? RoutingIdChanged { get; set; }

        public Func<ValueTask>? DisposeHandler { get; set; }

        public bool PublishAccepted { get; set; }

        public int DisposeCount { get; private set; }

        public ZLinkEnvelopeHeader? PublishedHeader { get; private set; }

        public int? ActorJoinResultCode { get; private set; }

        public ZLinkEnvelopeHeader? ActorJoinReplyHeader { get; private set; }

        public ValueTask DisposeAsync()
        {
            DisposeCount++;
            return DisposeHandler is { } handler ? handler() : ValueTask.CompletedTask;
        }

        public void SetRoutingId(RoutingId routingId)
        {
            RoutingId = routingId;
            RoutingIdChanged?.Invoke(routingId);
        }

        public void SetSubscription(string topic) { }

        public ZLinkBackendSubscribeMessage? Subscribe(RecvFlags flags) => null;

        public ZLinkBackendRouteReceived? RecvRoute(RecvFlags flags) => null;

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
            TimeSpan? timeout,
            ReadOnlyMemory<byte> metadata) => false;

        public bool RequestToChannel(
            string channelName,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout,
            ReadOnlyMemory<byte> metadata) => false;

        public SubmitResult SendToChannel(
            string channelName, Message message, SendFlags flags,
            ReadOnlyMemory<byte> metadata) => SubmitResult.Backpressured;

        public SubmitResult SendToChannel(
            string channelName, IReadOnlyList<Message> parts, SendFlags flags,
            ReadOnlyMemory<byte> metadata) => SubmitResult.Backpressured;

        public MeshPublishDetail Publish(
            string topic, Message message, SendFlags flags,
            ReadOnlyMemory<byte> metadata)
        {
            _ = topic;
            _ = flags;
            PublishedHeader = ZLinkEnvelopeCodec.DecodeHeader(message);
            return PublishAccepted
                ? new MeshPublishDetail(0, 0, 0, 0, 1, 1, 0)
                : throw new ZlinkSubmitException(
                    ZlinkSubmitException.ErrorCode.Backpressured);
        }

        public MeshPublishDetail Publish(
            string topic, IReadOnlyList<Message> parts, SendFlags flags,
            ReadOnlyMemory<byte> metadata)
        {
            _ = topic;
            _ = flags;
            PublishedHeader = ZLinkEnvelopeCodec.DecodeHeader(parts);
            return PublishAccepted
                ? new MeshPublishDetail(0, 0, 0, 0, 1, 1, 0)
                : throw new ZlinkSubmitException(
                    ZlinkSubmitException.ErrorCode.Backpressured);
        }

        public SubmitResult SendToSpot(
            RoutingId targetRid, RoutingId targetSpotRid, ulong spotGeneration,
            Message message, SendFlags flags, ReadOnlyMemory<byte> metadata)
            => SubmitResult.Backpressured;

        public SubmitResult SendToSpot(
            RoutingId targetRid,
            RoutingId targetSpotRid,
            ulong spotGeneration,
            IReadOnlyList<Message> parts,
            SendFlags flags,
            ReadOnlyMemory<byte> metadata) => SubmitResult.Backpressured;

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId targetSpotRid,
            ulong spotGeneration,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout,
            ReadOnlyMemory<byte> metadata) => false;

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId targetSpotRid,
            ulong spotGeneration,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout,
            ReadOnlyMemory<byte> metadata) => false;

        public ZLinkBackendActorJoinRequest? RecvActorJoin(RecvFlags flags) => null;

        public ZLinkBackendSpotActorLifecycleEvent? RecvActorLifecycle(RecvFlags flags) => null;

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            Message reply)
        {
            _ = request;
            ActorJoinResultCode = joinResultCode;
            ActorJoinReplyHeader = null;
        }

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            IReadOnlyList<Message> parts)
        {
            _ = request;
            ActorJoinResultCode = joinResultCode;
            ActorJoinReplyHeader = ZLinkEnvelopeCodec.DecodeHeader(parts);
        }

        public void OnActorLifecycle(
            Action<ZLinkBackendSpotActorLifecycleInfo>? onJoin,
            Action<ZLinkBackendSpotActorLifecycleInfo>? onLeave) { }
    }

    private sealed class CapturingSpotNode : IZLinkBackendSpotNode
    {
        private readonly CapturingSpot _entrySpot = new();

        public CapturingSpotNode()
        {
            _entrySpot.RoutingIdChanged = routingId =>
                InitializationEvents.Enqueue($"entry-rid:{routingId}");
        }

        public ConcurrentQueue<string> InitializationEvents { get; } = new();

        public RoutingId EntryRoutingId => _entrySpot.RoutingId;

        public RoutingId PublisherRoutingId { get; private set; }

        public IZLinkSpotPublisherConfig? PublisherConfig { get; private set; }

        public IZLinkSpotSubscriberConfig? SubscriberConfig { get; private set; }

        public CapturingSpot EntrySpotBackend => _entrySpot;

        public List<CapturingSpot> CreatedSpots { get; } = [];

        public Func<CapturingSpot>? CreatedSpotFactory { get; set; }

        public Func<ValueTask>? DisposeHandler
        {
            get => _entrySpot.DisposeHandler;
            set => _entrySpot.DisposeHandler = value;
        }

        public List<CapturedActorReply> NoBindReplies { get; } = [];

        public List<(ZLinkBackendActorRef Actor, IReadOnlyList<byte[]> Parts)> BoundSessionReplies { get; } = [];

        public bool BoundSessionSendAccepted { get; set; } = true;

        public int BoundSessionSendAttempts { get; private set; }

        private Action? SendReadyHandler { get; set; }

        public void SignalSendReady() => SendReadyHandler?.Invoke();

        public ZLinkBackendActorJoinEntrySpotResult? EntrySpotJoinResult { get; set; }

        public IReadOnlyList<Message> EntrySpotJoinReplyParts { get; set; } = [];

        public Func<IReadOnlyList<Message>, (ZLinkBackendActorJoinResult Result, IReadOnlyList<Message> Reply)>?
            ActorJoinHandler { get; set; }

        public bool DeferActorJoinCallback { get; set; }

        public ActorJoinCallback? DeferredActorJoinCallback { get; private set; }

        public Exception? ActorJoinSubmitFailure { get; set; }

        public IReadOnlyList<Message>? ActorJoinSubmittedParts { get; private set; }

        public bool DeferEntrySpotJoinCallback { get; set; }

        public ActorJoinEntrySpotCallback? DeferredEntrySpotJoinCallback { get; private set; }

        public RoutingId LastActorJoinTargetNodeRid { get; private set; }

        public RoutingId LastActorJoinTargetSpotRid { get; private set; }

        public SendFlags LastActorSendFlags { get; private set; }

        public bool ActorSendAccepted { get; set; }

        public List<(ZLinkBackendActorRef Actor, IReadOnlyList<byte[]> Parts)> ActorSends { get; } = [];

        public List<(ZLinkBackendActorRef Actor, IReadOnlyList<byte[]> Parts)> ActorRequests { get; } = [];

        public Func<IReadOnlyList<Message>, IReadOnlyList<Message>>? ActorRequestHandler { get; set; }

        public List<ZLinkBackendActorRef> DestroyedActors { get; } = [];

        public ConcurrentQueue<string> LifecycleEvents { get; } = new();

        public List<ZLinkBackendActorRef> CreatedActors { get; } = [];

        public ZLinkBackendActorRef? ActorLookupResult { get; set; }

        public List<RoutingId> CreatedActorEntryRids { get; } = [];

        public Action<ZLinkBackendActorRef>? BeforeDestroy { get; set; }

        public Action<ZLinkBackendActorRef>? BeforeNoBindReply { get; set; }

        public Exception? DestroyFailure { get; set; }

        public Func<ZLinkBackendActorRef, CancellationToken, ValueTask>? DestroyHandler { get; set; }

        public ConcurrentQueue<bool> ForwardResults { get; } = new();

        public List<(bool HasMore, byte[] Payload)> ForwardedParts { get; } = [];

        public TaskCompletionSource FinalPartAttempted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public RoutingId RoutingId { get; private set; }

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void SetRoutingId(RoutingId routingId)
        {
            RoutingId = routingId;
            InitializationEvents.Enqueue($"node-rid:{routingId}");
        }

        public void SetPublisherRoutingId(RoutingId routingId)
        {
            PublisherRoutingId = routingId;
        }

        public void SetSubscriberRoutingId(RoutingId routingId) { }

        public void SetRouterBind(string endpoint)
        {
            InitializationEvents.Enqueue($"router-bind:{endpoint}");
        }

        public void SetPubBind(string endpoint) { }

        public void ApplyRoleConfig(
            IZLinkSpotPublisherConfig? publisher,
            IZLinkSpotSubscriberConfig? subscriber)
        {
            PublisherConfig = publisher;
            SubscriberConfig = subscriber;
        }

        public void OnSendReady(Action handler) => SendReadyHandler = handler;

        public void ConnectPeer(string endpoint) { }

        public void ConnectPeer(RoutingId peerRid, string endpoint) { }

        public void DisconnectPeer(string endpoint) { }

        public IZLinkBackendSpot CreateSpot()
        {
            var spot = CreatedSpotFactory?.Invoke() ?? new CapturingSpot();
            spot.SetRoutingId(PublisherRoutingId);
            CreatedSpots.Add(spot);
            return spot;
        }

        public IZLinkBackendSpot GetOrCreateSpot(RoutingId targetSpotRid, out bool created)
        {
            created = true;
            var spot = CreatedSpotFactory?.Invoke() ?? new CapturingSpot();
            spot.SetRoutingId(targetSpotRid);
            CreatedSpots.Add(spot);
            return spot;
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

        public List<string> AddedChannels { get; } = [];

        public Dictionary<string, uint> ChannelWeights { get; } = new();

        public int StartCount { get; private set; }

        public Action<ZLinkBackendRouteReceived>? NodeRouteHandler { get; private set; }

        public Action<ZLinkBackendActorTransferControl>? TransferControlHandler { get; private set; }

        public void AddChannel(string channelName) => AddedChannels.Add(channelName);

        public void SetChannelWeight(string channelName, uint weight) =>
            ChannelWeights[channelName] = weight;

        public void Start() => StartCount++;

        public ZLinkBackendActorTransferToken PrepareActorTransfer(
            ZLinkBackendActorTransferPrepare prepare,
            out ZLinkBackendActorTransferPrepareResult result,
            TimeSpan timeout) => throw new NotSupportedException();

        public void CommitActorTransfer(
            ZLinkBackendActorTransferToken token,
            ulong newMembershipEpoch) => throw new NotSupportedException();

        public void ActivateActorTransfer(ZLinkBackendActorTransferToken token) =>
            throw new NotSupportedException();

        public void AbortActorTransfer(ZLinkBackendActorTransferToken token) =>
            throw new NotSupportedException();

        public void OnTransferControl(Action<ZLinkBackendActorTransferControl> handler) =>
            TransferControlHandler = handler;

        public void OnNodeRoute(Action<ZLinkBackendRouteReceived> handler) =>
            NodeRouteHandler = handler;

        public IZLinkBackendSpot EntrySpot()
        {
            InitializationEvents.Enqueue("entry-facade");
            return _entrySpot;
        }

        public ZLinkBackendActorRef CreateActor(string actorId, Message createRequest)
        {
            var actor = new ZLinkBackendActorRef(RoutingId, actorId, 1);
            CreatedActors.Add(actor);
            CreatedActorEntryRids.Add(_entrySpot.RoutingId);
            return actor;
        }

        public ZLinkBackendActorRef? ActorLookup(string actorId) => ActorLookupResult;

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
            TimeSpan? timeout)
        {
            _ = actor;
            _ = timeout;
            ActorJoinSubmittedParts = parts;
            if (ActorJoinSubmitFailure is { } failure) throw failure;
            if (DeferActorJoinCallback)
            {
                LastActorJoinTargetNodeRid = destNodeRid;
                LastActorJoinTargetSpotRid = destSpotRid;
                DeferredActorJoinCallback = callback;
                return true;
            }
            if (ActorJoinHandler is null) return false;
            LastActorJoinTargetNodeRid = destNodeRid;
            LastActorJoinTargetSpotRid = destSpotRid;
            var (result, reply) = ActorJoinHandler(parts);
            callback(result, reply);
            return true;
        }

        public bool JoinActorEntrySpot(
            ZLinkBackendActorRef actor,
            RoutingId destNodeRid,
            Message request,
            ActorJoinEntrySpotCallback callback,
            TimeSpan? timeout)
        {
            if (DeferEntrySpotJoinCallback)
            {
                DeferredEntrySpotJoinCallback = callback;
                return true;
            }
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
            BoundSessionSendAttempts++;
            if (BoundSessionSendAccepted)
                BoundSessionReplies.Add((actor, CopyParts(parts)));
            return BoundSessionSendAccepted;
        }

        public bool SendToActor(
            ZLinkBackendActorRef actor,
            IReadOnlyList<Message> parts,
            SendFlags flags)
        {
            LastActorSendFlags = flags;
            ActorSends.Add((actor, CopyParts(parts)));
            return ActorSendAccepted;
        }

        public ValueTask<IReadOnlyList<Message>> RequestToActorAsync(
            ZLinkBackendActorRef actor,
            IReadOnlyList<Message> parts,
            TimeSpan? timeout,
            CancellationToken cancellationToken)
        {
            _ = timeout;
            cancellationToken.ThrowIfCancellationRequested();
            ActorRequests.Add((actor, CopyParts(parts)));
            return ValueTask.FromResult(
                ActorRequestHandler?.Invoke(parts)
                ?? throw new NotSupportedException());
        }

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

    private sealed class CapturingBackendAdapterFactory(
        CapturingSpotNode node,
        CapturingSpotNode? pubSubOnlyNode = null) : IZLinkBackendAdapterFactory
    {
        private readonly CapturingChannelBackendAdapter _channelAdapter = new();

        public IZLinkChannelBackendAdapter CreateChannelAdapter() => _channelAdapter;

        public IZLinkSpotBackendAdapter CreateSpotAdapter() =>
            new CapturingSpotBackendAdapter(node, pubSubOnlyNode);

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

    private sealed class CapturingSpotBackendAdapter(
        CapturingSpotNode node,
        CapturingSpotNode? pubSubOnlyNode) : IZLinkSpotBackendAdapter
    {
        private int _createCount;

        // RouteMesh 10.0.0 unified the spot node: CreateSpotNode no longer carries
        // a SpotNodeMode and is invoked once per ZLinkSpotNodeRegistration. When a
        // pub/sub-only registration is present it is enumerated first, so the first
        // call resolves to the pub/sub-only node and later calls to the routed node.
        public IZLinkBackendSpotNode CreateSpotNode(IZLinkBackendContext context, string meshName)
        {
            var index = _createCount++;
            return index == 0 && pubSubOnlyNode is not null ? pubSubOnlyNode : node;
        }
    }

    private sealed class CapturingBackendContext : IZLinkBackendContext
    {
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void Shutdown() { }
    }

    private sealed class CapturingMessageFlowObserver : IZLinkMessageFlowObserver
    {
        private readonly ConcurrentQueue<ZLinkMessageFlowEvent> _events = new();
        private readonly TaskCompletionSource<ZLinkMessageFlowEvent> _observed =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public IReadOnlyCollection<ZLinkMessageFlowEvent> Events => _events.ToArray();

        public ValueTask OnMessageFlowAsync(
            ZLinkMessageFlowEvent flow,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            _events.Enqueue(flow);
            _observed.TrySetResult(flow);
            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkMessageFlowEvent> WaitAsync(TimeSpan timeout)
        {
            return await _observed.Task.WaitAsync(timeout);
        }

        public async Task<ZLinkMessageFlowEvent> WaitAsync(
            ZLinkMessageFlowOutcome outcome,
            TimeSpan timeout)
        {
            var deadline = DateTime.UtcNow + timeout;
            while (DateTime.UtcNow < deadline)
            {
                var observed = Events.FirstOrDefault(flow => flow.Outcome == outcome);
                if (observed is not null) return observed;
                await Task.Delay(5);
            }

            throw new TimeoutException($"Timed out waiting for message-flow outcome '{outcome}'.");
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

        public void ReportUnhandledCallbackException(Exception exception)
        {
            throw new InvalidOperationException("Runtime callback failed.", exception);
        }

        public void ReportRuntimeTaskException(string name, Exception exception)
        {
            throw new InvalidOperationException($"Runtime task '{name}' failed.", exception);
        }
    }
}
