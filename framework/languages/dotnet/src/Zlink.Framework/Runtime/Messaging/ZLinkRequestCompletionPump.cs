using Systems.Zlink;

namespace Zlink.Framework.Runtime.Messaging;

internal sealed class ZLinkRequestCompletionPump : IAsyncDisposable
{
    private static readonly TimeSpan PollTimeout = TimeSpan.FromMilliseconds(10);

    private readonly CancellationTokenSource _stop = new();
    private readonly Task _worker;

    private ZLinkRequestCompletionPump(IZlinkSocket socket)
    {
        _worker = Task.Factory.StartNew(
            () => Run(socket, _stop.Token),
            CancellationToken.None,
            TaskCreationOptions.LongRunning,
            TaskScheduler.Default);
    }

    public static ZLinkRequestCompletionPump? Start(object nativeInstance)
    {
        return nativeInstance is IZlinkSocket socket
            ? new ZLinkRequestCompletionPump(socket)
            : null;
    }

    public async ValueTask DisposeAsync()
    {
        _stop.Cancel();
        try
        {
            await _worker.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }
        catch (TimeoutException)
        {
        }
        finally
        {
            _stop.Dispose();
        }
    }

    private static void Run(IZlinkSocket socket, CancellationToken stopToken)
    {
        using var poller = Systems.Zlink.Zlink.CreatePoller();
        poller.Add(socket, PollEventFlags.PollCompletion, 0);
        Span<PollEvent> events = stackalloc PollEvent[1];

        while (!stopToken.IsCancellationRequested)
        {
            poller.Wait(events, PollTimeout);
        }
    }
}
