using Zlink.Framework.Runtime.Channels;
using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.UnitTests.Runtime.Dispatch;

public sealed class InboundDispatchBudgetTests
{
    [Fact]
    public async Task Hwm_pauses_only_new_receive_and_resumes_after_terminal_completion()
    {
        var budget = new ZLinkInboundDispatchBudget(100);

        Assert.True(budget.CanStartApplicationReceive);
        budget.Received(120);
        var capacity = budget.WaitForReceiveCapacityAsync(
            CancellationToken.None).AsTask();

        Assert.False(budget.CanStartApplicationReceive);
        Assert.False(capacity.IsCompleted);
        Assert.Equal(
            new ZLinkInboundDispatchBudgetSnapshot(100, 120, 120, 0, true),
            budget.Snapshot());

        budget.HandlerStarted(120);
        Assert.Equal(
            new ZLinkInboundDispatchBudgetSnapshot(100, 120, 0, 120, true),
            budget.Snapshot());

        budget.Completed(120, handlerStarted: true);
        var ownsResumePermit = await capacity;
        budget.CompleteReceiveAttempt(ownsResumePermit);
        Assert.True(budget.CanStartApplicationReceive);
        Assert.Equal(
            new ZLinkInboundDispatchBudgetSnapshot(100, 0, 0, 0, false),
            budget.Snapshot());
    }

    [Fact]
    public void Empty_budget_accepts_one_message_larger_than_hwm()
    {
        var budget = new ZLinkInboundDispatchBudget(64);

        Assert.True(budget.CanStartApplicationReceive);
        budget.Received(1024);

        Assert.False(budget.CanStartApplicationReceive);
        Assert.Equal(1024UL, budget.Snapshot().PendingPayloadBytes);
    }

    [Fact]
    public void Unlimited_budget_never_pauses()
    {
        var budget = new ZLinkInboundDispatchBudget(0);

        budget.Received(ulong.MaxValue);

        Assert.True(budget.CanStartApplicationReceive);
        Assert.False(budget.Snapshot().ApplicationReceivePaused);
        budget.Completed(ulong.MaxValue, handlerStarted: false);
        Assert.Equal(0UL, budget.Snapshot().PendingPayloadBytes);
    }

    [Fact]
    public void Snapshot_preserves_pending_equals_queued_plus_active()
    {
        var budget = new ZLinkInboundDispatchBudget(1000);
        budget.Received(600);
        budget.HandlerStarted(250);

        var snapshot = budget.Snapshot();

        Assert.Equal(
            snapshot.PendingPayloadBytes,
            snapshot.QueuedPayloadBytes + snapshot.ActivePayloadBytes);
        Assert.Equal(350UL, snapshot.QueuedPayloadBytes);
        Assert.Equal(250UL, snapshot.ActivePayloadBytes);
    }

    [Fact]
    public async Task Dispatch_queue_completes_host_byte_accounting_once()
    {
        using var errors = new ZLinkRuntimeErrorSink();
        await using var queue = new ZLinkChannelApplicationDispatchQueue(
            nameof(InboundDispatchBudgetTests),
            errors,
            CancellationToken.None);
        var budget = new ZLinkInboundDispatchBudget(10);
        var release = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        budget.Received(12);
        var capacity = budget.WaitForReceiveCapacityAsync(
            CancellationToken.None).AsTask();
        var posted = await queue.PostAsync(
            async _ => await release.Task.ConfigureAwait(false),
            static () => { },
            budget,
            12,
            CancellationToken.None);
        Assert.True(posted);
        await WaitUntilAsync(() => budget.Snapshot().ActivePayloadBytes == 12);
        Assert.False(capacity.IsCompleted);
        release.TrySetResult();
        var ownsResumePermit = await capacity;
        budget.CompleteReceiveAttempt(ownsResumePermit);
        await WaitUntilAsync(() => budget.PendingPayloadBytes == 0);
    }

    [Fact]
    public async Task Resume_releases_one_blocked_receive_attempt_at_a_time()
    {
        const int waiterCount = 32;
        var budget = new ZLinkInboundDispatchBudget(100);
        var acquired = System.Threading.Channels.Channel.CreateUnbounded<bool>();
        var release = new SemaphoreSlim(0, waiterCount);
        budget.Received(100);

        var waiters = Enumerable.Range(0, waiterCount)
            .Select(async _ =>
            {
                var ownsResumePermit = await budget.WaitForReceiveCapacityAsync(
                    CancellationToken.None);
                await acquired.Writer.WriteAsync(ownsResumePermit);
                await release.WaitAsync();
                budget.CompleteReceiveAttempt(ownsResumePermit);
            })
            .ToArray();
        await Task.Delay(20);

        budget.Completed(100, handlerStarted: false);
        for (var index = 0; index < waiterCount; index++)
        {
            Assert.True(await acquired.Reader.WaitToReadAsync()
                .AsTask().WaitAsync(TimeSpan.FromSeconds(5)));
            Assert.True(acquired.Reader.TryRead(out var ownsResumePermit));
            Assert.True(ownsResumePermit);
            await Task.Delay(5);
            Assert.False(acquired.Reader.TryRead(out _));
            release.Release();
        }

        await Task.WhenAll(waiters);
        Assert.Equal(0UL, budget.PendingPayloadBytes);
    }

    [Fact]
    public async Task Dispatch_queue_shutdown_has_one_reader_and_releases_each_budget_once()
    {
        const int workCount = 16;
        using var errors = new ZLinkRuntimeErrorSink();
        var queue = new ZLinkChannelApplicationDispatchQueue(
            nameof(Dispatch_queue_shutdown_has_one_reader_and_releases_each_budget_once),
            errors,
            CancellationToken.None);
        var budget = new ZLinkInboundDispatchBudget(1024);
        var rejected = 0;
        for (var index = 0; index < workCount; index++)
        {
            budget.Received(1);
            Assert.True(await queue.PostAsync(
                static async token =>
                    await Task.Delay(Timeout.InfiniteTimeSpan, token),
                () => Interlocked.Increment(ref rejected),
                budget,
                1,
                CancellationToken.None));
        }

        await WaitUntilAsync(() => budget.Snapshot().ActivePayloadBytes == 1);
        await queue.DisposeAsync();

        Assert.Equal(workCount - 1, Volatile.Read(ref rejected));
        Assert.Equal(0UL, budget.PendingPayloadBytes);
    }

    private static async Task WaitUntilAsync(Func<bool> predicate)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (!predicate())
        {
            Assert.True(DateTime.UtcNow < deadline);
            await Task.Delay(10);
        }
    }
}
