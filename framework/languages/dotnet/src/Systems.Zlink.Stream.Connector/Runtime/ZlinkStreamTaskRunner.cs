namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamTaskRunner(CancellationToken shutdownToken)
{
    public Task Run(Func<CancellationToken, ValueTask> callback)
    {
        return Task.Factory.StartNew(
            static state => RunCoreAsync((TaskState)state!),
            new TaskState(callback, shutdownToken),
            CancellationToken.None,
            TaskCreationOptions.DenyChildAttach,
            TaskScheduler.Default).Unwrap();
    }

    public void RunDetached(Func<CancellationToken, ValueTask> callback)
    {
        _ = Run(callback);
    }

    private static async Task RunCoreAsync(TaskState state)
    {
        try
        {
            await state.Callback(state.ShutdownToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (state.ShutdownToken.IsCancellationRequested)
        {
        }
        catch
        {
        }
    }

    private sealed record TaskState(
        Func<CancellationToken, ValueTask> Callback,
        CancellationToken ShutdownToken);
}
