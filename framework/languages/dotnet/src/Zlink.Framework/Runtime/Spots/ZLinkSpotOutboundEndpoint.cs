namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotOutboundEndpoint(
    IZLinkCurrentSpotActivation activation,
    ZLinkSpotOutboundTransport outbound,
    ZLinkFrameworkRuntime runtime) : IZLinkSpotOutbound
{
    public IZLinkSendCall SendToSpot<TMessage>(SpotHandle target, TMessage message)
    {
        return new ZLinkRoutedSpotSendCall<TMessage>(activation, RequireResolvedHandle(target), message);
    }

    public IZLinkRequestCall RequestToSpot<TRequest>(SpotHandle target, TRequest request)
    {
        return new ZLinkRoutedSpotRequestCall<TRequest>(activation, RequireResolvedHandle(target), request);
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

    private static ZLinkResolvedSpotHandle RequireResolvedHandle(SpotHandle target)
    {
        return target as ZLinkResolvedSpotHandle
               ?? throw new ArgumentException("Spot handle was not created by this framework runtime.", nameof(target));
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        using var operation = runtime.EnterOperation(countAsRequest: true);
        var bundle = runtime.GetClientBundle(channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var requestTimeout = timeout ?? activation.DefaultRequestTimeout;
        var metricStarted = ZLinkRuntimeMetrics.StartChannelRequest();
        var timedOut = false;
        try
        {
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
                    cancellationToken).ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
            timedOut = true;
            throw;
        }
        finally
        {
            ZLinkRuntimeMetrics.CompleteChannelRequest(metricStarted, timedOut);
        }
    }

    public ValueTask SendToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        using var operation = runtime.EnterOperation();
        var bundle = runtime.GetClientBundle(channelName);
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
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return runtime.RequestToSpotViaRouterChannelAsync(
            routerChannelId,
            targetNodeRid,
            targetSpotRid,
            targetSpotGeneration,
            parts,
            timeout ?? activation.DefaultRequestTimeout,
            cancellationToken,
            metadata);
    }

    public ValueTask<MeshPublishDetail> PublishCurrentAsync(
        string topic,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        using var operation = runtime.EnterOperation();
        return outbound.PublishCurrentAsync(topic, parts, cancellationToken, metadata);
    }

    public bool TryPublishCurrentOnce(
        string topic,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata,
        out MeshPublishDetail? detail)
    {
        using var operation = runtime.EnterOperation();
        return outbound.TryPublishCurrentOnce(topic, parts, metadata, out detail);
    }

    public ValueTask SendToSpotAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return runtime.SendToSpotViaRouterChannelAsync(
            routerChannelId,
            targetNodeRid,
            targetSpotRid,
            targetSpotGeneration,
            parts,
            cancellationToken,
            metadata);
    }

}
