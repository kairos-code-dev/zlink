namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkEntrySpotActivation
{
    public IZLinkSpotHandlerRegistry Handlers => _handlersSurface;

    public IZLinkSpotOutbound Outbound => _outboundSurface;

    public IZLinkSendCall SendToSpot<TMessage>(RoutingId spotRid, TMessage message)
    {
        return _outboundEndpoint.SendToSpot(spotRid, message);
    }

    public IZLinkRequestCall RequestToSpot<TRequest>(RoutingId spotRid, TRequest request)
    {
        return _outboundEndpoint.RequestToSpot(spotRid, request);
    }

    public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
    {
        return _outboundEndpoint.Publish(topic, message);
    }

    public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
    {
        return _outboundEndpoint.SendToChannel(channelName, message);
    }

    public IZLinkRequestCall RequestToChannel<TRequest>(string channelName, TRequest request)
    {
        return _outboundEndpoint.RequestToChannel(channelName, request);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return await _outboundEndpoint.RequestToChannelAsync(channelName, parts, timeout, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToSpotAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return await _outboundEndpoint.RequestToSpotAsync(
                routerChannelId,
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask SendToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _outboundEndpoint.SendToChannelAsync(channelName, parts, cancellationToken);
    }

    public ValueTask PublishCurrentAsync(
        string topic,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _outboundEndpoint.PublishCurrentAsync(topic, parts, cancellationToken);
    }

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return _outboundEndpoint.SendToSpot(targetRid, spotRid, parts, flags);
    }

    public ValueTask SendToSpotAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _outboundEndpoint.SendToSpotAsync(
            routerChannelId,
            targetNodeRid,
            targetSpotRid,
            parts,
            cancellationToken);
    }
}
