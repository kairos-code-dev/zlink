namespace Zlink.Framework.Runtime.Channels;

internal interface IZLinkRouteInternalPacketDispatcher
{
    bool CanHandleSend(string packetName);

    bool CanHandleRequest(string packetName);

    ValueTask DispatchSendAsync(
        Received received,
        CancellationToken cancellationToken);

    ValueTask<Message> DispatchRequestAsync(
        Received received,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken);
}

internal sealed class ZLinkNoRouteInternalPacketDispatcher : IZLinkRouteInternalPacketDispatcher
{
    public static ZLinkNoRouteInternalPacketDispatcher Instance { get; } = new();

    private ZLinkNoRouteInternalPacketDispatcher()
    {
    }

    public bool CanHandleSend(string packetName)
    {
        _ = packetName;
        return false;
    }

    public bool CanHandleRequest(string packetName)
    {
        _ = packetName;
        return false;
    }

    public ValueTask DispatchSendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        _ = received;
        _ = cancellationToken;
        throw new InvalidOperationException("No routed internal send dispatcher is configured.");
    }

    public ValueTask<Message> DispatchRequestAsync(
        Received received,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken)
    {
        _ = received;
        _ = routedHeader;
        _ = cancellationToken;
        throw new InvalidOperationException("No routed internal request dispatcher is configured.");
    }
}
