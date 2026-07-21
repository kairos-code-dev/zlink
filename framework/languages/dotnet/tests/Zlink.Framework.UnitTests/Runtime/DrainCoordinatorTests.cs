using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Diagnostics.HealthChecks;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Runtime.Protocol;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.UnitTests;

public sealed class DrainCoordinatorTests
{
    [Fact]
    public void Propagation_Bound_Is_Polling_Plus_Five_Seconds_And_One_Hundred_Milliseconds()
    {
        var options = new ZLinkLocationOptions
        {
            PollingInterval = TimeSpan.FromSeconds(2)
        };

        Assert.Equal(
            TimeSpan.FromMilliseconds(7_100),
            ZLinkFrameworkDrainExecutor.CalculatePropagationDelay(options));
    }

    [Fact]
    public async Task Drain_Executor_Seals_Before_Publishing_And_Waits_Accepted_Work_Before_Handoff()
    {
        var probe = new DrainExecutionProbe();
        var executor = new ZLinkFrameworkDrainExecutor(
            probe.Operations,
            new ZLinkLocationOptions
            {
                // The fixed 5s read bound plus 100ms jitter leaves a 1ms
                // propagation delay while preserving the production formula.
                PollingInterval = TimeSpan.FromMilliseconds(-5_099)
            });

        var reason = await executor.ExecuteAsync(
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        Assert.Null(reason);
        Assert.Equal(
            new[]
            {
                "seal-admission",
                "marker",
                "quiesce-serving-channels",
                "wait-accepted",
                "wait-accepted-handoffs",
                "drain-actors",
                "drain-sessions",
                "drain-spots",
                "freeze-owner-writes",
                "cleanup-owner"
            },
            probe.Events);
    }

    [Fact]
    public async Task Drain_Executor_Seals_Admission_Before_Weight_Quiescence()
    {
        var probe = new DrainExecutionProbe();
        var executor = new ZLinkFrameworkDrainExecutor(
            probe.Operations,
            new ZLinkLocationOptions
            {
                // 50ms = -5.050s polling + 5s store bound + 100ms jitter.
                PollingInterval = TimeSpan.FromMilliseconds(-5_050)
            });

        await executor.ExecuteAsync(TimeSpan.FromSeconds(1), CancellationToken.None);
        Assert.True(
            probe.Events.IndexOf("seal-admission")
            < probe.Events.IndexOf("quiesce-serving-channels"));
    }

    [Fact]
    public async Task Drain_Executor_Seals_Admission_While_Weight_Propagation_Is_Pending()
    {
        var probe = new DrainExecutionProbe();
        var executor = new ZLinkFrameworkDrainExecutor(
            probe.Operations,
            new ZLinkLocationOptions
            {
                // 50ms = -5.050s polling + 5s store bound + 100ms jitter.
                PollingInterval = TimeSpan.FromMilliseconds(-5_050)
            });

        var drain = executor.ExecuteAsync(TimeSpan.FromSeconds(1), CancellationToken.None).AsTask();
        await probe.ServingChannelsQuiesced.Task.WaitAsync(TimeSpan.FromSeconds(1));

        Assert.Contains("seal-admission", probe.Events);
        Assert.Null(await drain);
        Assert.Contains("wait-accepted", probe.Events);
    }

    [Fact]
    public async Task Drain_Executor_Publishes_Marker_Before_Waiting_For_Accepted_Work()
    {
        var probe = new DrainExecutionProbe { HoldAcceptedOperations = true };
        var executor = new ZLinkFrameworkDrainExecutor(
            probe.Operations,
            new ZLinkLocationOptions { PollingInterval = TimeSpan.FromMilliseconds(-5_099) });

        var drain = executor.ExecuteAsync(TimeSpan.FromSeconds(1), CancellationToken.None).AsTask();
        await probe.AcceptedOperationsSealed.Task.WaitAsync(TimeSpan.FromSeconds(1));

        Assert.Contains("marker", probe.Events);
        Assert.Contains("quiesce-serving-channels", probe.Events);
        Assert.Equal("wait-accepted", probe.Events[^1]);
        probe.AcceptedOperationsReleased.TrySetResult();
        Assert.Null(await drain.WaitAsync(TimeSpan.FromSeconds(1)));
        Assert.True(probe.Events.IndexOf("seal-admission") < probe.Events.IndexOf("marker"));
        Assert.True(probe.Events.IndexOf("marker") < probe.Events.IndexOf("wait-accepted"));
    }

    [Fact]
    public async Task Serving_Weight_Store_Failure_Retries_After_Admission_Is_Sealed()
    {
        var probe = new DrainExecutionProbe { WeightAlwaysFails = true };
        var executor = new ZLinkFrameworkDrainExecutor(
            probe.Operations with { HasAutoConnect = false },
            new ZLinkLocationOptions { PollingInterval = TimeSpan.FromMilliseconds(1) });
        using var deadline = new CancellationTokenSource(TimeSpan.FromMilliseconds(30));

        var reason = await executor.ExecuteAsync(TimeSpan.FromMilliseconds(30), deadline.Token);

        Assert.Equal(ZLinkDrainForceReason.DrainingStatePublishFailed, reason);
        Assert.True(probe.WeightAttempts > 1);
        Assert.Equal("seal-admission", probe.Events[0]);
    }

    [Fact]
    public async Task Marker_Store_Failure_Retries_Until_Deadline_After_Admission_Seal()
    {
        var probe = new DrainExecutionProbe { MarkerAlwaysFails = true };
        var executor = new ZLinkFrameworkDrainExecutor(
            probe.Operations,
            new ZLinkLocationOptions { PollingInterval = TimeSpan.FromMilliseconds(1) });
        using var deadline = new CancellationTokenSource(TimeSpan.FromMilliseconds(30));

        var reason = await executor.ExecuteAsync(
            TimeSpan.FromMilliseconds(30),
            deadline.Token);

        Assert.Equal(ZLinkDrainForceReason.DrainingStatePublishFailed, reason);
        Assert.True(probe.MarkerAttempts > 1);
        Assert.Equal("seal-admission", probe.Events[0]);
        Assert.All(probe.Events.Skip(1), static item => Assert.Equal("marker", item));
    }

    [Fact]
    public async Task Forced_Drain_Cleans_Owner_Before_Stopping_The_Location_Runtime()
    {
        var probe = new DrainExecutionProbe();
        var executor = new ZLinkFrameworkDrainExecutor(
            probe.Operations,
            new ZLinkLocationOptions(),
            stopMeshMonitoring: () => probe.Events.Add("stop-mesh-monitoring"));

        await executor.ForceStopAsync(
            ZLinkDrainForceReason.DeadlineExceeded,
            CancellationToken.None);

        Assert.Equal(
            new[]
            {
                "drain-sessions",
                "stop-mesh-monitoring",
                "stop-runtime",
                "stop-auto-connect",
                "cleanup-owner",
                "stop-location"
            },
            probe.Events);
    }

    [Fact]
    public async Task AllocatedRoutingId_ForcedDrain_LeavesOwnerAndLeaseForHostedReleaseOrder()
    {
        var probe = new DrainExecutionProbe();
        var options = new ZLinkLocationOptions { AllocatedRoutingIdsEnabled = true };
        var executor = new ZLinkFrameworkDrainExecutor(probe.Operations, options);

        await executor.ForceStopAsync(
            ZLinkDrainForceReason.DeadlineExceeded,
            CancellationToken.None);

        Assert.Equal(
            new[]
            {
                "drain-sessions",
                "stop-runtime",
                "stop-auto-connect"
            },
            probe.Events);
    }

    [Fact]
    public async Task Fixed_Drain_Waits_For_Spot_Queue_Close_Before_Row_Release()
    {
        var closeStarted = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var allowClose = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var events = new List<string>();

        var operation = ZLinkSpotNodeCatalog.CloseBeforeReleaseAsync(
            async () =>
            {
                events.Add("close-started");
                closeStarted.TrySetResult();
                await allowClose.Task;
                events.Add("queue-drained-and-closed");
            },
            () =>
            {
                events.Add("row-released");
                return ValueTask.CompletedTask;
            }).AsTask();

        await closeStarted.Task.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.Equal(new[] { "close-started" }, events);
        Assert.False(operation.IsCompleted);
        allowClose.TrySetResult();
        await operation.WaitAsync(TimeSpan.FromSeconds(1));

        Assert.Equal(
            new[] { "close-started", "queue-drained-and-closed", "row-released" },
            events);
    }

    [Fact]
    public void Draining_Gate_Rejects_Each_New_Public_Admission_With_The_Frozen_Error()
    {
        var gate = new ZLinkDrainAdmissionGate();
        Assert.True(gate.BeginDrain());

        var spot = Assert.Throws<ZLinkFrameworkException>(gate.RequireSpotAdmission);
        Assert.Equal(ZLinkFrameworkErrorKind.RequestRejected, spot.Kind);

        var actor = Assert.Throws<ZLinkFrameworkException>(gate.RequireActorAdmission);
        Assert.Equal(ZLinkFrameworkErrorKind.ActorCreateRejected, actor.Kind);

        Assert.False(gate.TryEnterActorAdmission(out var rejectedJoin));
        rejectedJoin.Dispose();
    }

    [Fact]
    public async Task Drain_Waits_For_Actor_Admission_Accepted_Before_The_Transition()
    {
        var gate = new ZLinkDrainAdmissionGate();
        Assert.True(gate.TryEnterActorAdmission(out var accepted));
        Assert.True(gate.BeginDrain());
        Assert.False(gate.TryEnterActorAdmission(out var rejected));
        rejected.Dispose();

        var wait = gate.WaitForAcceptedActorAdmissionsAsync(CancellationToken.None);
        Assert.False(wait.IsCompleted);

        accepted.Dispose();
        await wait.WaitAsync(TimeSpan.FromSeconds(1));
    }

    [Fact]
    public async Task Drain_Is_Idempotent_And_First_Deadline_Is_Fixed()
    {
        var executor = new FakeDrainExecutor();
        var admission = new ZLinkDrainAdmissionGate();
        var events = new RecordingEventPublisher();
        using var coordinator = new ZLinkDrainCoordinator(admission, executor, events);

        var first = coordinator.DrainAsync(TimeSpan.FromSeconds(3)).AsTask();
        var second = coordinator.DrainAsync(TimeSpan.FromSeconds(30)).AsTask();

        Assert.False(coordinator.IsReady);
        Assert.Equal(TimeSpan.FromSeconds(3), await executor.Started.Task.WaitAsync(TimeSpan.FromSeconds(5)));
        executor.Complete.TrySetResult(null);

        var firstResult = await first;
        var secondResult = await second;
        Assert.IsType<Drained>(firstResult);
        Assert.Same(firstResult, secondResult);
        Assert.Equal(1, executor.ExecuteCount);
        Assert.Equal(
            new[] { ZLinkDrainState.Draining, ZLinkDrainState.Drained },
            events.States);
    }

    [Fact]
    public async Task Drain_Lifecycle_Callbacks_Share_One_Lifecycle_Origin_Flow()
    {
        var executor = new FakeDrainExecutor();
        var events = new RecordingEventPublisher();
        using var coordinator = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events,
            flowCaptureEnabled: static () => true);

        var drain = coordinator.DrainAsync(TimeSpan.FromSeconds(3)).AsTask();
        await executor.Started.Task.WaitAsync(TimeSpan.FromSeconds(1));
        executor.Complete.TrySetResult(null);
        Assert.IsType<Drained>(await drain.WaitAsync(TimeSpan.FromSeconds(1)));

        Assert.Equal(2, events.Flows.Count);
        Assert.All(events.Flows, callback =>
        {
            Assert.Equal(ZLinkFlowOrigin.Lifecycle, callback.Flow.Origin);
            Assert.True(ZlinkStreamFlowId.IsValid(callback.Flow.FlowId));
        });
        Assert.Equal(events.Flows[0].Flow.FlowId, events.Flows[1].Flow.FlowId);
        Assert.Equal(
            new[] { ZLinkDrainState.Draining, ZLinkDrainState.Drained },
            events.Flows.Select(static callback => callback.State));
    }

    [Fact]
    public async Task Drain_Does_Not_Complete_Until_The_InFlight_Executor_Completes()
    {
        var executor = new FakeDrainExecutor();
        using var coordinator = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events: null);

        var drain = coordinator.DrainAsync(TimeSpan.FromSeconds(3)).AsTask();
        await executor.Started.Task.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.False(drain.IsCompleted);

        executor.Complete.TrySetResult(null);

        Assert.IsType<Drained>(await drain.WaitAsync(TimeSpan.FromSeconds(1)));
    }

    [Fact]
    public async Task Executor_Reported_Marker_Publish_Failure_Is_The_Exact_ForceStopped_Reason()
    {
        var executor = new FakeDrainExecutor();
        using var coordinator = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events: null);

        var drain = coordinator.DrainAsync(TimeSpan.FromSeconds(3)).AsTask();
        await executor.Started.Task.WaitAsync(TimeSpan.FromSeconds(1));
        executor.Complete.TrySetResult(ZLinkDrainForceReason.DrainingStatePublishFailed);

        var forced = Assert.IsType<ForceStopped>(
            await drain.WaitAsync(TimeSpan.FromSeconds(1)));
        Assert.Equal(ZLinkDrainForceReason.DrainingStatePublishFailed, forced.Reason);
        Assert.Equal(ZLinkDrainForceReason.DrainingStatePublishFailed, executor.ForceReason);
    }

    [Fact]
    public async Task Force_Stop_Event_Failure_Does_Not_Repeat_Forced_Teardown()
    {
        var executor = new FakeDrainExecutor();
        using var coordinator = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            new ForceStoppingFailurePublisher());

        var first = coordinator.DrainAsync(TimeSpan.FromSeconds(1)).AsTask();
        var second = coordinator.DrainAsync(TimeSpan.FromSeconds(5)).AsTask();
        await executor.Started.Task.WaitAsync(TimeSpan.FromSeconds(1));
        executor.Complete.TrySetResult(ZLinkDrainForceReason.DeadlineExceeded);

        var firstResult = Assert.IsType<ForceStopped>(await first.WaitAsync(TimeSpan.FromSeconds(1)));
        Assert.Same(firstResult, await second.WaitAsync(TimeSpan.FromSeconds(1)));
        Assert.Equal(1, executor.ForceCount);
        Assert.Same(firstResult, await coordinator.AwaitDrainedAsync());
    }

    [Fact]
    public async Task RouteMesh_Runtime_Rejects_Host_Global_Drain_For_Multiple_Meshes()
    {
        await using var services = new ServiceCollection().BuildServiceProvider();
        var registration = new ZLinkFrameworkRegistration();
        registration.SpotNodes.Add(
            "orders",
            new ZLinkSpotNodeRegistration { SpotNodeName = "orders" });
        registration.SpotNodes.Add(
            "billing",
            new ZLinkSpotNodeRegistration { SpotNodeName = "billing" });
        var framework = new ZLinkFrameworkRuntime(
            services,
            null!,
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration));
        var drain = new CountingDrainControl();
        using var meshRuntime = new ZLinkRouteMeshRuntimeService(
            framework,
            storeHealth: null,
            locationQuery: null,
            () => drain);

        await Assert.ThrowsAsync<ZLinkConfigurationException>(
            () => meshRuntime.DrainAsync("orders").AsTask());
        await Assert.ThrowsAsync<ZLinkConfigurationException>(
            () => meshRuntime.AwaitDrainedAsync("orders").AsTask());
        Assert.Equal(0, drain.CallCount);
    }

    [Fact]
    public async Task Default_Drain_Uses_Thirty_Seconds_Without_Event_Registration()
    {
        var executor = new FakeDrainExecutor();
        using var coordinator = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events: null);

        var drain = coordinator.DrainAsync().AsTask();
        Assert.Equal(
            TimeSpan.FromSeconds(30),
            await executor.Started.Task.WaitAsync(TimeSpan.FromSeconds(1)));
        executor.Complete.TrySetResult(null);

        Assert.IsType<Drained>(await drain.WaitAsync(TimeSpan.FromSeconds(1)));
    }

    [Fact]
    public async Task Host_Stop_Uses_The_Same_Thirty_Second_Default_Deadline()
    {
        var executor = new FakeDrainExecutor();
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(_ => { });
        builder.Services.AddSingleton<IZLinkDrainExecutor>(executor);
        using var host = builder.Build();
        await host.StartAsync();

        var stop = host.StopAsync();
        Assert.Equal(
            TimeSpan.FromSeconds(30),
            await executor.Started.Task.WaitAsync(TimeSpan.FromSeconds(1)));
        Assert.False(stop.IsCompleted);
        executor.Complete.TrySetResult(null);

        await stop.WaitAsync(TimeSpan.FromSeconds(1));
    }

    [Fact]
    public async Task Waiter_Cancellation_Does_Not_Cancel_Shared_Drain()
    {
        var executor = new FakeDrainExecutor();
        using var coordinator = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events: null);
        using var waiterCancellation = new CancellationTokenSource();
        var canceledWaiter = coordinator.AwaitDrainedAsync(waiterCancellation.Token).AsTask();

        var drain = coordinator.DrainAsync(TimeSpan.FromSeconds(3)).AsTask();
        waiterCancellation.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => canceledWaiter);

        executor.Complete.TrySetResult(null);
        var result = await drain;
        Assert.Same(result, await coordinator.AwaitDrainedAsync());
        Assert.IsType<Drained>(result);
    }

    [Fact]
    public async Task Deadline_Validation_Happens_Before_Drain_Starts()
    {
        var executor = new FakeDrainExecutor();
        using var coordinator = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events: null);

        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(async () =>
            await coordinator.DrainAsync(TimeSpan.Zero));

        Assert.True(coordinator.IsReady);
        Assert.Equal(0, executor.ExecuteCount);
    }

    [Fact]
    public async Task Deadline_Expiry_Force_Stops_With_The_Frozen_Reason()
    {
        var executor = new FakeDrainExecutor { WaitForDeadline = true };
        using var coordinator = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events: null);

        var result = await coordinator.DrainAsync(TimeSpan.FromMilliseconds(20));

        var forced = Assert.IsType<ForceStopped>(result);
        Assert.Equal(ZLinkDrainForceReason.DeadlineExceeded, forced.Reason);
        Assert.Equal(ZLinkDrainForceReason.DeadlineExceeded, executor.ForceReason);
    }

    [Fact]
    public async Task Force_Stop_Reports_Owner_Cleanup_Failure_As_The_Terminal_Reason()
    {
        var executor = new FakeDrainExecutor
        {
            ForceFailureReason = ZLinkDrainForceReason.OwnerCleanupFailed
        };
        using var coordinator = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events: null);

        var drain = coordinator.DrainAsync(TimeSpan.FromSeconds(1)).AsTask();
        await executor.Started.Task;
        executor.Complete.TrySetResult(ZLinkDrainForceReason.DeadlineExceeded);

        var forced = Assert.IsType<ForceStopped>(await drain);
        Assert.Equal(ZLinkDrainForceReason.OwnerCleanupFailed, forced.Reason);
    }

    [Fact]
    public void Actor_Drain_Uses_Only_The_Mesh_That_Owns_The_Actor_Factory()
    {
        var registration = new ZLinkFrameworkRegistration();
        registration.SpotNodes.Add(
            "rooms",
            new ZLinkSpotNodeRegistration
            {
                SpotNodeName = "rooms",
                SpotMeshChannelName = "room-mesh"
            });
        var actors = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "actors",
            SpotMeshChannelName = "actor-mesh"
        };
        actors.ActorFactories.Add("player", typeof(object));
        registration.SpotNodes.Add("actors", actors);

        Assert.Equal(
            "actor-mesh",
            ZLinkFrameworkRuntime.ResolveActorDrainMeshName(registration, "player"));
        Assert.Null(ZLinkFrameworkRuntime.ResolveActorDrainMeshName(registration, "unknown"));
    }

    [Fact]
    public async Task Drain_Health_Check_Projects_Readiness_Without_Starting_Drain()
    {
        var drain = new MutableDrainControl();
        var registrations = new ServiceCollection()
            .AddLogging()
            .AddSingleton<IZLinkDrainControl>(drain);
        await using var services = registrations
            .AddHealthChecks()
            .AddZLinkDrainHealthCheck()
            .Services
            .BuildServiceProvider();
        var health = services.GetRequiredService<HealthCheckService>();

        var serving = await health.CheckHealthAsync();
        Assert.Equal(HealthStatus.Healthy, serving.Status);
        Assert.Equal(HealthStatus.Healthy, serving.Entries["zlink-drain"].Status);

        drain.IsReady = false;
        var draining = await health.CheckHealthAsync();
        Assert.Equal(HealthStatus.Unhealthy, draining.Status);
        Assert.Equal(HealthStatus.Unhealthy, draining.Entries["zlink-drain"].Status);
    }

    [Fact]
    public async Task Framework_Registration_Resolves_Production_Drain_Without_Locations()
    {
        var registrations = new ServiceCollection();
        registrations.AddZLinkFramework(_ => { });
        await using var services = registrations.BuildServiceProvider();

        var drain = services.GetRequiredService<IZLinkDrainControl>();
        var result = await drain.DrainAsync(TimeSpan.FromSeconds(1));

        Assert.IsType<Drained>(result);
        Assert.False(drain.IsReady);
    }

    [Fact]
    public void Drain_Result_Base_Has_No_Externally_Callable_Constructor()
    {
        Assert.Empty(typeof(ZLinkDrainResult).GetConstructors(
            System.Reflection.BindingFlags.Public
            | System.Reflection.BindingFlags.Instance));
    }

    [Fact]
    public async Task Framework_Drain_Sends_ServerDrain_Before_Orderly_Stream_Close()
    {
        var port = FindFreeTcpPort();
        var sessionProbe = new DrainSessionProbe();
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(sessionProbe);
        builder.Services.AddZLinkFramework(options =>
            options.AddStreamNode("drain-stream")
                .Bind($"tcp://127.0.0.1:{port}")
                .AddSession<DrainSession>());
        using var host = builder.Build();
        await host.StartAsync();

        var disconnected = new TaskCompletionSource<ZlinkStreamCloseReason>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var closingObserved = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        await using var connector = ZlinkStreamConnectorFactory.Create(
            new ZlinkStreamConnectorOptions
            {
                Endpoint = new Uri($"tcp://127.0.0.1:{port}"),
                DispatchMode = ZlinkStreamDispatchMode.Immediate,
                Reconnect = new ZlinkStreamReconnectOptions { Enabled = false },
                Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false }
            });
        connector.Disconnected += (closed, _) =>
        {
            disconnected.TrySetResult(closed.CloseReason);
            return ValueTask.CompletedTask;
        };
        connector.ObserveInbound((frame, _) =>
        {
            if (string.Equals(frame.Name, "session-closing", StringComparison.Ordinal))
                closingObserved.TrySetResult();
            return ValueTask.CompletedTask;
        });
        await connector.Connect.Async();
        connector.Send(new DrainProbeMessage("connected"))
            .PacketName("drain.probe")
            .Submit();
        await sessionProbe.Connected.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var result = await host.Services.GetRequiredService<IZLinkDrainControl>()
            .DrainAsync(TimeSpan.FromSeconds(5));

        Assert.IsType<Drained>(result);
        await closingObserved.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(
            ZlinkStreamCloseReason.ServerDrain,
            await disconnected.Task.WaitAsync(TimeSpan.FromSeconds(5)));
        await host.StopAsync();
    }

    [Fact]
    public async Task New_Stream_Session_After_Drain_Is_Rejected_With_ServerDrain()
    {
        var port = FindFreeTcpPort();
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
            options.AddStreamNode("drain-stream")
                .Bind($"tcp://127.0.0.1:{port}")
                .AddSession<DrainSession>());
        using var host = builder.Build();
        await host.StartAsync();

        var result = await host.Services.GetRequiredService<IZLinkDrainControl>()
            .DrainAsync(TimeSpan.FromSeconds(5));
        Assert.IsType<Drained>(result);

        var disconnected = new TaskCompletionSource<ZlinkStreamCloseReason>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        await using var connector = ZlinkStreamConnectorFactory.Create(
            new ZlinkStreamConnectorOptions
            {
                Endpoint = new Uri($"tcp://127.0.0.1:{port}"),
                DispatchMode = ZlinkStreamDispatchMode.Immediate,
                Reconnect = new ZlinkStreamReconnectOptions { Enabled = false },
                Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false }
            });
        connector.Disconnected += (closed, _) =>
        {
            disconnected.TrySetResult(closed.CloseReason);
            return ValueTask.CompletedTask;
        };

        await connector.Connect.Async();
        connector.Send(new DrainProbeMessage("new-after-drain"))
            .PacketName("drain.probe")
            .Submit();

        Assert.Equal(
            ZlinkStreamCloseReason.ServerDrain,
            await disconnected.Task.WaitAsync(TimeSpan.FromSeconds(5)));
        await host.StopAsync();
    }

    private sealed class FakeDrainExecutor : IZLinkDrainExecutor
    {
        public TaskCompletionSource<TimeSpan> Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource<ZLinkDrainForceReason?> Complete { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int ExecuteCount { get; private set; }

        public ZLinkDrainForceReason? ForceReason { get; private set; }

        public int ForceCount { get; private set; }

        public bool WaitForDeadline { get; init; }

        public ZLinkDrainForceReason? ForceFailureReason { get; init; }

        public async ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
            TimeSpan deadline,
            CancellationToken deadlineToken)
        {
            ExecuteCount++;
            Started.TrySetResult(deadline);
            if (WaitForDeadline)
            {
                await Task.Delay(Timeout.InfiniteTimeSpan, deadlineToken);
                return null;
            }
            return await Complete.Task.WaitAsync(deadlineToken);
        }

        public ValueTask ForceStopAsync(
            ZLinkDrainForceReason reason,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            ForceCount++;
            ForceReason = reason;
            if (ForceFailureReason is { } failureReason)
                throw new ZLinkDrainForceException(
                    failureReason,
                    [new InvalidOperationException("owner cleanup failed")]);
            return ValueTask.CompletedTask;
        }
    }

    private sealed class DrainExecutionProbe
    {
        public List<string> Events { get; } = [];

        public TaskCompletionSource ServingChannelsQuiesced { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public bool MarkerAlwaysFails { get; init; }

        public bool WeightAlwaysFails { get; init; }

        public bool HoldAcceptedOperations { get; init; }

        public TaskCompletionSource AcceptedOperationsSealed { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource AcceptedOperationsReleased { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int MarkerAttempts { get; private set; }

        public int WeightAttempts { get; private set; }

        public ZLinkDrainExecutionOperations Operations => new(
            HasAutoConnect: true,
            HasLocationRuntime: true,
            QuiesceServingChannels: _ =>
            {
                Events.Add("quiesce-serving-channels");
                WeightAttempts++;
                ServingChannelsQuiesced.TrySetResult();
                return ValueTask.FromResult(!WeightAlwaysFails);
            },
            MarkDraining: _ =>
            {
                Events.Add("marker");
                MarkerAttempts++;
                return ValueTask.FromResult(!MarkerAlwaysFails);
            },
            SealApplicationAdmissions: () => Events.Add("seal-admission"),
            WaitForAcceptedOperations: () =>
            {
                Events.Add("wait-accepted");
                AcceptedOperationsSealed.TrySetResult();
                return HoldAcceptedOperations
                    ? AcceptedOperationsReleased.Task
                    : Task.CompletedTask;
            },
            WaitForAcceptedActorHandoffs: _ =>
            {
                Events.Add("wait-accepted-handoffs");
                return Task.CompletedTask;
            },
            DrainActors: _ =>
            {
                Events.Add("drain-actors");
                return ValueTask.FromResult(true);
            },
            DrainSpots: _ =>
            {
                Events.Add("drain-spots");
                return ValueTask.FromResult(true);
            },
            DrainStreamSessions: _ =>
            {
                Events.Add("drain-sessions");
                return ValueTask.FromResult(true);
            },
            FreezeOwnerWrites: _ =>
            {
                Events.Add("freeze-owner-writes");
                return ValueTask.CompletedTask;
            },
            CleanupOwner: _ =>
            {
                Events.Add("cleanup-owner");
                return ValueTask.CompletedTask;
            },
            GetRemainderCounts: static () => new ZLinkDrainRemainderCounts(0, 0, 0, 0),
            ForceStopRuntime: _ =>
            {
                Events.Add("stop-runtime");
                return ValueTask.CompletedTask;
            },
            StopAutoConnect: _ =>
            {
                Events.Add("stop-auto-connect");
                return ValueTask.CompletedTask;
            },
            StopLocation: _ =>
            {
                Events.Add("stop-location");
                return ValueTask.CompletedTask;
            });
    }

    private sealed class RecordingEventPublisher : IZLinkRuntimeEventPublisher
    {
        public List<ZLinkDrainState> States { get; } = [];

        public List<(ZLinkDrainState State, ZLinkFlowValue Flow)> Flows { get; } = [];

        public ValueTask PublishAsync<TEvent>(TEvent @event, CancellationToken cancellationToken)
            where TEvent : IZLinkRuntimeEvent
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (@event is ZLinkDrainEvent drain)
            {
                States.Add(drain.State);
                if (ZLinkFlowContext.Current is { } flow)
                    Flows.Add((drain.State, flow));
            }
            return ValueTask.CompletedTask;
        }
    }

    private sealed class ForceStoppingFailurePublisher : IZLinkRuntimeEventPublisher
    {
        public ValueTask PublishAsync<TEvent>(TEvent @event, CancellationToken cancellationToken)
            where TEvent : IZLinkRuntimeEvent
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (@event is ZLinkDrainEvent { State: ZLinkDrainState.ForceStopping })
                throw new InvalidOperationException("force-stopping event sink failed");
            return ValueTask.CompletedTask;
        }
    }

    private sealed class CountingDrainControl : IZLinkDrainControl
    {
        public int CallCount { get; private set; }

        public bool IsReady => true;

        public ValueTask<ZLinkDrainResult> DrainAsync(
            CancellationToken cancellationToken = default) =>
            DrainAsync(TimeSpan.FromSeconds(30), cancellationToken);

        public ValueTask<ZLinkDrainResult> DrainAsync(
            TimeSpan deadline,
            CancellationToken cancellationToken = default)
        {
            _ = deadline;
            cancellationToken.ThrowIfCancellationRequested();
            CallCount++;
            return ValueTask.FromResult<ZLinkDrainResult>(new Drained());
        }

        public ValueTask<ZLinkDrainResult> AwaitDrainedAsync(
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            CallCount++;
            return ValueTask.FromResult<ZLinkDrainResult>(new Drained());
        }
    }

    private sealed class MutableDrainControl : IZLinkDrainControl
    {
        public bool IsReady { get; set; } = true;

        public ValueTask<ZLinkDrainResult> DrainAsync(CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("The readiness check must not start drain.");

        public ValueTask<ZLinkDrainResult> DrainAsync(
            TimeSpan deadline,
            CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("The readiness check must not start drain.");

        public ValueTask<ZLinkDrainResult> AwaitDrainedAsync(
            CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("The readiness check must not await drain.");
    }

    private static int FindFreeTcpPort()
    {
        var listener = new System.Net.Sockets.TcpListener(
            System.Net.IPAddress.Loopback,
            0);
        listener.Start();
        try
        {
            return ((System.Net.IPEndPoint)listener.LocalEndpoint).Port;
        }
        finally
        {
            listener.Stop();
        }
    }

    private sealed class DrainSessionProbe
    {
        public TaskCompletionSource Connected { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed record DrainProbeMessage(string Value);

    private sealed class DrainSession(
        IZLinkSessionContext context,
        DrainSessionProbe probe) : IZLinkSession
    {
        public IZLinkSessionContext Context { get; } = context;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            probe.Connected.TrySetResult();
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }
}
