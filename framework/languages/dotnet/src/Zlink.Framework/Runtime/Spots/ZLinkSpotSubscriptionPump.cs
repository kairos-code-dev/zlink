namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotSubscriptionPump
{
    private static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(20);
    private Task? _task;
    private Exception? _lastError;

    public string State
    {
        get
        {
            var task = _task;
            if (task is null)
            {
                return "null";
            }

            if (task.IsFaulted)
            {
                return task.Exception?.GetBaseException().ToString() ?? "faulted";
            }

            if (task.IsCanceled)
            {
                return "canceled";
            }

            if (task.IsCompleted && _lastError is not null)
            {
                return _lastError.ToString();
            }

            return task.IsCompleted ? "completed" : "running";
        }
    }

    public void StartIfNeeded(
        bool hasSubscriptions,
        CancellationToken cancellationToken,
        Func<CancellationToken, ValueTask> dispatchAsync)
    {
        if (!hasSubscriptions || _task is not null)
        {
            return;
        }

        var taskRunner = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
            cancellationToken);
        _task = taskRunner.Run(
            "spot-subscription-pump",
            ct => RunAsync(ct, dispatchAsync));
    }

    public async ValueTask StopAsync()
    {
        if (_task is null)
        {
            return;
        }

        try
        {
            await _task.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }
        catch (ObjectDisposedException)
        {
        }
    }

    private async ValueTask RunAsync(
        CancellationToken cancellationToken,
        Func<CancellationToken, ValueTask> dispatchAsync)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                await dispatchAsync(cancellationToken).ConfigureAwait(false);
                await Task.Delay(PollInterval, cancellationToken);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (ZlinkRecvException ex)
                when (cancellationToken.IsCancellationRequested
                      || ex.Result is ZlinkRecvException.ErrorCode.InternalError
                      or ZlinkRecvException.ErrorCode.InvalidHandle)
            {
                return;
            }
            catch (Exception ex)
            {
                _lastError = ex;
                throw;
            }
        }
    }
}
