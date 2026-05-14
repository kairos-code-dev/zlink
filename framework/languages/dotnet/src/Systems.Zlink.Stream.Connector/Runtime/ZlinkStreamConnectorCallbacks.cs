namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamConnectorCallbacks(ZlinkStreamTaskRunner taskRunner)
{
    private readonly object _gate = new();
    private Func<ZlinkStreamError, CancellationToken, ValueTask>? _errorReceived;
    private Func<CancellationToken, ValueTask>? _disconnected;

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

    public async ValueTask PublishErrorAsync(ZlinkStreamError error, CancellationToken cancellationToken)
    {
        var handler = SnapshotErrorReceived();
        if (handler is not null)
        {
            await InvokeUserCallbackAsync(() => handler(error, cancellationToken), cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public async ValueTask NotifyDisconnectedAsync(CancellationToken cancellationToken)
    {
        var disconnected = SnapshotDisconnected();
        if (disconnected is not null)
        {
            await InvokeUserCallbackAsync(
                () => disconnected(cancellationToken),
                cancellationToken).ConfigureAwait(false);
        }
    }

    public void QueueRequestCallback<TResult>(
        Func<ValueTask<ZlinkStreamEncodedBody>> request,
        Func<ZlinkStreamEncodedBody, TResult> success,
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
                    callback(success(reply));
                }
                catch (ZlinkStreamException ex)
                {
                    callback(failure(ex.Error));
                }
                catch (Exception ex)
                {
                    callback(failure(new ZlinkStreamError(
                        ZlinkStreamErrorCode.SendFailed,
                        ex.Message,
                        ex)));
                }
            });
    }

    private async ValueTask InvokeUserCallbackAsync(
        Func<ValueTask> callback,
        CancellationToken cancellationToken)
    {
        try
        {
            await callback().ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            var handler = SnapshotErrorReceived();
            if (handler is not null)
            {
                try
                {
                    await handler(new ZlinkStreamError(
                        ZlinkStreamErrorCode.UserCallbackFailed,
                        "User callback failed.",
                        ex), cancellationToken).ConfigureAwait(false);
                }
                catch
                {
                }
            }
        }
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
}
