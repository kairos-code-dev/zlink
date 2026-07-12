using Zlink.Framework.Runtime.Spots;
using Zlink.Framework.Runtime.Timers;

namespace Zlink.Framework.UnitTests;

public sealed class TimerLifecycleTests
{
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
            static (_, _, _) => ValueTask.CompletedTask,
            static (_, _, _, _, _) => ValueTask.CompletedTask,
            CancellationToken.None);
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
