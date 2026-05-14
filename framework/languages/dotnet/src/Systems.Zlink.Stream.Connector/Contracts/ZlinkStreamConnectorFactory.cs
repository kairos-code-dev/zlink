namespace Systems.Zlink.Stream.Connector.Contracts;

public static class ZlinkStreamConnectorFactory
{
    public static IZlinkStreamConnector Create(ZlinkStreamConnectorOptions options)
    {
        return new ZlinkStreamConnector(options);
    }

    public static async ValueTask<IZlinkStreamConnector> ConnectAsync(
        ZlinkStreamConnectorOptions options,
        CancellationToken cancellationToken = default)
    {
        var connector = new ZlinkStreamConnector(options);
        await connector.ConnectAsync(cancellationToken).ConfigureAwait(false);
        return connector;
    }
}
