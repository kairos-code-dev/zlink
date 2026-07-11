using Systems.Zlink;

namespace Zlink.Framework.Runtime.Messaging;

internal sealed class ZLinkRequestCompletionPump : IAsyncDisposable
{
    private readonly CancellationTokenSource _stop = new();
    private readonly Task _worker;
    private int _disposed;

    private ZLinkRequestCompletionPump(IZlinkSocket socket)
    {
        _worker = Task.Factory.StartNew(
            () => Run(socket, _stop.Token),
            CancellationToken.None,
            TaskCreationOptions.LongRunning,
            TaskScheduler.Default);
    }

    public static ZLinkRequestCompletionPump Start(IZlinkSocket socket) => new(socket);

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        _stop.Cancel();
        try
        {
            await _worker.ConfigureAwait(false);
        }
        finally
        {
            _stop.Dispose();
        }
    }

    private static void Run(IZlinkSocket socket, CancellationToken stopToken)
    {
        using var poller = Systems.Zlink.Zlink.CreatePoller();
        // Hot path support: the native socket completes async requests only when
        // completion events are drained. A dedicated completion-only poll keeps
        // request latency stable without coupling it to application receive loops.
        poller.Add(socket, PollEventFlags.PollCompletion, 0);
        Span<PollEvent> events = stackalloc PollEvent[1];

        while (!stopToken.IsCancellationRequested)
        {
            if (poller.Wait(events, TimeSpan.Zero) == 0)
                Thread.Sleep(1);
        }
    }
}
