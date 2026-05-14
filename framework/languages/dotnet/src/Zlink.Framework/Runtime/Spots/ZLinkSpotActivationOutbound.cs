namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation
{
    public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
    {
        return new ZLinkCurrentSpotPublishCall<TEvent>(this, topic, message);
    }

    public IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message)
    {
        return new ZLinkCurrentSpotSendCall<TMessage>(this, channelName, message);
    }

    public IZLinkRequestCall RequestChannel<TRequest>(string channelName, TRequest request)
    {
        return new ZLinkCurrentSpotRequestCall<TRequest>(this, channelName, request);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestChannelAsync(
        string channelName,
        Message message,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return await _outbound.RequestChannelAsync(channelName, message, timeout, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return await _outbound.RequestChannelAsync(channelName, parts, timeout, cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask SendChannelAsync(
        string channelName,
        Message message,
        CancellationToken cancellationToken)
    {
        return _outbound.SendChannelAsync(channelName, message, cancellationToken);
    }

    public ValueTask SendChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _outbound.SendChannelAsync(channelName, parts, cancellationToken);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        Message message,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return await _outbound.RequestSpotAsync(
                targetNodeRid,
                targetSpotRid,
                message,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return await _outbound.RequestSpotAsync(
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask PublishCurrentAsync(
        string topic,
        Message message,
        CancellationToken cancellationToken)
    {
        return _outbound.PublishCurrentAsync(topic, message, cancellationToken);
    }

    public ValueTask PublishCurrentAsync(
        string topic,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _outbound.PublishCurrentAsync(topic, parts, cancellationToken);
    }

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        Message message,
        SendFlags flags)
    {
        return _outbound.SendToSpot(targetRid, spotRid, message, flags);
    }

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return _outbound.SendToSpot(targetRid, spotRid, parts, flags);
    }
}
