namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotOutboundEndpoint(
    IZLinkCurrentSpotActivation activation,
    ZLinkSpotOutboundTransport outbound,
    ZLinkFrameworkRuntime runtime) : IZLinkSpotOutbound
{
    public IZLinkSpotSendCall SendToSpot<TMessage>(string spotId, TMessage message) =>
        new ZLinkInstanceSpotSendCall<TMessage>(runtime, spotId, message);

    public IZLinkSpotRequestCall RequestToSpot<TRequest>(string spotId, TRequest request) =>
        new ZLinkInstanceSpotRequestCall<TRequest>(runtime, spotId, request);

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

    public ValueTask<ZLinkOneWaySubmitResult> SendToChannelAsync(
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
        string targetSpotId,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return runtime.RequestToSpotViaRouterChannelAsync(
            routerChannelId,
            targetNodeRid,
            targetSpotId,
            targetSpotGeneration,
            authorityOwnerGeneration,
            parts,
            timeout ?? activation.DefaultRequestTimeout,
            cancellationToken,
            metadata);
    }

    public async ValueTask<SubmitResult> PublishCurrentAsync(
        string channelName,
        string topic,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata,
        Action release,
        IZLinkRuntimeFailureReporter errorSink)
    {
        using var operation = runtime.EnterOperation();
        return await ZLinkLogicalMulticastSubmitter.SubmitAsync(
                runtime.WorkerPool,
                () => outbound.PublishCurrent(channelName, topic, parts, metadata),
                cancellationToken,
                runtime.ShutdownToken,
                runtime.Registration.DefaultSocketSendTimeout,
                release,
                errorSink)
            .ConfigureAwait(false);
    }

    /// <summary>Performs the first non-blocking spot-send admission attempt.
    /// False lets the async submitter wait for send-ready.</summary>
    public bool TrySendToSpotOnce(
        string routerChannelId,
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        return runtime.TrySendToSpotViaRouterChannelOnce(
            routerChannelId,
            targetNodeRid,
            targetSpotId,
            targetSpotGeneration,
            authorityOwnerGeneration,
            parts,
            metadata);
    }

    public ValueTask<ZLinkOneWaySubmitResult> SendToSpotAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return runtime.SendToSpotViaRouterChannelAsync(
            routerChannelId,
            targetNodeRid,
            targetSpotId,
            targetSpotGeneration,
            authorityOwnerGeneration,
            parts,
            cancellationToken,
            metadata);
    }

}
