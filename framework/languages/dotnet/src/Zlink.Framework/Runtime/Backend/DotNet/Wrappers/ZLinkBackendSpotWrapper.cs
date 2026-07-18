namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

// RouteMesh 10.0.0 spot seam over the binding ISpot plus the node dispatch pump.
// Inbound route/subscribe/actor-join/lifecycle records are pulled from the pump's
// per-spot queues (fed by the node DrainReady loop); outbound requests register a
// completion in the node completion table.
internal sealed class ZLinkBackendSpotWrapper : IZLinkBackendSpot
{
    private readonly IMeshNode _node;
    private readonly ISpot _spot;
    private readonly ZLinkMeshDispatchPump _pump;
    private readonly ZLinkMeshCompletionTable _completions;
    private readonly ZLinkMeshDispatchPump.SpotDispatchState _state;

    private readonly Func<string?> _publishChannelName;

    public ZLinkBackendSpotWrapper(
        IMeshNode node,
        ISpot spot,
        ZLinkMeshDispatchPump pump,
        ZLinkMeshCompletionTable completions,
        Func<string?>? publishChannelName = null)
    {
        _node = node;
        _spot = spot;
        _pump = pump;
        _completions = completions;
        _publishChannelName = publishChannelName ?? (static () => null);
        _state = pump.RegisterSpot(spot.RoutingId);
    }

    // Spot pub/sub is logical multicast on the router plane: the publish and
    // subscription channel is the node's mesh channel; the topic is the
    // filter. Falls back to the topic (channel-per-topic) when the node has
    // no registered channel yet.
    private string PublishChannel(string topic) => _publishChannelName() ?? topic;

    public RoutingId RoutingId => _spot.RoutingId;

    public ulong LifecycleGeneration => _spot.Status().LifecycleGeneration;

    internal ISpot NativeSpot => _spot;

    // Spot routing ids are assigned by the node (CreateSpot/GetOrCreateSpot); the
    // binding ISpot has no setter, so this preserves callers as a no-op.
    public void SetRoutingId(RoutingId routingId)
    {
    }

    public void SetSubscription(string topic)
    {
        _spot.SetSubscription(PublishChannel(topic), topic);
    }

    public ZLinkBackendSubscribeMessage? Subscribe(RecvFlags flags)
    {
        return _state.Subscriptions.TryDequeue(out var message) ? message : null;
    }

    public ZLinkBackendRouteReceived? RecvRoute(RecvFlags flags)
    {
        return _state.Routes.TryDequeue(out var route) ? route : null;
    }

    public void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler)
    {
        _pump.SetDispatchHandler(_spot.RoutingId, handler);
    }

    public void OnSendReady(Action handler)
    {
        _state.SendReadyHandler = handler;
    }

    public bool RequestToChannel(
        string channelName,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout,
        ReadOnlyMemory<byte> metadata)
    {
        return RequestToChannel(
            channelName, new[] { message }, callback, flags, timeout, metadata);
    }

    public bool RequestToChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout,
        ReadOnlyMemory<byte> metadata)
    {
        var submit = _spot.RequestToChannel(
            channelName, parts, out var operationId, timeout ?? default, flags,
            metadata);
        // Terminal admission failures (NotFound, InvalidState, ...) must surface
        // to the caller; only Backpressured means "wait for send-ready and retry".
        return ZLinkSubmitFailureMapper.AcceptOrThrow(submit, $"channel '{channelName}'")
               && _completions.RegisterRequest(operationId, callback);
    }

    public SubmitResult SendToChannel(
        string channelName, Message message, SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        return _spot.SendToChannel(channelName, new[] { message }, flags, metadata);
    }

    public SubmitResult SendToChannel(
        string channelName, IReadOnlyList<Message> parts, SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        return _spot.SendToChannel(channelName, parts, flags, metadata);
    }

    public MeshPublishDetail Publish(
        string topic, Message message, SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        return _spot.Publish(PublishChannel(topic), topic, new[] { message }, flags, metadata);
    }

    public MeshPublishDetail Publish(
        string topic, IReadOnlyList<Message> parts, SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        var detail = _spot.Publish(PublishChannel(topic), topic, parts, flags, metadata);
        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
            Console.Error.WriteLine(
                $"[publish] ch={PublishChannel(topic)} topic={topic} detail={detail}");
        return detail;
    }

    public SubmitResult SendToSpot(
        RoutingId targetRid, RoutingId spotRid, ulong spotGeneration,
        Message message, SendFlags flags, ReadOnlyMemory<byte> metadata)
    {
        return _spot.SendToSpot(
            targetRid, spotRid, spotGeneration, new[] { message }, flags, metadata);
    }

    public SubmitResult SendToSpot(
        RoutingId targetRid, RoutingId spotRid, ulong spotGeneration,
        IReadOnlyList<Message> parts, SendFlags flags, ReadOnlyMemory<byte> metadata)
    {
        return _spot.SendToSpot(
            targetRid, spotRid, spotGeneration, parts, flags, metadata);
    }

    public bool RequestToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        ulong spotGeneration,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout,
        ReadOnlyMemory<byte> metadata)
    {
        return RequestToSpot(
            targetRid, spotRid, spotGeneration, new[] { message }, callback, flags,
            timeout, metadata);
    }

    public bool RequestToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        ulong spotGeneration,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout,
        ReadOnlyMemory<byte> metadata)
    {
        var submit = _spot.RequestToSpot(
            targetRid, spotRid, spotGeneration, parts, out var operationId,
            timeout ?? default, flags, metadata);
        // See RequestToChannel: only Backpressured may wait for send-ready.
        return ZLinkSubmitFailureMapper.AcceptOrThrow(
                   submit, $"SPOT '{spotRid}' on node '{targetRid}'")
               && _completions.RegisterRequest(operationId, callback);
    }

    public ZLinkBackendActorJoinRequest? RecvActorJoin(RecvFlags flags)
    {
        return _state.ActorJoins.TryDequeue(out var request) ? request : null;
    }

    public ZLinkBackendSpotActorLifecycleEvent? RecvActorLifecycle(RecvFlags flags)
    {
        return _state.Lifecycles.TryDequeue(out var lifecycle) ? lifecycle : null;
    }

    public void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request, int joinResultCode, Message reply)
    {
        RequireMeshRequest(request).ReplyJoin(joinResultCode, new[] { reply });
    }

    public void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request,
        int joinResultCode,
        IReadOnlyList<Message> parts)
    {
        RequireMeshRequest(request).ReplyJoin(joinResultCode, parts);
    }

    private static ZLinkMeshActorJoinRequest RequireMeshRequest(
        ZLinkBackendActorJoinRequest request) =>
        request as ZLinkMeshActorJoinRequest
        ?? throw new InvalidOperationException("Expected a MeshNode actor join request.");

    public ValueTask DisposeAsync()
    {
        return _spot.DisposeAsync();
    }
}
