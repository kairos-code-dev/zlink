namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotOutboundEndpoint(
    IZLinkCurrentSpotActivation activation,
    IServiceProvider services,
    ZLinkSpotOutboundTransport outbound,
    ZLinkFrameworkRuntime runtime,
    string resolverErrorMessage)
{
    public IZLinkSendCall SendToSpot<TMessage>(RoutingId spotRid, TMessage message)
    {
        return new ZLinkRoutedSpotSendCall<TMessage>(
            activation,
            RequireRemoteAddressResolver(),
            ZLinkSpotRemoteAddressTarget.ByRoutingId(spotRid),
            message);
    }

    public IZLinkRequestCall RequestToSpot<TRequest>(RoutingId spotRid, TRequest request)
    {
        return new ZLinkRoutedSpotRequestCall<TRequest>(
            activation,
            RequireRemoteAddressResolver(),
            ZLinkSpotRemoteAddressTarget.ByRoutingId(spotRid),
            request);
    }

    public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
    {
        return new ZLinkCurrentSpotPublishCall<TEvent>(activation, topic, message);
    }

    public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
    {
        return new ZLinkCurrentSpotSendCall<TMessage>(activation, channelName, message);
    }

    public IZLinkRequestCall RequestToChannel<TRequest>(string channelName, TRequest request)
    {
        return new ZLinkCurrentSpotRequestCall<TRequest>(activation, channelName, request);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToChannelAsync(
        string channelName,
        Message message,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return await outbound.RequestToChannelAsync(channelName, message, timeout, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return await outbound.RequestToChannelAsync(channelName, parts, timeout, cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask SendToChannelAsync(
        string channelName,
        Message message,
        CancellationToken cancellationToken)
    {
        return outbound.SendToChannelAsync(channelName, message, cancellationToken);
    }

    public ValueTask SendToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return outbound.SendToChannelAsync(channelName, parts, cancellationToken);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        Message message,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return await outbound.RequestToSpotAsync(
                targetNodeRid,
                targetSpotRid,
                message,
                timeout,
                cancellationToken)
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
        return await runtime.RequestToSpotViaRouterChannelAsync(
                routerChannelId,
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout ?? activation.DefaultTimeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask PublishCurrentAsync(
        string topic,
        Message message,
        CancellationToken cancellationToken)
    {
        return outbound.PublishCurrentAsync(topic, message, cancellationToken);
    }

    public ValueTask PublishCurrentAsync(
        string topic,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return outbound.PublishCurrentAsync(topic, parts, cancellationToken);
    }

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        Message message,
        SendFlags flags)
    {
        return outbound.SendToSpot(targetRid, spotRid, message, flags);
    }

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return outbound.SendToSpot(targetRid, spotRid, parts, flags);
    }

    public ValueTask SendToSpotAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return runtime.SendToSpotViaRouterChannelAsync(
            routerChannelId,
            targetNodeRid,
            targetSpotRid,
            parts,
            cancellationToken);
    }

    private IZLinkSpotRemoteAddressResolver RequireRemoteAddressResolver()
    {
        return services.GetService(typeof(IZLinkSpotRemoteAddressResolver)) is IZLinkSpotRemoteAddressResolver resolver
            ? resolver
            : throw new ZLinkConfigurationException(resolverErrorMessage);
    }
}
