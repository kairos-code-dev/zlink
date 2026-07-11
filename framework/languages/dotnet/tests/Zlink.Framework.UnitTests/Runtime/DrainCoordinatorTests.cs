using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Diagnostics.HealthChecks;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;

public sealed class DrainCoordinatorTests
{
    [Fact]
    public async Task Drain_Is_Idempotent_And_First_Deadline_Is_Fixed()
    {
        var executor = new FakeDrainExecutor();
        var admission = new ZLinkDrainAdmissionGate();
        var events = new RecordingEventPublisher();
        var coordinator = new ZLinkDrainCoordinator(admission, executor, events);

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
    public async Task Waiter_Cancellation_Does_Not_Cancel_Shared_Drain()
    {
        var executor = new FakeDrainExecutor();
        var coordinator = new ZLinkDrainCoordinator(
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
        var coordinator = new ZLinkDrainCoordinator(
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
        var coordinator = new ZLinkDrainCoordinator(
            new ZLinkDrainAdmissionGate(),
            executor,
            events: null);

        var result = await coordinator.DrainAsync(TimeSpan.FromMilliseconds(20));

        var forced = Assert.IsType<ForceStopped>(result);
        Assert.Equal(ZLinkDrainForceReason.DeadlineExceeded, forced.Reason);
        Assert.Equal(ZLinkDrainForceReason.DeadlineExceeded, executor.ForceReason);
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
                .RegisterSession<DrainSession>());
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

    private sealed class FakeDrainExecutor : IZLinkDrainExecutor
    {
        public TaskCompletionSource<TimeSpan> Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource<ZLinkDrainForceReason?> Complete { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int ExecuteCount { get; private set; }

        public ZLinkDrainForceReason? ForceReason { get; private set; }

        public bool WaitForDeadline { get; init; }

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
            ForceReason = reason;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class RecordingEventPublisher : IZLinkRuntimeEventPublisher
    {
        public List<ZLinkDrainState> States { get; } = [];

        public ValueTask PublishAsync<TEvent>(TEvent @event, CancellationToken cancellationToken)
            where TEvent : IZLinkRuntimeEvent
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (@event is ZLinkDrainEvent drain) States.Add(drain.State);
            return ValueTask.CompletedTask;
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
