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

    public ZLinkBackendSpotWrapper(
        IMeshNode node,
        ISpot spot,
        ZLinkMeshDispatchPump pump,
        ZLinkMeshCompletionTable completions)
    {
        _node = node;
        _spot = spot;
        _pump = pump;
        _completions = completions;
        _state = pump.RegisterSpot(spot.RoutingId);
    }

    public RoutingId RoutingId => _spot.RoutingId;

    // Spot routing ids are assigned by the node (CreateSpot/GetOrCreateSpot); the
    // binding ISpot has no setter, so this preserves callers as a no-op.
    public void SetRoutingId(RoutingId routingId)
    {
    }

    public void SetSubscription(string topic)
    {
        _spot.SetSubscription(topic, topic);
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
        TimeSpan? timeout)
    {
        return RequestToChannel(channelName, new[] { message }, callback, flags, timeout);
    }

    public bool RequestToChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var submit = _spot.RequestToChannel(
            channelName, parts, out var operationId, timeout ?? default, flags);
        return submit == SubmitResult.Ok
               && _completions.RegisterRequest(operationId, callback);
    }

    public bool SendToChannel(string channelName, Message message, SendFlags flags)
    {
        return _spot.SendToChannel(channelName, new[] { message }, flags) == SubmitResult.Ok;
    }

    public bool SendToChannel(
        string channelName, IReadOnlyList<Message> parts, SendFlags flags)
    {
        return _spot.SendToChannel(channelName, parts, flags) == SubmitResult.Ok;
    }

    public bool Publish(string topic, Message message, SendFlags flags)
    {
        return _spot.Publish(topic, topic, new[] { message }, flags) is not null;
    }

    public bool Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags)
    {
        return _spot.Publish(topic, topic, parts, flags) is not null;
    }

    public bool SendToSpot(
        RoutingId targetRid, RoutingId spotRid, Message message, SendFlags flags)
    {
        return _spot.SendToSpot(targetRid, spotRid, 0, new[] { message }, flags)
            == SubmitResult.Ok;
    }

    public bool SendToSpot(
        RoutingId targetRid, RoutingId spotRid, IReadOnlyList<Message> parts, SendFlags flags)
    {
        return _spot.SendToSpot(targetRid, spotRid, 0, parts, flags) == SubmitResult.Ok;
    }

    public bool RequestToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        return RequestToSpot(targetRid, spotRid, new[] { message }, callback, flags, timeout);
    }

    public bool RequestToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var submit = _spot.RequestToSpot(
            targetRid, spotRid, 0, parts, out var operationId, timeout ?? default, flags);
        return submit == SubmitResult.Ok
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
