using System.Collections.Concurrent;

namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamConnectorCallbacks(
    ZlinkStreamTaskRunner taskRunner,
    ZlinkStreamDispatchMode dispatchMode)
{
    private readonly ConcurrentQueue<QueuedCallback> _dispatchQueue = new();
    private readonly object _gate = new();
    private Func<ZlinkStreamError, CancellationToken, ValueTask>? _errorReceived;
    private Func<CancellationToken, ValueTask>? _disconnected;
    private Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>? _connectionStateChanged;
    private int _pendingDispatchCount;

    public int PendingDispatchCount => Volatile.Read(ref _pendingDispatchCount);

    public void AddErrorReceived(Func<ZlinkStreamError, CancellationToken, ValueTask>? handler)
    {
        lock (_gate)
        {
            _errorReceived += handler;
        }
    }

    public void RemoveErrorReceived(Func<ZlinkStreamError, CancellationToken, ValueTask>? handler)
    {
        lock (_gate)
        {
            _errorReceived -= handler;
        }
    }

    public void AddDisconnected(Func<CancellationToken, ValueTask>? handler)
    {
        lock (_gate)
        {
            _disconnected += handler;
        }
    }

    public void RemoveDisconnected(Func<CancellationToken, ValueTask>? handler)
    {
        lock (_gate)
        {
            _disconnected -= handler;
        }
    }

    public void AddConnectionStateChanged(Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>? handler)
    {
        lock (_gate)
        {
            _connectionStateChanged += handler;
        }
    }

    public void RemoveConnectionStateChanged(Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>? handler)
    {
        lock (_gate)
        {
            _connectionStateChanged -= handler;
        }
    }

    public async ValueTask PublishErrorAsync(ZlinkStreamError error, CancellationToken cancellationToken)
    {
        var handler = SnapshotErrorReceived();
        if (handler is not null)
        {
            await DispatchUserCallbackAsync(
                    dispatchedToken => handler(error, dispatchedToken),
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public async ValueTask NotifyDisconnectedAsync(CancellationToken cancellationToken)
    {
        var disconnected = SnapshotDisconnected();
        if (disconnected is not null)
        {
            await DispatchUserCallbackAsync(
                    disconnected,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public async ValueTask NotifyConnectionStateChangedAsync(
        ZlinkStreamConnectionStateChanged change,
        CancellationToken cancellationToken)
    {
        var handler = SnapshotConnectionStateChanged();
        if (handler is not null)
        {
            await DispatchUserCallbackAsync(
                    dispatchedToken => handler(change, dispatchedToken),
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public async ValueTask DispatchUserCallbackAsync(
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(callback);

        if (dispatchMode == ZlinkStreamDispatchMode.Immediate)
        {
            await InvokeUserCallbackAsync(callback, cancellationToken, reportErrors: true)
                .ConfigureAwait(false);
            return;
        }

        Enqueue(callback, reportErrors: true);
    }

    public async ValueTask DispatchAsync(CancellationToken cancellationToken)
    {
        while (_dispatchQueue.TryDequeue(out var queued))
        {
            Interlocked.Decrement(ref _pendingDispatchCount);
            cancellationToken.ThrowIfCancellationRequested();
            await InvokeUserCallbackAsync(queued.Callback, cancellationToken, queued.ReportErrors)
                .ConfigureAwait(false);
        }
    }

    public void QueueRequestCallback<TResult>(
        Func<ValueTask<ZlinkStreamEncodedPayload>> request,
        Func<ZlinkStreamEncodedPayload, TResult> success,
        Func<ZlinkStreamError, TResult> failure,
        Action<TResult> callback)
    {
        taskRunner.RunDetached(
            "stream-request-callback",
            async _ =>
            {
                try
                {
                    var reply = await request().ConfigureAwait(false);
                    await DispatchUserCallbackAsync(
                            _ =>
                            {
                                callback(success(reply));
                                return ValueTask.CompletedTask;
                            },
                            CancellationToken.None)
                        .ConfigureAwait(false);
                }
                catch (ZlinkStreamException ex)
                {
                    await DispatchUserCallbackAsync(
                            _ =>
                            {
                                callback(failure(ex.Error));
                                return ValueTask.CompletedTask;
                            },
                            CancellationToken.None)
                        .ConfigureAwait(false);
                }
                catch (Exception ex)
                {
                    var error = new ZlinkStreamError(
                        ZlinkStreamErrorCode.SendFailed,
                        ex.Message,
                        ex);
                    await DispatchUserCallbackAsync(
                            _ =>
                            {
                                callback(failure(error));
                                return ValueTask.CompletedTask;
                            },
                            CancellationToken.None)
                        .ConfigureAwait(false);
                }
            });
    }

    private async ValueTask InvokeUserCallbackAsync(
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken,
        bool reportErrors)
    {
        try
        {
            await callback(cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex) when (reportErrors)
        {
            await ReportUserCallbackErrorAsync(ex, cancellationToken).ConfigureAwait(false);
        }
        catch
        {
        }
    }

    private async ValueTask ReportUserCallbackErrorAsync(
        Exception exception,
        CancellationToken cancellationToken)
    {
        var handler = SnapshotErrorReceived();
        if (handler is null)
        {
            return;
        }

        var error = new ZlinkStreamError(
            ZlinkStreamErrorCode.UserCallbackFailed,
            "User callback failed.",
            exception);

        if (dispatchMode == ZlinkStreamDispatchMode.Immediate)
        {
            await InvokeUserCallbackAsync(
                    dispatchedToken => handler(error, dispatchedToken),
                    cancellationToken,
                    reportErrors: false)
                .ConfigureAwait(false);
            return;
        }

        Enqueue(
            dispatchedToken => handler(error, dispatchedToken),
            reportErrors: false);
    }

    private void Enqueue(
        Func<CancellationToken, ValueTask> callback,
        bool reportErrors)
    {
        _dispatchQueue.Enqueue(new QueuedCallback(callback, reportErrors));
        Interlocked.Increment(ref _pendingDispatchCount);
    }

    private Func<ZlinkStreamError, CancellationToken, ValueTask>? SnapshotErrorReceived()
    {
        lock (_gate)
        {
            return _errorReceived;
        }
    }

    private Func<CancellationToken, ValueTask>? SnapshotDisconnected()
    {
        lock (_gate)
        {
            return _disconnected;
        }
    }

    private Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>? SnapshotConnectionStateChanged()
    {
        lock (_gate)
        {
            return _connectionStateChanged;
        }
    }

    private readonly record struct QueuedCallback(
        Func<CancellationToken, ValueTask> Callback,
        bool ReportErrors);
}
