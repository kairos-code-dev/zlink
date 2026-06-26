namespace Zlink.Framework.Runtime.Execution;

internal sealed class ZLinkSerialWorkItem
{
    private readonly Func<CancellationToken, ValueTask> _callback;
    private readonly TaskCompletionSource _completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public ZLinkSerialWorkItem(Func<CancellationToken, ValueTask> callback)
    {
        _callback = callback;
    }

    public Task Completion => _completion.Task;

    public async ValueTask<ZLinkSerialWorkItemResult> InvokeAsync(
        Action<Exception> onUnhandledException,
        CancellationToken cancellationToken,
        ZLinkSerialTurn turn)
    {
        try
        {
            using var turnScope = ZLinkSerialTurn.Push(turn);
            var operation = _callback(cancellationToken);
            if (operation.IsCompletedSuccessfully)
            {
                _completion.TrySetResult();
                return ZLinkSerialWorkItemResult.Completed;
            }

            var task = operation.AsTask();
            turn.BindOwnerTask(task);
            var completed = await Task.WhenAny(task, turn.Suspended).ConfigureAwait(false);
            if (ReferenceEquals(completed, turn.Suspended))
            {
                _ = CompleteAfterSuspensionAsync(task, onUnhandledException);
                return ZLinkSerialWorkItemResult.Suspended;
            }

            await task.ConfigureAwait(false);
            _completion.TrySetResult();
            return ZLinkSerialWorkItemResult.Completed;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            _completion.TrySetCanceled(cancellationToken);
            return ZLinkSerialWorkItemResult.Completed;
        }
        catch (Exception ex)
        {
            _completion.TrySetException(ex);
            _ = _completion.Task.Exception;
            onUnhandledException(ex);
            return ZLinkSerialWorkItemResult.Completed;
        }
    }

    private async Task CompleteAfterSuspensionAsync(
        Task task,
        Action<Exception> onUnhandledException)
    {
        try
        {
            await task.ConfigureAwait(false);
            _completion.TrySetResult();
        }
        catch (Exception ex)
        {
            _completion.TrySetException(ex);
            _ = _completion.Task.Exception;
            if (ex is not OperationCanceledException)
            {
                onUnhandledException(ex);
            }
        }
    }
}

internal enum ZLinkSerialWorkItemResult
{
    Completed,
    Suspended,
}
