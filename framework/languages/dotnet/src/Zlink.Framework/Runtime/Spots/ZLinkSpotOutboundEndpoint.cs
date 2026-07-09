namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotOutboundEndpoint(
    IZLinkCurrentSpotActivation activation,
    ZLinkSpotOutboundTransport outbound,
    ZLinkFrameworkRuntime runtime) : IZLinkSpotOutbound
{
    public IZLinkSendCall SendToSpot<TMessage>(SpotRef address, TMessage message)
    {
        return new ZLinkRoutedSpotSendCall<TMessage>(activation, address, message);
    }

    public IZLinkYieldRequestCall RequestToSpot<TRequest>(SpotRef address, TRequest request)
    {
        return new ZLinkRoutedSpotRequestCall<TRequest>(activation, address, request);
    }

    public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
    {
        return new ZLinkCurrentSpotPublishCall<TEvent>(activation, topic, message);
    }

    public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
    {
        return new ZLinkCurrentSpotSendCall<TMessage>(activation, channelName, message);
    }

    public IZLinkYieldRequestCall RequestToChannel<TRequest>(string channelName, TRequest request)
    {
        return new ZLinkCurrentSpotRequestCall<TRequest>(activation, channelName, request);
    }

    public ValueTask<IReadOnlyList<Message>> RequestToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return RequestToChannelThroughSharedClientAsync(channelName, parts, timeout, cancellationToken);
    }

    public ValueTask SendToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return SendToChannelThroughSharedClientAsync(channelName, parts, cancellationToken);
    }

    private async ValueTask<IReadOnlyList<Message>> RequestToChannelThroughSharedClientAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var bundle = runtime.GetOrCreateClientBundle(channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var requestTimeout = timeout ?? activation.DefaultRequestTimeout;
        return await ZLinkRawRequestSubmitter.SubmitAsync(
                bundle.Submitter
                ?? throw new InvalidOperationException("ZLink request submitter is not initialized."),
                parts,
                (pending, callback, currentTimeout) => dealer.Request(
                    pending,
                    callback,
                    SendFlags.DontWait,
                    currentTimeout),
                requestTimeout,
                "SPOT channel request failed with result '{0}'.",
                cancellationToken)
            .ConfigureAwait(false);
    }

    private ValueTask SendToChannelThroughSharedClientAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var bundle = runtime.GetOrCreateClientBundle(channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        return (bundle.Submitter
                ?? throw new InvalidOperationException("ZLink send submitter is not initialized."))
            .Async(
                parts,
                pending => dealer.Send(pending, SendFlags.DontWait),
                cancellationToken);
    }

    public ValueTask<IReadOnlyList<Message>> RequestToSpotAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return runtime.RequestToSpotViaRouterChannelAsync(
            routerChannelId,
            targetNodeRid,
            targetSpotRid,
            parts,
            timeout ?? activation.DefaultRequestTimeout,
            cancellationToken);
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
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return outbound.SendToSpot(targetRid, targetSpotRid, parts, flags);
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

}
