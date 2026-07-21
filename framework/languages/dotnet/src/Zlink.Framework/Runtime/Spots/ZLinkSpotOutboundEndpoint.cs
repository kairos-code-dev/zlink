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

    public IZLinkPublishCall Publish<TEvent>(string channelName, string topic, TEvent message)
    {
        return new ZLinkCurrentSpotPublishCall<TEvent>(activation, channelName, topic, message);
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
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        using var operation = runtime.EnterOperation(countAsRequest: true);
        var requestTimeout = timeout ?? activation.DefaultRequestTimeout;
        var metricStarted = ZLinkRuntimeMetrics.StartChannelRequest();
        var timedOut = false;
        try
        {
            return await outbound.RequestToChannelAsync(
                    channelName,
                    parts,
                    requestTimeout,
                    cancellationToken,
                    metadata).ConfigureAwait(false);
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

    /// <summary>Performs the first non-blocking ChannelName admission attempt
    /// on the current MeshNode. False lets the async submitter wait for
    /// send-ready.</summary>
    public bool TrySendToChannelOnce(
        string channelName,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        using var operation = runtime.EnterOperation();
        return outbound.TrySendToChannelOnce(channelName, parts, metadata);
    }

    public ValueTask<ZLinkSubmitResult> SendToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        using var operation = runtime.EnterOperation();
        return outbound.SendToChannelAsync(channelName, parts, cancellationToken, metadata);
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

    public async ValueTask<MeshPublishResult> PublishCurrentAsync(
        string channelName,
        string topic,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        using var operation = runtime.EnterOperation();
        return await ZLinkLogicalMulticastSubmitter.SubmitAsync(
                runtime.WorkerPool,
                () => outbound.PublishCurrentBlocking(channelName, topic, parts, metadata),
                cancellationToken,
                runtime.ShutdownToken)
            .ConfigureAwait(false);
    }

    public MeshPublishResult TryPublishCurrentOnce(
        string channelName,
        string topic,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata)
    {
        using var operation = runtime.EnterOperation();
        return outbound.TryPublishCurrentOnce(channelName, topic, parts, metadata);
    }

    /// <summary>Performs the first non-blocking spot-send admission attempt.
    /// False lets the async submitter wait for send-ready.</summary>
    public bool TrySendToSpotOnce(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        return runtime.TrySendToSpotViaRouterChannelOnce(
            routerChannelId,
            targetNodeRid,
            targetSpotRid,
            targetSpotGeneration,
            parts,
            metadata);
    }

    public ValueTask<ZLinkSubmitResult> SendToSpotAsync(
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
