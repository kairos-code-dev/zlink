namespace Zlink.Framework.Runtime.Core;

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

    public async ValueTask InvokeAsync(
        Action<Exception> onUnhandledException,
        CancellationToken cancellationToken)
    {
        try
        {
            await _callback(cancellationToken).ConfigureAwait(false);
            _completion.TrySetResult();
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            _completion.TrySetCanceled(cancellationToken);
        }
        catch (Exception ex)
        {
            _completion.TrySetException(ex);
            _ = _completion.Task.Exception;
            onUnhandledException(ex);
        }
    }
}
