namespace Zlink.Framework.Tests;

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
        Assert.Equal(1, submitted);

        writable = true;
        ready?.Invoke();
        await task;

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
}
