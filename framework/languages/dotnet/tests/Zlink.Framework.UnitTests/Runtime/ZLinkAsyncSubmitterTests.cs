namespace Zlink.Framework.UnitTests;

public sealed class ZLinkAsyncSubmitterTests
{
    [Fact]
    public async Task Async_DrainsPendingItemFromReadyCallback()
    {
        Action? ready = null;
        var writable = false;
        var submitted = 0;

        await using var submitter = new ZLinkAsyncSubmitter(
            handler => ready = handler,
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        var task = submitter.Async(
            Message.From("payload"),
            _ =>
            {
                submitted++;
                return writable;
            });

        Assert.False(task.IsCompleted);
        Assert.Equal(2, submitted);

        writable = true;
        ready?.Invoke();
        await task;

        Assert.Equal(3, submitted);
    }

    [Fact]
    public async Task Async_RetriesAfterQueueingToCloseReadyRace()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None);
        var submitted = 0;

        await submitter.Async(
            Message.From("payload"),
            _ =>
            {
                submitted++;
                return submitted == 2;
            });

        Assert.Equal(2, submitted);
    }

    [Fact]
    public async Task Async_RetriesWithFreshMessageCopies()
    {
        Action? ready = null;
        var submitted = 0;

        await using var submitter = new ZLinkAsyncSubmitter(
            handler => ready = handler,
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        var task = submitter.Async(
            Message.From("payload"),
            parts =>
            {
                submitted++;
                using var moved = parts[0].Move();
                return submitted == 3;
            });

        Assert.False(task.IsCompleted);
        Assert.Equal(2, submitted);

        ready?.Invoke();
        await task;

        Assert.Equal(3, submitted);
    }

    [Fact]
    public async Task Async_FailsPendingItemWhenSendTimeoutExpires()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromMilliseconds(20),
            CancellationToken.None);

        var task = submitter.Async(
            Message.From("payload"),
            _ => false);

        await Assert.ThrowsAsync<TimeoutException>(async () => await task.AsTask());
    }

    [Fact]
    public async Task Async_ThrowsWhenQueueIsFull()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None,
            capacity: 1);

        var first = submitter.Async(
            Message.From("first"),
            _ => false);

        Assert.False(first.IsCompleted);

        Assert.Throws<InvalidOperationException>(() =>
            submitter.Async(
                Message.From("second"),
                _ => false));
    }

    [Fact]
    public async Task DisposeAsync_FailsPendingItems()
    {
        var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        var pending = submitter.Async(
            Message.From("payload"),
            _ => false);

        await submitter.DisposeAsync();

        await Assert.ThrowsAsync<ObjectDisposedException>(async () => await pending.AsTask());
    }
}
