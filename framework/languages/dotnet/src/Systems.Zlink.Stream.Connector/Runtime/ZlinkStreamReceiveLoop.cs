namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamReceiveLoop(
    ZlinkStreamReceiveDispatcher dispatcher,
    Func<IZlinkStreamConnection?> connectionProvider,
    Action recordInbound,
    int maxReceivePayloadSize,
    string transport)
{
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            var connection = connectionProvider();
            if (connection is null) return;

            var packet = await ZlinkStreamFrameCodec.ReadAsync(
                connection,
                maxReceivePayloadSize,
                cancellationToken).ConfigureAwait(false);
            ZlinkStreamRuntimeMetrics.RecordInboundBytes(
                ZlinkStreamFrameCodec.GetFrameSize(packet.Header.Length, packet.Payload.Length),
                transport);
            recordInbound();
            await dispatcher.DispatchPacketAsync(packet, cancellationToken).ConfigureAwait(false);
        }
    }
}
