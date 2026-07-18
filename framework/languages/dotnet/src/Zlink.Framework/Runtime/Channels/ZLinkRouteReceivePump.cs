using Zlink.Framework.Runtime.Diagnostics;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteReceivePump(
    IZLinkBackendRouterSocket router,
    ZLinkRoutePacketDispatcher dispatcher,
    IZLinkRuntimeErrorSink errorSink)
{
    private readonly object _dispatchGate = new();
    private readonly HashSet<Task> _dispatchTasks = [];

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        var backoff = new ZLinkPollingBackoff();
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                Received? received = null;
                try
                {
                    received = router.Recv(RecvFlags.DontWait);
                    if (received is null)
                    {
                        await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                        continue;
                    }

                    backoff.Reset();
                    if (IsProbeFrame(received)) continue;

                    DispatchInBackground(received, cancellationToken);
                    received = null;
                }
                catch (Exception) when (cancellationToken.IsCancellationRequested)
                {
                    return;
                }
                catch (ZlinkRecvException ex) when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
                {
                    await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                }
                catch (ObjectDisposedException)
                {
                    return;
                }
                catch (ZlinkRecvException ex)
                    when (cancellationToken.IsCancellationRequested
                          || ex.Result == ZlinkRecvException.ErrorCode.InvalidHandle)
                {
                    return;
                }
                catch (ZlinkRecvException ex)
                    when (ex.Result == ZlinkRecvException.ErrorCode.InternalError)
                {
                    await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                }
                finally
                {
                    received?.Dispose();
                }
            }
        }
        finally
        {
            await WaitForDispatchTasksAsync().ConfigureAwait(false);
        }
    }

    private static bool IsProbeFrame(Received received)
    {
        return received.Parts.Count == 0 || received.Parts[0].Size == 0;
    }

    private void DispatchInBackground(
        Received received,
        CancellationToken cancellationToken)
    {
        var dispatchTask = Task.Run(
            async () =>
            {
                try
                {
                    using (received)
                    {
                        await dispatcher.DispatchAsync(received, cancellationToken).ConfigureAwait(false);
                    }
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                }
                catch (ObjectDisposedException) when (cancellationToken.IsCancellationRequested)
                {
                }
                catch (Exception ex)
                {
                    errorSink.ReportUnhandledCallbackException(ex);
                }
            },
            CancellationToken.None);

        lock (_dispatchGate)
        {
            _dispatchTasks.RemoveWhere(task => task.IsCompleted);
            _dispatchTasks.Add(dispatchTask);
        }
    }

    private async Task WaitForDispatchTasksAsync()
    {
        Task[] tasks;
        lock (_dispatchGate)
        {
            tasks = _dispatchTasks.ToArray();
            _dispatchTasks.Clear();
        }

        if (tasks.Length == 0) return;

        try
        {
            await Task.WhenAll(tasks).ConfigureAwait(false);
        }
        catch
        {
        }
    }
}
