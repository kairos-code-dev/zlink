namespace Zlink.Framework.UnitTests;

public sealed class ZLinkAsyncSubmitterTests
{
    [Fact]
    public async Task SubmitAsync_DrainsPendingItemFromReadyCallback()
    {
        Action? ready = null;
        var writable = false;
        var submitted = 0;

        await using var submitter = new ZLinkAsyncSubmitter(
            handler => ready = handler,
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        var task = submitter.SubmitAsync(
            Message.FromString("payload"),
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
    public async Task SubmitAsync_RetriesAfterQueueingToCloseReadyRace()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None);
        var submitted = 0;

        await submitter.SubmitAsync(
            Message.FromString("payload"),
            _ =>
            {
                submitted++;
                return submitted == 2;
            });

        Assert.Equal(2, submitted);
    }

    [Fact]
    public async Task SubmitAsync_FailsPendingItemWhenSendTimeoutExpires()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromMilliseconds(20),
            CancellationToken.None);

        var task = submitter.SubmitAsync(
            Message.FromString("payload"),
            _ => false);

        await Assert.ThrowsAsync<TimeoutException>(async () => await task.AsTask());
    }

    [Fact]
    public async Task SubmitAsync_ThrowsWhenQueueIsFull()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None,
            capacity: 1);

        var first = submitter.SubmitAsync(
            Message.FromString("first"),
            _ => false);

        Assert.False(first.IsCompleted);

        Assert.Throws<InvalidOperationException>(() =>
            submitter.SubmitAsync(
                Message.FromString("second"),
                _ => false));
    }

    [Fact]
    public async Task DisposeAsync_FailsPendingItems()
    {
        var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        var pending = submitter.SubmitAsync(
            Message.FromString("payload"),
            _ => false);

        await submitter.DisposeAsync();

        await Assert.ThrowsAsync<ObjectDisposedException>(async () => await pending.AsTask());
    }
}
