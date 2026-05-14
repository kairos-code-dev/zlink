namespace Zlink.Framework.Contracts.Timers;

public interface IZLinkTimer : IAsyncDisposable
{
    bool IsDisposed { get; }

    ValueTask CancelAsync(CancellationToken cancellationToken = default);
}
