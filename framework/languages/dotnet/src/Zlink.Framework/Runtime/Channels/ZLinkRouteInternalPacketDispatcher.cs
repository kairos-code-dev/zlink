namespace Zlink.Framework.Runtime.Channels;

internal interface IZLinkRouteInternalPacketDispatcher
{
    bool CanHandleSend(string packetName);

    bool CanHandleRequest(string packetName);

    ValueTask DispatchSendAsync(
        Received received,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken);

    ValueTask<Message> DispatchRequestAsync(
        Received received,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken);
}

internal sealed class ZLinkNoRouteInternalPacketDispatcher : IZLinkRouteInternalPacketDispatcher
{
    private ZLinkNoRouteInternalPacketDispatcher()
    {
    }

    public static ZLinkNoRouteInternalPacketDispatcher Instance { get; } = new();

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
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken)
    {
        _ = received;
        _ = routedHeader;
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
