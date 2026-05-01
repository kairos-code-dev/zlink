using System.Threading.Channels;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionSerialExecutor : IAsyncDisposable
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly Channel<Func<ValueTask>> _queue =
        Channel.CreateUnbounded<Func<ValueTask>>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false
        });
    private readonly Task _pump;

    public ZLinkStreamSessionSerialExecutor()
    {
        _pump = Task.Run(RunAsync);
    }

    public bool Enqueue(Func<ValueTask> work)
    {
        return _queue.Writer.TryWrite(() => ExecuteAsync(work));
    }

    public async ValueTask DisposeAsync()
    {
        _queue.Writer.TryComplete();

        try
        {
            await _pump.ConfigureAwait(false);
        }
        catch (ObjectDisposedException)
        {
        }

        _gate.Dispose();
    }

    private async ValueTask ExecuteAsync(Func<ValueTask> work)
    {
        await _gate.WaitAsync().ConfigureAwait(false);
        try
        {
            await work().ConfigureAwait(false);
        }
        finally
        {
            _gate.Release();
        }
    }

    private async Task RunAsync()
    {
        await foreach (var work in _queue.Reader.ReadAllAsync().ConfigureAwait(false))
        {
            await work().ConfigureAwait(false);
        }
    }
}
