namespace Zlink.Framework;

internal sealed class ZLinkTimer(global::Zlink.Timer nativeTimer) : IZLinkTimer
{
    private int _disposed;

    public bool IsDisposed => Volatile.Read(ref _disposed) != 0;

    public ValueTask CancelAsync(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;

        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return ValueTask.CompletedTask;
        }

        nativeTimer.Stop();
        return nativeTimer.DisposeAsync();
    }

    public ValueTask DisposeAsync()
    {
        return CancelAsync();
    }
}
