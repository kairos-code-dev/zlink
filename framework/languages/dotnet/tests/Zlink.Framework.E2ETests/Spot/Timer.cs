using System.Collections.Concurrent;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests;

public sealed class TimerTests : SpotTestSupport
{
    [Fact]
    public async Task SpotTimer_Provides_Tick_Metadata()
    {
        using var stopSource = new CancellationTokenSource();
        var completion = new TaskCompletionSource<ZLinkTimerTick>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        await using var timer = new ZLinkTimer(
            "metadata",
            TimeSpan.FromMilliseconds(20),
            new ZLinkTimerOptions(),
            stopSource.Token,
            (tick, _) =>
            {
                completion.TrySetResult(tick);
                return ValueTask.CompletedTask;
            },
            static (_, _, _, _) => ValueTask.CompletedTask);

        var observed = await completion.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await timer.CancelAsync();

        Assert.Equal("metadata", observed.Name);
        Assert.Equal(1UL, observed.DeliveryIndex);
        Assert.True(observed.ScheduledIndex >= 1);
        Assert.Equal(TimeSpan.FromMilliseconds(20), observed.Period);
        Assert.True(observed.StartedAt >= observed.ScheduledAt);
        Assert.True(observed.StartedElapsed >= observed.ScheduledElapsed);
        Assert.Equal(observed.StartedElapsed - observed.ScheduledElapsed, observed.Delay);
    }

    [Fact]
    public async Task SpotTimer_Skips_Late_Ticks_When_Configured()
    {
        using var stopSource = new CancellationTokenSource();
        var completion = new TaskCompletionSource<ZLinkTimerTick>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var callCount = 0;

        await using var timer = new ZLinkTimer(
            "skip",
            TimeSpan.FromMilliseconds(10),
            new ZLinkTimerOptions
            {
                OverrunPolicy = ZLinkTimerOverrunPolicy.SkipLateTicks
            },
            stopSource.Token,
            async (tick, _) =>
            {
                var count = Interlocked.Increment(ref callCount);
                if (count == 1)
                {
                    await Task.Delay(80);
                    return;
                }

                completion.TrySetResult(tick);
            },
            static (_, _, _, _) => ValueTask.CompletedTask);

        var observed = await completion.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await timer.CancelAsync();

        Assert.Equal(2UL, observed.DeliveryIndex);
        Assert.True(observed.ScheduledIndex > observed.DeliveryIndex);
        Assert.True(observed.SkippedTicks > 0);
    }

    [Fact]
    public async Task SpotTimer_Catches_Up_Within_Configured_Limit()
    {
        using var stopSource = new CancellationTokenSource();
        var observed = new ConcurrentQueue<ZLinkTimerTick>();
        var completion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var callCount = 0;

        await using var timer = new ZLinkTimer(
            "catch-up",
            TimeSpan.FromMilliseconds(10),
            new ZLinkTimerOptions
            {
                OverrunPolicy = ZLinkTimerOverrunPolicy.CatchUpBounded,
                MaxCatchUpTicks = 2
            },
            stopSource.Token,
            async (tick, _) =>
            {
                var count = Interlocked.Increment(ref callCount);
                if (count == 1)
                {
                    await Task.Delay(80);
                    return;
                }

                observed.Enqueue(tick);
                if (observed.Count >= 2)
                {
                    completion.TrySetResult();
                }
            },
            static (_, _, _, _) => ValueTask.CompletedTask);

        await completion.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await timer.CancelAsync();

        var ticks = observed.ToArray();
        Assert.True(ticks[0].SkippedTicks > 0);
        Assert.Equal(ticks[0].ScheduledIndex + 1, ticks[1].ScheduledIndex);
        Assert.Equal(0UL, ticks[1].SkippedTicks);
    }

    [Fact]
    public async Task SpotTimer_DelayNextTick_Waits_After_Handler_Completion()
    {
        using var stopSource = new CancellationTokenSource();
        var observed = new ConcurrentQueue<ZLinkTimerTick>();
        var completion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var callCount = 0;

        await using var timer = new ZLinkTimer(
            "delay-next",
            TimeSpan.FromMilliseconds(40),
            new ZLinkTimerOptions
            {
                OverrunPolicy = ZLinkTimerOverrunPolicy.DelayNextTick
            },
            stopSource.Token,
            async (tick, _) =>
            {
                observed.Enqueue(tick);
                if (Interlocked.Increment(ref callCount) == 1)
                {
                    await Task.Delay(90);
                    return;
                }

                completion.TrySetResult();
            },
            static (_, _, _, _) => ValueTask.CompletedTask);

        await completion.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await timer.CancelAsync();

        var ticks = observed.ToArray();
        Assert.Equal(0UL, ticks[0].SkippedTicks);
        Assert.Equal(0UL, ticks[1].SkippedTicks);
        Assert.True(ticks[1].StartedElapsed - ticks[0].StartedElapsed >= TimeSpan.FromMilliseconds(120));
    }

    [Fact]
    public async Task SpotTimer_StopOnUnhandledException_Stops_Timer()
    {
        using var stopSource = new CancellationTokenSource();
        var reported = new TaskCompletionSource<(ZLinkTimerTick Tick, bool Stopped)>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var callCount = 0;

        await using var timer = new ZLinkTimer(
            "stop-on-error",
            TimeSpan.FromMilliseconds(10),
            new ZLinkTimerOptions
            {
                StopOnUnhandledException = true
            },
            stopSource.Token,
            (_, _) =>
            {
                Interlocked.Increment(ref callCount);
                throw new InvalidOperationException("timer failed");
            },
            (tick, _, stopped, _) =>
            {
                reported.TrySetResult((tick, stopped));
                return ValueTask.CompletedTask;
            });

        var failure = await reported.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await Task.Delay(80);
        await timer.CancelAsync();

        Assert.True(failure.Stopped);
        Assert.Equal(1UL, failure.Tick.DeliveryIndex);
        Assert.Equal(1, Volatile.Read(ref callCount));
    }

    [Fact]
    public async Task EntrySpotTimer_Does_Not_Reenter_Same_Timer()
    {
        using var stopSource = new CancellationTokenSource();
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var active = 0;
        var reentered = false;

        await using var timer = new ZLinkTimer(
            "entry-non-reentrant",
            TimeSpan.FromMilliseconds(10),
            new ZLinkTimerOptions(),
            stopSource.Token,
            async (_, _) =>
            {
                if (Interlocked.Increment(ref active) != 1)
                {
                    reentered = true;
                }

                try
                {
                    await Task.Delay(120);
                    completion.TrySetResult();
                }
                finally
                {
                    Interlocked.Decrement(ref active);
                }
            },
            static (_, _, _, _) => ValueTask.CompletedTask);

        await completion.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await timer.CancelAsync();

        Assert.False(reentered);
    }

    [Fact]
    public async Task SpotTimer_Reports_Handler_Exception_To_Monitoring()
    {
        var services = new ServiceCollection();
        services.AddSingleton<TimerFailureProbe>();
        services.AddSingleton<IZLinkRuntimeEventHandler<ZLinkSpotEvent>>(static provider =>
            provider.GetRequiredService<TimerFailureProbe>());
        services.AddSingleton<ZLinkRuntimeEventDispatcher>();

        await using var provider = services.BuildServiceProvider();
        var dispatcher = provider.GetRequiredService<ZLinkRuntimeEventDispatcher>();
        using var stopSource = new CancellationTokenSource();
        var failed = 0;

        await using var timer = new ZLinkTimer(
            "failing",
            TimeSpan.FromMilliseconds(10),
            new ZLinkTimerOptions(),
            stopSource.Token,
            (_, _) =>
            {
                if (Interlocked.Exchange(ref failed, 1) == 0)
                {
                    throw new InvalidOperationException("timer failure probe");
                }

                return ValueTask.CompletedTask;
            },
            (tick, exception, stopped, ct) =>
            {
                return dispatcher.PublishAsync(
                    new ZLinkSpotEvent(
                        "timer-node",
                        DateTimeOffset.UtcNow,
                        stopped
                            ? ZLinkSpotEventKind.TimerStoppedAfterUnhandledException
                            : ZLinkSpotEventKind.TimerHandlerFailed,
                        Status: null,
                        Peers: null,
                        Subjects: null,
                        TimerDiagnostic: new ZLinkSpotTimerDiagnostic(
                            RoutingId.FromString("01"),
                            IsEntrySpot: false,
                            tick.Name,
                            "FailingTimerHandler",
                            tick.DeliveryIndex,
                            tick.ScheduledIndex,
                            exception.GetType().FullName ?? exception.GetType().Name,
                            exception.Message)),
                    ct);
            });

        var @event = await provider.GetRequiredService<TimerFailureProbe>()
            .WaitAsync(TimeSpan.FromSeconds(5));
        await timer.CancelAsync();

        Assert.Equal(ZLinkSpotEventKind.TimerHandlerFailed, @event.Event);
        Assert.Equal("timer-node", @event.SourceName);
        Assert.NotNull(@event.TimerDiagnostic);
        Assert.Equal("failing", @event.TimerDiagnostic?.TimerName);
        Assert.False(@event.TimerDiagnostic?.IsEntrySpot);
    }

    [Fact]
    public async Task EntrySpotTimer_Does_Not_Block_EntrySpot_Callbacks_Globally()
    {
        var spotNode = GetFreeTcpEndpoint();
        var spotChannel = $"game.entry-timer.{Guid.NewGuid():N}";

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotTimerCallbackRecorder>();
        builder.Services.AddScoped<EntryTimerSpot>();
        builder.Services.AddScoped<EntryTimerBlockingHandler>();
        builder.Services.AddScoped<EntryTimerRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh(spotChannel, mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("entry-timer-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(spotNode);
                });
                spot.AddEntrySpot<EntryTimerSpot>();
            });
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotTimerCallbackRecorder>();
        var nodeRuntime = GetSpotNodeRuntime(runtime, "entry-timer-node");
        var activation = nodeRuntime.EntrySpotActivation
            ?? throw new InvalidOperationException("Entry Spot activation was not created.");

        await recorder.TimerStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var record = new EntryTimerRecordCommand("record");
        var recordHeader = CreateEntrySpotEnvelopeHeader(spotChannel, record);
        Assert.True(activation.TryResolvePacket(recordHeader, out var recordDescriptor));
        var recordDispatch = activation
            .InvokePacketAsync(recordDescriptor!, record, CancellationToken.None)
            .AsTask();

        await recorder.Recorded.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.DoesNotContain("timer-end", recorder.Events);

        recorder.ReleaseTimer.TrySetResult();
        await recordDispatch.WaitAsync(TimeSpan.FromSeconds(5));
        await RetryAsync(
            () => recorder.Events.Contains("timer-end"),
            TimeSpan.FromSeconds(5));

        await host.StopAsync();
    }

    [Fact]
    public async Task SpotTimer_CancelAsync_Stops_Managed_Timer_Loop()
    {
        using var stopSource = new CancellationTokenSource();
        var firstTick = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var tickCount = 0;

        await using var timer = new ZLinkTimer(
            "cancel",
            TimeSpan.FromMilliseconds(10),
            new ZLinkTimerOptions(),
            stopSource.Token,
            (_, _) =>
            {
                Interlocked.Increment(ref tickCount);
                firstTick.TrySetResult();
                return ValueTask.CompletedTask;
            },
            static (_, _, _, _) => ValueTask.CompletedTask);

        await firstTick.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await timer.CancelAsync();
        var ticksAfterCancel = Volatile.Read(ref tickCount);

        await Task.Delay(80);

        Assert.True(timer.IsDisposed);
        Assert.Equal(ticksAfterCancel, Volatile.Read(ref tickCount));
    }

    [Fact]
    public async Task SpotTimer_NonCatchUpPolicy_Ignores_MaxCatchUpTicks()
    {
        using var stopSource = new CancellationTokenSource();
        await using var registry = new ZLinkSpotTimerRegistry();
        var firstTick = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        var timer = await registry.AddAsync(
            "non-catch-up",
            TimeSpan.FromMilliseconds(10),
            new ZLinkTimerOptions
            {
                OverrunPolicy = ZLinkTimerOverrunPolicy.DelayNextTick,
                MaxCatchUpTicks = 0
            },
            typeof(SpotHeartbeatTimerHandler),
            typeof(PublishingStageSpot),
            stopSource.Token,
            (_, _, _) =>
            {
                firstTick.TrySetResult();
                return ValueTask.CompletedTask;
            },
            static (_, _, _, _, _) => ValueTask.CompletedTask,
            CancellationToken.None);

        await firstTick.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await timer.CancelAsync();
    }

    [Fact]
    public async Task SpotTimer_CatchUpPolicy_Rejects_Invalid_MaxCatchUpTicks()
    {
        using var stopSource = new CancellationTokenSource();
        await using var registry = new ZLinkSpotTimerRegistry();

        await Assert.ThrowsAsync<ZLinkConfigurationException>(async () =>
        {
            _ = await registry.AddAsync(
                "invalid-catch-up",
                TimeSpan.FromMilliseconds(10),
                new ZLinkTimerOptions
                {
                    OverrunPolicy = ZLinkTimerOverrunPolicy.CatchUpBounded,
                    MaxCatchUpTicks = 0
                },
                typeof(SpotHeartbeatTimerHandler),
                typeof(PublishingStageSpot),
                stopSource.Token,
                static (_, _, _) => ValueTask.CompletedTask,
                static (_, _, _, _, _) => ValueTask.CompletedTask,
                CancellationToken.None);
        });
    }

    [Fact]
    public async Task SpotTimer_Rejects_Unknown_OverrunPolicy()
    {
        using var stopSource = new CancellationTokenSource();
        await using var registry = new ZLinkSpotTimerRegistry();

        await Assert.ThrowsAsync<ZLinkConfigurationException>(async () =>
        {
            _ = await registry.AddAsync(
                "unknown-policy",
                TimeSpan.FromMilliseconds(10),
                new ZLinkTimerOptions
                {
                    OverrunPolicy = (ZLinkTimerOverrunPolicy)999
                },
                typeof(SpotHeartbeatTimerHandler),
                typeof(PublishingStageSpot),
                stopSource.Token,
                static (_, _, _) => ValueTask.CompletedTask,
                static (_, _, _, _, _) => ValueTask.CompletedTask,
                CancellationToken.None);
        });
    }
}
