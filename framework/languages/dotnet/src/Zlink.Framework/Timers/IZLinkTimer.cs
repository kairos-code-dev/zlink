namespace Zlink.Framework;

public interface IZLinkTimer : IAsyncDisposable
{
    bool IsDisposed { get; }

    ValueTask CancelAsync(CancellationToken cancellationToken = default);
}
