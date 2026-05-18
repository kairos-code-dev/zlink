using System.Diagnostics;
using System.Security.Authentication;

namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamConnectorLifecycle(
    ZlinkStreamConnectorOptions options,
    ZlinkStreamPendingRequests pending,
    ZlinkStreamTaskRunner taskRunner,
    ZlinkStreamConnectorCallbacks callbacks)
    : IDisposable
{
    private readonly object _gate = new();
    private readonly CancellationTokenSource _closeCts = new();
    private IZlinkStreamConnection? _connection;
    private CancellationTokenSource? _sessionCts;
    private Task? _receiveTask;
    private Task? _heartbeatTask;
    private Task? _activeConnectTask;
    private Func<CancellationToken, Task>? _runReceiveLoop;
    private Func<CancellationToken, ValueTask>? _sendHeartbeatPing;
    private ZlinkStreamConnectionState _state = ZlinkStreamConnectionState.Created;
    private long _lastInboundTicks;
    private long _lastPingTicks;

    public IZlinkStreamConnection? Connection
    {
        get
        {
            lock (_gate)
            {
                return _connection;
            }
        }
    }

    public ZlinkStreamConnectionState State
    {
        get
        {
            lock (_gate)
            {
                return _state;
            }
        }
    }

    public bool IsConnected => State == ZlinkStreamConnectionState.Connected;

    public async ValueTask ConnectAsync(
        Func<CancellationToken, Task> runReceiveLoop,
        Func<CancellationToken, ValueTask> sendHeartbeatPing,
        Action throwIfDisposed,
        CancellationToken cancellationToken)
    {
        Task? waitTask;
        ZlinkStreamConnectionStateChanged? change = null;
        lock (_gate)
        {
            throwIfDisposed();
            if (_state == ZlinkStreamConnectionState.Closed)
            {
                throw new ObjectDisposedException(nameof(ZlinkStreamConnector), "Connector is closed.");
            }

            _runReceiveLoop = runReceiveLoop;
            _sendHeartbeatPing = sendHeartbeatPing;

            if (_state == ZlinkStreamConnectionState.Connected)
            {
                return;
            }

            if (_activeConnectTask is not null)
            {
                waitTask = _activeConnectTask;
            }
            else
            {
                change = SetStateLocked(ZlinkStreamConnectionState.Connecting, null);
                waitTask = ConnectOnceAsync(cancellationToken);
                _activeConnectTask = waitTask;
                ObserveBackgroundTask(waitTask);
            }
        }

        await NotifyStateChangedAsync(change, cancellationToken).ConfigureAwait(false);
        try
        {
            await waitTask.WaitAsync(cancellationToken).ConfigureAwait(false);
        }
        catch (Exception) when (State == ZlinkStreamConnectionState.Closed)
        {
            throw new ObjectDisposedException(nameof(ZlinkStreamConnector), "Connector is closed.");
        }

        if (State == ZlinkStreamConnectionState.Closed)
        {
            throw new ObjectDisposedException(nameof(ZlinkStreamConnector), "Connector is closed.");
        }
    }

    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        LifecycleSnapshot snapshot;
        Task? activeConnectTask;
        ZlinkStreamConnectionStateChanged? change;
        lock (_gate)
        {
            if (_state == ZlinkStreamConnectionState.Closed)
            {
                return;
            }

            _closeCts.Cancel();
            snapshot = DetachLocked();
            activeConnectTask = _activeConnectTask;
            _activeConnectTask = null;
            change = SetStateLocked(ZlinkStreamConnectionState.Closed, null);
        }

        snapshot.SessionCts?.Cancel();
        await CloseConnectionAsync(snapshot.Connection, cancellationToken).ConfigureAwait(false);
        pending.FailAll(new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connector closed."));
        await WaitBackgroundTaskAsync(snapshot.ReceiveTask).ConfigureAwait(false);
        await WaitBackgroundTaskAsync(snapshot.HeartbeatTask).ConfigureAwait(false);
        await WaitBackgroundTaskAsync(activeConnectTask).ConfigureAwait(false);
        snapshot.SessionCts?.Dispose();

        await NotifyStateChangedAsync(change, cancellationToken).ConfigureAwait(false);
        if (snapshot.Connection is not null)
        {
            await callbacks.NotifyDisconnectedAsync(cancellationToken).ConfigureAwait(false);
        }
    }

    public void RecordInbound()
    {
        var now = Stopwatch.GetTimestamp();
        Interlocked.Exchange(ref _lastInboundTicks, now);
    }

    public async ValueTask HandleTransportErrorAsync(ZlinkStreamError error, CancellationToken cancellationToken = default)
    {
        await callbacks.PublishErrorAsync(error, cancellationToken).ConfigureAwait(false);
        await HandleDisconnectAsync(error, cancellationToken).ConfigureAwait(false);
    }

    public void Dispose()
    {
        _closeCts.Dispose();
        _sessionCts?.Dispose();
    }

    private async Task ConnectOnceAsync(CancellationToken cancellationToken)
    {
        try
        {
            var connection = await OpenConnectionAsync(cancellationToken).ConfigureAwait(false);
            await AttachConnectionAsync(connection, cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            await TransitionToDisconnectedAsync(
                new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connect canceled."),
                CancellationToken.None).ConfigureAwait(false);
            throw;
        }
        catch (Exception ex) when (ex is not ZlinkStreamException)
        {
            var error = MapConnectException(ex, cancellationToken);
            await TransitionToDisconnectedAsync(error, cancellationToken).ConfigureAwait(false);
            throw new ZlinkStreamException(error);
        }
        catch (ZlinkStreamException ex)
        {
            await TransitionToDisconnectedAsync(ex.Error, cancellationToken).ConfigureAwait(false);
            throw;
        }
        finally
        {
            ClearActiveConnectTask(null);
        }
    }

    private async Task ReconnectLoopAsync()
    {
        var reconnect = options.Reconnect;
        if (reconnect is null)
        {
            return;
        }

        var delay = reconnect.InitialDelay <= reconnect.MaxDelay
            ? reconnect.InitialDelay
            : reconnect.MaxDelay;
        var attempt = 0;
        ZlinkStreamError? lastError = null;

        try
        {
            while (!_closeCts.IsCancellationRequested)
            {
                await Task.Delay(delay, _closeCts.Token).ConfigureAwait(false);
                attempt++;

                try
                {
                    var connection = await OpenConnectionAsync(_closeCts.Token).ConfigureAwait(false);
                    await AttachConnectionAsync(connection, _closeCts.Token).ConfigureAwait(false);
                    return;
                }
                catch (Exception ex) when (!_closeCts.IsCancellationRequested)
                {
                    lastError = ex is ZlinkStreamException streamException
                        ? streamException.Error
                        : MapConnectException(ex, _closeCts.Token);
                    await callbacks.PublishErrorAsync(lastError, CancellationToken.None).ConfigureAwait(false);

                    if (reconnect.MaxAttempts is { } maxAttempts && attempt >= maxAttempts)
                    {
                        await TransitionToDisconnectedAsync(lastError, CancellationToken.None).ConfigureAwait(false);
                        throw new ZlinkStreamException(lastError);
                    }

                    delay = NextReconnectDelay(delay, reconnect);
                }
            }
        }
        catch (OperationCanceledException) when (_closeCts.IsCancellationRequested)
        {
        }
        finally
        {
            ClearActiveConnectTask(null);
        }
    }

    private async ValueTask<IZlinkStreamConnection> OpenConnectionAsync(CancellationToken cancellationToken)
    {
        using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, _closeCts.Token);
        timeoutCts.CancelAfter(options.ConnectTimeout);

        try
        {
            return await ZlinkStreamTransportFactory
                .ConnectAsync(options, timeoutCts.Token)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException ex) when (!cancellationToken.IsCancellationRequested && !_closeCts.IsCancellationRequested)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.ConnectTimeout, "Connect timed out.", ex);
        }
        catch (AuthenticationException ex)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.TlsValidationFailed, "TLS validation failed.", ex);
        }
    }

    private async ValueTask AttachConnectionAsync(IZlinkStreamConnection connection, CancellationToken cancellationToken)
    {
        var runReceiveLoop = _runReceiveLoop
            ?? throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.ConfigurationError, "Receive loop is not configured.");
        var sessionCts = CancellationTokenSource.CreateLinkedTokenSource(_closeCts.Token);
        var now = Stopwatch.GetTimestamp();
        Interlocked.Exchange(ref _lastInboundTicks, now);
        Interlocked.Exchange(ref _lastPingTicks, now);

        var receiveTask = taskRunner.Run(
            "stream-receive-loop",
            _ => new ValueTask(RunReceiveLoopGuardedAsync(runReceiveLoop, sessionCts.Token)));
        var heartbeatTask = options.Heartbeat is null
            ? null
            : taskRunner.Run(
                "stream-heartbeat-loop",
                _ => new ValueTask(RunHeartbeatLoopAsync(sessionCts.Token)));

        LifecycleSnapshot oldSnapshot;
        ZlinkStreamConnectionStateChanged? change;
        lock (_gate)
        {
            oldSnapshot = DetachLocked();
            _connection = connection;
            _sessionCts = sessionCts;
            _receiveTask = receiveTask;
            _heartbeatTask = heartbeatTask;
            change = SetStateLocked(ZlinkStreamConnectionState.Connected, null);
        }

        oldSnapshot.SessionCts?.Cancel();
        await CloseConnectionAsync(oldSnapshot.Connection, cancellationToken).ConfigureAwait(false);
        oldSnapshot.SessionCts?.Dispose();
        await NotifyStateChangedAsync(change, cancellationToken).ConfigureAwait(false);
    }

    private async Task RunReceiveLoopGuardedAsync(
        Func<CancellationToken, Task> runReceiveLoop,
        CancellationToken cancellationToken)
    {
        try
        {
            await runReceiveLoop(cancellationToken).ConfigureAwait(false);
            if (!cancellationToken.IsCancellationRequested)
            {
                await HandleDisconnectAsync(
                    new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connector disconnected."),
                    CancellationToken.None).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception ex)
        {
            var error = ex is ZlinkStreamException streamException
                ? streamException.Error
                : new ZlinkStreamError(ZlinkStreamErrorCode.FrameDecodeFailed, "Receive loop failed.", ex);
            await HandleTransportErrorAsync(error, CancellationToken.None).ConfigureAwait(false);
        }
    }

    private async Task RunHeartbeatLoopAsync(CancellationToken cancellationToken)
    {
        var heartbeat = options.Heartbeat;
        var sendHeartbeatPing = _sendHeartbeatPing;
        if (heartbeat is null || sendHeartbeatPing is null)
        {
            return;
        }

        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(heartbeat.Interval, cancellationToken).ConfigureAwait(false);

                var now = Stopwatch.GetTimestamp();
                if (Elapsed(now, Interlocked.Read(ref _lastInboundTicks)) >= heartbeat.Timeout)
                {
                    await HandleTransportErrorAsync(
                        new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Heartbeat timed out."),
                        CancellationToken.None).ConfigureAwait(false);
                    return;
                }

                if (Elapsed(now, Interlocked.Read(ref _lastPingTicks)) >= heartbeat.Interval)
                {
                    await sendHeartbeatPing(cancellationToken).ConfigureAwait(false);
                    Interlocked.Exchange(ref _lastPingTicks, Stopwatch.GetTimestamp());
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception ex)
        {
            var error = ex is ZlinkStreamException streamException
                ? streamException.Error
                : new ZlinkStreamError(ZlinkStreamErrorCode.SendFailed, "Heartbeat send failed.", ex);
            await HandleTransportErrorAsync(error, CancellationToken.None).ConfigureAwait(false);
        }
    }

    private async ValueTask HandleDisconnectAsync(ZlinkStreamError error, CancellationToken cancellationToken)
    {
        LifecycleSnapshot snapshot;
        ZlinkStreamConnectionStateChanged? change;
        Task? reconnectTask = null;
        lock (_gate)
        {
            if (_state is ZlinkStreamConnectionState.Closed or ZlinkStreamConnectionState.Disconnected)
            {
                return;
            }

            snapshot = DetachLocked();
            var nextState = options.Reconnect is null
                ? ZlinkStreamConnectionState.Disconnected
                : ZlinkStreamConnectionState.Reconnecting;
            change = SetStateLocked(nextState, error);

            if (nextState == ZlinkStreamConnectionState.Reconnecting && _activeConnectTask is null)
            {
                reconnectTask = ReconnectLoopAsync();
                _activeConnectTask = reconnectTask;
                ObserveBackgroundTask(reconnectTask);
            }
        }

        snapshot.SessionCts?.Cancel();
        await CloseConnectionAsync(snapshot.Connection, cancellationToken).ConfigureAwait(false);
        snapshot.SessionCts?.Dispose();
        await NotifyStateChangedAsync(change, cancellationToken).ConfigureAwait(false);
        pending.FailAll(GetPendingDisconnectError(error));
        await callbacks.NotifyDisconnectedAsync(cancellationToken).ConfigureAwait(false);

        _ = reconnectTask;
    }

    private async ValueTask TransitionToDisconnectedAsync(ZlinkStreamError error, CancellationToken cancellationToken)
    {
        ZlinkStreamConnectionStateChanged? change;
        lock (_gate)
        {
            if (_state == ZlinkStreamConnectionState.Closed)
            {
                return;
            }

            change = SetStateLocked(ZlinkStreamConnectionState.Disconnected, error);
        }

        await NotifyStateChangedAsync(change, cancellationToken).ConfigureAwait(false);
    }

    private LifecycleSnapshot DetachLocked()
    {
        var snapshot = new LifecycleSnapshot(_connection, _sessionCts, _receiveTask, _heartbeatTask);
        _connection = null;
        _sessionCts = null;
        _receiveTask = null;
        _heartbeatTask = null;
        return snapshot;
    }

    private ZlinkStreamConnectionStateChanged? SetStateLocked(
        ZlinkStreamConnectionState next,
        ZlinkStreamError? error)
    {
        if (_state == next)
        {
            return null;
        }

        var previous = _state;
        _state = next;
        return new ZlinkStreamConnectionStateChanged(previous, next, error);
    }

    private async ValueTask NotifyStateChangedAsync(
        ZlinkStreamConnectionStateChanged? change,
        CancellationToken cancellationToken)
    {
        if (change is not null)
        {
            await callbacks.NotifyConnectionStateChangedAsync(change, cancellationToken).ConfigureAwait(false);
        }
    }

    private static async ValueTask CloseConnectionAsync(
        IZlinkStreamConnection? connection,
        CancellationToken cancellationToken)
    {
        if (connection is not null)
        {
            await connection.CloseAsync(cancellationToken).ConfigureAwait(false);
        }
    }

    private static async ValueTask WaitBackgroundTaskAsync(Task? task)
    {
        if (task is null)
        {
            return;
        }

        try
        {
            await task.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }
        catch (ZlinkStreamException)
        {
        }
    }

    private void ClearActiveConnectTask(int? taskId)
    {
        lock (_gate)
        {
            if (_activeConnectTask is null)
            {
                return;
            }

            if (taskId is null || _activeConnectTask.Id == taskId.Value)
            {
                _activeConnectTask = null;
            }
        }
    }

    private static void ObserveBackgroundTask(Task task)
    {
        _ = task.ContinueWith(
            static completed => _ = completed.Exception,
            CancellationToken.None,
            TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }

    private static ZlinkStreamError MapConnectException(Exception ex, CancellationToken cancellationToken)
        => ex switch
        {
            OperationCanceledException canceled when !cancellationToken.IsCancellationRequested =>
                new ZlinkStreamError(ZlinkStreamErrorCode.ConnectTimeout, "Connect timed out.", canceled),
            AuthenticationException authentication =>
                new ZlinkStreamError(ZlinkStreamErrorCode.TlsValidationFailed, "TLS validation failed.", authentication),
            _ => new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connect failed.", ex)
        };

    private static TimeSpan NextReconnectDelay(TimeSpan current, ZlinkStreamReconnectOptions options)
    {
        var nextMilliseconds = current.TotalMilliseconds * options.BackoffFactor;
        if (nextMilliseconds >= options.MaxDelay.TotalMilliseconds)
        {
            return options.MaxDelay;
        }

        return TimeSpan.FromMilliseconds(nextMilliseconds);
    }

    private static TimeSpan Elapsed(long nowTicks, long previousTicks)
        => TimeSpan.FromSeconds((double)(nowTicks - previousTicks) / Stopwatch.Frequency);

    private static ZlinkStreamError GetPendingDisconnectError(ZlinkStreamError cause)
        => cause.Code == ZlinkStreamErrorCode.Disconnected
            ? cause
            : new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connector disconnected.", cause.Exception);

    private readonly record struct LifecycleSnapshot(
        IZlinkStreamConnection? Connection,
        CancellationTokenSource? SessionCts,
        Task? ReceiveTask,
        Task? HeartbeatTask);
}
