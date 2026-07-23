using Zlink.Framework.Runtime.Spots;
using Zlink.Framework.Runtime.Timers;

namespace Zlink.Framework.UnitTests;

public sealed class TimerLifecycleTests
{
    [Fact]
    public async Task Timer_freeze_preserves_logical_cursor_and_resume_dispatches_due_tick()
    {
        var tick = new TaskCompletionSource<ZLinkTimerTick>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var timer = new ZLinkTimer(
            "relocatable",
            TimeSpan.FromMilliseconds(30),
            new ZLinkTimerOptions(),
            CancellationToken.None,
            (value, _) =>
            {
                tick.TrySetResult(value);
                return ValueTask.CompletedTask;
            },
            static (_, _, _, _) => ValueTask.CompletedTask);

        var frozen = timer.Freeze();
        Assert.Equal("relocatable", frozen.Name);
        Assert.Equal<ulong>(0, frozen.DeliveryIndex);
        Assert.Null(frozen.PendingTick);
        await Task.Delay(80);
        Assert.False(tick.Task.IsCompleted);

        timer.Resume();
        var delivered = await tick.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal<ulong>(1, delivered.DeliveryIndex);
        Assert.True(delivered.ScheduledIndex >= 1);
        Assert.Equal(delivered.ScheduledIndex - 1, delivered.SkippedTicks);
        await timer.DisposeAsync();
    }

    [Fact]
    public async Task Timer_freeze_commits_tick_that_started_before_freeze()
    {
        var tickStarted = new TaskCompletionSource<ZLinkTimerTick>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseTick = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var timer = new ZLinkTimer(
            "pending",
            TimeSpan.FromMilliseconds(1),
            new ZLinkTimerOptions(),
            CancellationToken.None,
            async (value, _) =>
            {
                tickStarted.TrySetResult(value);
                await releaseTick.Task.ConfigureAwait(false);
            },
            static (_, _, _, _) => ValueTask.CompletedTask);

        var started = await tickStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var frozen = timer.Freeze();
        Assert.Equal(started, frozen.PendingTick);
        Assert.Equal<ulong>(0, frozen.DeliveryIndex);

        releaseTick.TrySetResult();
        await WaitUntilAsync(() => timer.Snapshot().PendingTick is null);
        var completed = timer.Snapshot();
        Assert.Null(completed.PendingTick);
        Assert.Equal(started.DeliveryIndex, completed.DeliveryIndex);
        Assert.Equal(started.ScheduledIndex, completed.LastScheduledIndex);
        await timer.DisposeAsync();
    }

    [Fact]
    public async Task Timer_freeze_keeps_tick_pending_when_dispatch_is_suppressed_after_freeze()
    {
        var tickStarted = new TaskCompletionSource<ZLinkTimerTick>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseDispatch = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var dispatchReturned = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var timer = new ZLinkTimer(
            new ZLinkTimerLogicalSnapshot(
                "suppressed",
                TimeSpan.FromMilliseconds(1),
                new ZLinkTimerOptions(),
                DateTimeOffset.UtcNow,
                0,
                0,
                null,
                null),
            CancellationToken.None,
            async (value, _) =>
            {
                tickStarted.TrySetResult(value);
                await releaseDispatch.Task.ConfigureAwait(false);
                dispatchReturned.TrySetResult();
                return false;
            },
            static (_, _, _, _) => ValueTask.CompletedTask);

        var started = await tickStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var frozen = timer.Freeze();
        Assert.Equal(started, frozen.PendingTick);

        releaseDispatch.TrySetResult();
        await dispatchReturned.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await Task.Delay(20);
        var suppressed = timer.Snapshot();
        Assert.Equal(started, suppressed.PendingTick);
        Assert.Equal<ulong>(0, suppressed.DeliveryIndex);
        await timer.DisposeAsync();
    }

    [Fact]
    public async Task Concurrent_cancel_and_dispose_wait_for_the_same_blocked_timer_pump()
    {
        var tickStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseTick = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var timer = new ZLinkTimer(
            "blocked",
            TimeSpan.FromMilliseconds(1),
            new ZLinkTimerOptions(),
            CancellationToken.None,
            async (_, _) =>
            {
                tickStarted.TrySetResult();
                await releaseTick.Task.ConfigureAwait(false);
            },
            static (_, _, _, _) => ValueTask.CompletedTask);

        await tickStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var cancel = timer.CancelAsync().AsTask();
        var dispose = timer.DisposeAsync().AsTask();
        Assert.False(cancel.IsCompleted);
        Assert.False(dispose.IsCompleted);
        Assert.True(timer.IsDisposed);

        releaseTick.TrySetResult();
        await Task.WhenAll(cancel, dispose).WaitAsync(TimeSpan.FromSeconds(5));

        await timer.CancelAsync();
        await timer.DisposeAsync();
    }

    [Fact]
    public async Task Concurrent_cancel_callers_observe_the_same_cleanup_failure_after_pump_completion()
    {
        var tickStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseTick = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var timer = new ZLinkTimer(
            "failing-cancel",
            TimeSpan.FromMilliseconds(1),
            new ZLinkTimerOptions(),
            CancellationToken.None,
            async (_, cancellationToken) =>
            {
                using var registration = cancellationToken.Register(
                    static () => throw new InvalidOperationException("cancel callback failed"));
                tickStarted.TrySetResult();
                await releaseTick.Task.ConfigureAwait(false);
            },
            static (_, _, _, _) => ValueTask.CompletedTask);
        await tickStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var first = timer.CancelAsync().AsTask();
        var second = timer.CancelAsync().AsTask();
        Assert.False(first.IsCompleted);
        Assert.False(second.IsCompleted);

        releaseTick.TrySetResult();
        var firstFailure = await Assert.ThrowsAsync<AggregateException>(() => first);
        var secondFailure = await Assert.ThrowsAsync<AggregateException>(() => second);
        Assert.Contains(firstFailure.InnerExceptions, static error => error is InvalidOperationException);
        Assert.Same(firstFailure, secondFailure);
    }

    [Fact]
    public async Task Registry_close_rejects_new_timers_and_finalizes_every_admitted_timer()
    {
        var registry = new ZLinkSpotTimerRegistry(static () => false);
        var admitted = new List<IZLinkTimer>();
        for (var index = 0; index < 32; index++)
        {
            admitted.Add(await AddTimerAsync(registry, $"timer-{index}"));
        }

        var first = registry.DisposeAsync().AsTask();
        var second = registry.DisposeAsync().AsTask();
        await Task.WhenAll(first, second).WaitAsync(TimeSpan.FromSeconds(5));

        Assert.All(admitted, static timer => Assert.True(timer.IsDisposed));
        await Assert.ThrowsAsync<ObjectDisposedException>(
            async () => await AddTimerAsync(registry, "after-close"));
    }

    [Fact]
    public async Task Registry_relocation_roundtrip_restores_registration_and_cursor_frozen()
    {
        var source = new ZLinkSpotTimerRegistry(static () => false);
        await AddTimerAsync(source, "logical");

        var relocation = source.FreezeRelocation();
        Assert.Single(relocation);
        var sourceSnapshot = ZLinkSpotTimerRelocationCodec.Decode(relocation[0]);
        Assert.Equal(typeof(TestTimerHandler), sourceSnapshot.HandlerType);
        Assert.Equal(typeof(TestTimerSpot), sourceSnapshot.SpotType);

        var target = new ZLinkSpotTimerRegistry(static () => false);
        target.RestoreRelocation(
            relocation,
            CancellationToken.None,
            static (_, _, _) => ValueTask.FromResult(true),
            static (_, _, _, _, _) => ValueTask.CompletedTask);
        var restored = target.FreezeRelocation();
        var targetSnapshot = ZLinkSpotTimerRelocationCodec.Decode(
            Assert.Single(restored));

        Assert.Equal(sourceSnapshot.Timer.Name, targetSnapshot.Timer.Name);
        Assert.Equal(sourceSnapshot.Timer.Period, targetSnapshot.Timer.Period);
        Assert.Equal(
            sourceSnapshot.Timer.DeliveryIndex,
            targetSnapshot.Timer.DeliveryIndex);
        Assert.Equal(
            sourceSnapshot.Timer.LastScheduledIndex,
            targetSnapshot.Timer.LastScheduledIndex);

        source.Resume();
        target.Resume();
        await source.DisposeAsync();
        await target.DisposeAsync();
    }

    [Fact]
    public async Task Add_timer_racing_registry_dispose_is_either_rejected_or_fully_finalized()
    {
        for (var iteration = 0; iteration < 100; iteration++)
        {
            var registry = new ZLinkSpotTimerRegistry(static () => false);
            var start = new ManualResetEventSlim();
            var add = Task.Run(async () =>
            {
                start.Wait();
                try
                {
                    return await AddTimerAsync(registry, $"race-{iteration}");
                }
                catch (ObjectDisposedException)
                {
                    return null;
                }
            });
            var dispose = Task.Run(async () =>
            {
                start.Wait();
                await registry.DisposeAsync();
            });

            start.Set();
            await Task.WhenAll(add, dispose).WaitAsync(TimeSpan.FromSeconds(5));
            if (await add is { } timer) Assert.True(timer.IsDisposed);
            await registry.DisposeAsync();
        }
    }

    private static ValueTask<IZLinkTimer> AddTimerAsync(
        ZLinkSpotTimerRegistry registry,
        string name)
    {
        return registry.AddAsync(
            name,
            TimeSpan.FromHours(1),
            null,
            typeof(TestTimerHandler),
            typeof(TestTimerSpot),
            CancellationToken.None,
            static (_, _, _) => ValueTask.FromResult(true),
            static (_, _, _, _, _) => ValueTask.CompletedTask,
            CancellationToken.None);
    }

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        var timeoutAt = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(5);
        while (!condition())
        {
            if (DateTimeOffset.UtcNow >= timeoutAt)
                throw new TimeoutException("The expected timer state was not reached.");
            await Task.Delay(10);
        }
    }

    private sealed class TestTimerSpot;

    private sealed class TestTimerHandler : IZLinkSpotTimerHandler<TestTimerSpot>
    {
        public ValueTask HandleAsync(
            TestTimerSpot spot,
            ZLinkTimerTick tick,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }
}
