namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamReceiveLoop(
    ZlinkStreamPendingRequests pending,
    ZlinkStreamReceiveDispatcher dispatcher,
    ZlinkStreamConnectorCallbacks callbacks,
    Func<IZlinkStreamConnection?> connectionProvider,
    Action clearConnection)
{
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                var connection = connectionProvider();
                if (connection is null)
                {
                    return;
                }

                var packet = await ZlinkStreamFrameCodec.ReadAsync(connection, cancellationToken).ConfigureAwait(false);
                await dispatcher.DispatchPacketAsync(packet, cancellationToken).ConfigureAwait(false);
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
            await callbacks.PublishErrorAsync(error, CancellationToken.None).ConfigureAwait(false);
        }
        finally
        {
            clearConnection();
            pending.FailAll(new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connector disconnected."));
            await callbacks.NotifyDisconnectedAsync(CancellationToken.None).ConfigureAwait(false);
        }
    }
}
