namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkCompositeRouteInternalPacketDispatcher(
    params IZLinkRouteInternalPacketDispatcher[] dispatchers)
    : IZLinkRouteInternalPacketDispatcher
{
    public bool CanHandleSend(string packetName)
    {
        return dispatchers.Any(dispatcher => dispatcher.CanHandleSend(packetName));
    }

    public bool CanHandleRequest(string packetName)
    {
        return dispatchers.Any(dispatcher => dispatcher.CanHandleRequest(packetName));
    }

    public ValueTask DispatchSendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var packetName = ZLinkEnvelopeCodec.DecodeHeader(received.Parts).MessageName;
        return ResolveSend(packetName).DispatchSendAsync(received, cancellationToken);
    }

    public ValueTask<Message> DispatchRequestAsync(
        Received received,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken)
    {
        return ResolveRequest(routedHeader.MessageName)
            .DispatchRequestAsync(received, routedHeader, cancellationToken);
    }

    private IZLinkRouteInternalPacketDispatcher ResolveSend(string packetName)
    {
        return dispatchers.FirstOrDefault(dispatcher => dispatcher.CanHandleSend(packetName))
               ?? throw new InvalidOperationException($"Internal routed send packet '{packetName}' is not supported.");
    }

    private IZLinkRouteInternalPacketDispatcher ResolveRequest(string packetName)
    {
        return dispatchers.FirstOrDefault(dispatcher => dispatcher.CanHandleRequest(packetName))
               ?? throw new InvalidOperationException(
                   $"Internal routed request packet '{packetName}' is not supported.");
    }
}