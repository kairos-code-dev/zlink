namespace Zlink.Framework.Runtime.Execution;

internal sealed class ZLinkRuntimeTaskRunner
{
    private readonly IZLinkRuntimeErrorSink _errorSink;
    private readonly CancellationToken _shutdownToken;

    public ZLinkRuntimeTaskRunner(
        IZLinkRuntimeErrorSink errorSink,
        CancellationToken shutdownToken)
    {
        _errorSink = errorSink;
        _shutdownToken = shutdownToken;
    }

    public void RunDetached(
        string name,
        Func<CancellationToken, ValueTask> callback)
    {
        _ = Run(name, callback);
    }

    public Task Run(
        string name,
        Func<CancellationToken, ValueTask> callback)
    {
        return Task.Factory.StartNew(
            static state => RunDetachedCoreAsync((TaskState)state!),
            new TaskState(name, callback, _errorSink, _shutdownToken),
            CancellationToken.None,
            TaskCreationOptions.DenyChildAttach,
            TaskScheduler.Default).Unwrap();
    }

    private static async Task RunDetachedCoreAsync(TaskState state)
    {
        try
        {
            await state.Callback(state.ShutdownToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (state.ShutdownToken.IsCancellationRequested)
        {
        }
        catch (Exception ex)
        {
            try
            {
                state.ErrorSink.ReportRuntimeTaskException(state.Name, ex);
            }
            catch
            {
            }
        }
    }

    public void ReportErrorSinkFailure(
        string name,
        Exception exception)
    {
        ZLinkFrameworkDebugLog.TaskFailure(name, exception);
        ZLinkRuntimeErrorSink.ReportUnhandledCallbackException(exception);
    }

    private sealed record TaskState(
        string Name,
        Func<CancellationToken, ValueTask> Callback,
        IZLinkRuntimeErrorSink ErrorSink,
        CancellationToken ShutdownToken);
}
