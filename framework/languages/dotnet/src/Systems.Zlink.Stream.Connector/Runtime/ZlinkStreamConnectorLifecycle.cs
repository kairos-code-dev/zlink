using System.Security.Authentication;

namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamConnectorLifecycle(
    ZlinkStreamConnectorOptions options,
    ZlinkStreamPendingRequests pending,
    ZlinkStreamTaskRunner taskRunner)
    : IDisposable
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private IZlinkStreamConnection? _connection;
    private CancellationTokenSource? _receiveCts;
    private Task? _receiveTask;

    public IZlinkStreamConnection? Connection => _connection;

    public bool IsConnected => _connection is not null;

    public async ValueTask ConnectAsync(
        Func<CancellationToken, Task> runReceiveLoop,
        Action throwIfDisposed,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            throwIfDisposed();
            if (_connection is not null)
            {
                return;
            }

            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(options.ConnectTimeout);

            try
            {
                _connection = await ZlinkStreamTransportFactory
                    .ConnectAsync(options, timeoutCts.Token)
                    .ConfigureAwait(false);
                _receiveCts = new CancellationTokenSource();
                _receiveTask = taskRunner.Run(
                    "stream-receive-loop",
                    _ => new ValueTask(runReceiveLoop(_receiveCts.Token)));
            }
            catch (OperationCanceledException ex) when (!cancellationToken.IsCancellationRequested)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.ConnectTimeout, "Connect timed out.", ex);
            }
            catch (AuthenticationException ex)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.TlsValidationFailed, "TLS validation failed.", ex);
            }
            catch (Exception ex) when (ex is not ZlinkStreamException)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.Disconnected, "Connect failed.", ex);
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        var snapshot = await DetachAsync(cancellationToken).ConfigureAwait(false);
        snapshot.ReceiveCts?.Cancel();

        if (snapshot.Connection is not null)
        {
            await snapshot.Connection.CloseAsync(cancellationToken).ConfigureAwait(false);
        }

        pending.FailAll(new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connector closed."));

        if (snapshot.ReceiveTask is not null)
        {
            try
            {
                await snapshot.ReceiveTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }

        snapshot.ReceiveCts?.Dispose();
    }

    public void ClearConnection()
    {
        Interlocked.Exchange(ref _connection, null);
    }

    public void Dispose()
    {
        _gate.Dispose();
    }

    private async ValueTask<LifecycleSnapshot> DetachAsync(CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var snapshot = new LifecycleSnapshot(_connection, _receiveCts, _receiveTask);
            _connection = null;
            _receiveCts = null;
            _receiveTask = null;
            return snapshot;
        }
        finally
        {
            _gate.Release();
        }
    }

    private readonly record struct LifecycleSnapshot(
        IZlinkStreamConnection? Connection,
        CancellationTokenSource? ReceiveCts,
        Task? ReceiveTask);
}
