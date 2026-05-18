namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamReceiveLoop(
    ZlinkStreamReceiveDispatcher dispatcher,
    Func<IZlinkStreamConnection?> connectionProvider,
    Action recordInbound)
{
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            var connection = connectionProvider();
            if (connection is null)
            {
                return;
            }

            var packet = await ZlinkStreamFrameCodec.ReadAsync(connection, cancellationToken).ConfigureAwait(false);
            recordInbound();
            await dispatcher.DispatchPacketAsync(packet, cancellationToken).ConfigureAwait(false);
        }
    }
}
