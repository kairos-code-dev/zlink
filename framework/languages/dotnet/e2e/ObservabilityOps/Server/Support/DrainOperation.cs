using ObservabilityOps.Shared;
using Zlink.Framework.Contracts.Configuration;

namespace ObservabilityOps.Server.Support;

public sealed class DrainOperation
{
    private readonly object _gate = new();
    private Task<ZLinkDrainResult>? _task;
    private Exception? _error;
    private int _terminalCount;

    public DrainStatus Start(IZLinkDrainControl drain, TimeSpan deadline)
    {
        lock (_gate)
            _task ??= RunAsync(drain, deadline);
        return Snapshot();
    }

    public DrainStatus Snapshot()
    {
        lock (_gate)
        {
            var result = _task is { Status: TaskStatus.RanToCompletion } ? _task.Result : null;
            return new DrainStatus(
                _task is not null,
                _task?.IsCompleted ?? false,
                result?.GetType().Name,
                (result as ForceStopped)?.Reason.ToString(),
                _error?.Message,
                Volatile.Read(ref _terminalCount));
        }
    }

    public async Task<DrainStatus> WaitAsync(TimeSpan timeout, CancellationToken cancellationToken)
    {
        Task<ZLinkDrainResult> task;
        lock (_gate) task = _task ?? throw new InvalidOperationException("Drain has not started.");
        await task.WaitAsync(timeout, cancellationToken);
        return Snapshot();
    }

    private async Task<ZLinkDrainResult> RunAsync(IZLinkDrainControl drain, TimeSpan deadline)
    {
        try
        {
            return await drain.DrainAsync(deadline, CancellationToken.None);
        }
        catch (Exception exception)
        {
            lock (_gate) _error = exception;
            throw;
        }
        finally
        {
            Interlocked.Increment(ref _terminalCount);
        }
    }
}
