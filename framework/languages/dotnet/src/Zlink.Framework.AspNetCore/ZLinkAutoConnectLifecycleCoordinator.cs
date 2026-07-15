namespace Zlink.Framework.AspNetCore;

/// <summary>
/// Owns auto-connect generation start and stop. Socket monitoring is an
/// optional startup barrier: when configured, monitor callbacks must be
/// attached before the generation can dial or publish location peers.
/// </summary>
internal sealed class ZLinkAutoConnectLifecycleCoordinator
{
    private readonly object _gate = new();
    private readonly Func<ZLinkFrameworkRuntimeState, CancellationToken, ValueTask>? _start;
    private readonly Func<CancellationToken, ValueTask>? _stop;
    private readonly bool _requiresSocketMonitoring;
    private ZLinkFrameworkRuntimeState? _state;
    private Task? _startTask;
    private Task? _stopTask;
    private bool _socketMonitoringReady;

    internal ZLinkAutoConnectLifecycleCoordinator(
        ZLinkLocationAutoConnectHost? autoConnect,
        bool requiresSocketMonitoring)
        : this(
            autoConnect is null ? null : autoConnect.StartAsync,
            autoConnect is null ? null : autoConnect.StopAsync,
            requiresSocketMonitoring)
    {
    }

    internal ZLinkAutoConnectLifecycleCoordinator(
        Func<ZLinkFrameworkRuntimeState, CancellationToken, ValueTask>? start,
        Func<CancellationToken, ValueTask>? stop,
        bool requiresSocketMonitoring)
    {
        _start = start;
        _stop = stop;
        _requiresSocketMonitoring = requiresSocketMonitoring;
    }

    internal Task FrameworkReadyAsync(
        ZLinkFrameworkRuntimeState state,
        CancellationToken cancellationToken)
    {
        lock (_gate)
        {
            _state = state;
            return TryStartUnderLock(cancellationToken) ?? Task.CompletedTask;
        }
    }

    internal Task SocketMonitoringReadyAsync(CancellationToken cancellationToken)
    {
        lock (_gate)
        {
            _socketMonitoringReady = true;
            return TryStartUnderLock(cancellationToken) ?? Task.CompletedTask;
        }
    }

    internal ValueTask StopAsync(CancellationToken cancellationToken)
    {
        lock (_gate)
            return new ValueTask(
                _stopTask ??= StopCoreAsync(_startTask, cancellationToken));
    }

    private async Task StopCoreAsync(Task? startTask, CancellationToken cancellationToken)
    {
        Exception? startFailure = null;
        if (startTask is not null)
            try
            {
                await startTask.ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                startFailure = exception;
            }

        Exception? stopFailure = null;
        if (startTask is not null && _stop is { } stop)
            try
            {
                await stop(cancellationToken).ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                stopFailure = exception;
            }

        if (startFailure is not null && stopFailure is not null)
            throw new AggregateException(startFailure, stopFailure);
        if (startFailure is not null)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(startFailure).Throw();
        if (stopFailure is not null)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(stopFailure).Throw();
    }

    private Task? TryStartUnderLock(CancellationToken cancellationToken)
    {
        if (_startTask is not null
            || _stopTask is not null
            || _start is null
            || _state is null
            || (_requiresSocketMonitoring && !_socketMonitoringReady))
            return _startTask;

        _startTask = _start(_state, cancellationToken).AsTask();
        return _startTask;
    }
}
