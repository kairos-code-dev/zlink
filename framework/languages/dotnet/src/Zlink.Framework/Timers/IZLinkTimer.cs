namespace Zlink.Framework.Timers;

public interface IZLinkTimer : IAsyncDisposable
{
    bool IsDisposed { get; }

    ValueTask CancelAsync(CancellationToken cancellationToken = default);
}
