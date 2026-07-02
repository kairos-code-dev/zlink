namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendSpotNodeWrapper(ISpotNode nativeSpotNode) : IZLinkBackendSpotNode
{
    public object NativeInstance => nativeSpotNode;

    public RoutingId RoutingId => nativeSpotNode.RoutingId;

    public void SetRoutingId(RoutingId routingId)
    {
        nativeSpotNode.SetRoutingId(routingId);
    }

    public void SetPublisherRoutingId(RoutingId routingId)
    {
        nativeSpotNode.SetPublisherRoutingId(routingId);
    }

    public void SetSubscriberRoutingId(RoutingId routingId)
    {
        nativeSpotNode.SetSubscriberRoutingId(routingId);
    }

    public void SetRouterBind(string endpoint)
    {
        nativeSpotNode.SetRouterBind(endpoint);
    }

    public void SetPubBind(string endpoint)
    {
        nativeSpotNode.SetPubBind(endpoint);
    }

    public void ConnectPeer(string endpoint)
    {
        nativeSpotNode.ConnectPeer(endpoint);
    }

    public void ConnectPeer(RoutingId peerRid, string endpoint)
    {
        nativeSpotNode.ConnectPeerRid(peerRid, endpoint);
    }

    public void DisconnectPeer(string endpoint)
    {
        nativeSpotNode.DisconnectPeer(endpoint);
    }

    public IZLinkBackendSpot CreateSpot()
    {
        return new ZLinkBackendSpotWrapper(nativeSpotNode.CreateSpot());
    }

    public IZLinkBackendSpot GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        return new ZLinkBackendSpotWrapper(
            nativeSpotNode.GetOrCreateSpot(spotRid, out created));
    }

    public ZLinkSpotNodeStatus Status()
    {
        return nativeSpotNode.Status().ToFramework();
    }

    public IReadOnlyList<ZLinkSpotNodePeerEntry> Peers()
    {
        return nativeSpotNode.Peers()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects()
    {
        return nativeSpotNode.Subjects()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IZLinkBackendSpotRouteBridge CreateRouteBridge()
    {
        return new ZLinkBackendSpotRouteBridgeWrapper(nativeSpotNode.CreateRouteBridge());
    }

    public IZLinkBackendSpot EntrySpot()
    {
        return new ZLinkBackendSpotWrapper(nativeSpotNode.EntrySpot());
    }

    public ZLinkBackendActorRef CreateActor(string actorId, Message createRequest)
    {
        var actor = nativeSpotNode.CreateActor(actorId, createRequest);
        return actor.Ref.ToBackend();
    }

    public ZLinkBackendActorRef? ActorLookup(string actorId)
    {
        try
        {
            var actorRef = nativeSpotNode.ActorLookup(actorId);
            return actorRef.ToBackend();
        }
        catch (ZlinkConfigException ex) when (ex.Result == ZlinkConfigException.ErrorCode.NotFound)
        {
            return null;
        }
    }

    public bool JoinActor(
        ZLinkBackendActorRef actor,
        RoutingId destNodeRid,
        RoutingId destSpotRid,
        Message message,
        RequestCallback callback,
        TimeSpan? timeout)
    {
        var operation = nativeSpotNode.JoinActor(actor.ToNative(), destNodeRid, destSpotRid)
            .Message(message)
            .Flags(SendFlags.DontWait);
        if (timeout is { } value) operation = operation.Timeout(value);

        return operation.Submit((result, parts) => callback(result.Result, parts));
    }

    public bool JoinActor(
        ZLinkBackendActorRef actor,
        RoutingId destNodeRid,
        RoutingId destSpotRid,
        IReadOnlyList<Message> parts,
        ActorJoinCallback callback,
        TimeSpan? timeout)
    {
        var operation = nativeSpotNode.JoinActor(actor.ToNative(), destNodeRid, destSpotRid)
            .Messages(parts)
            .Flags(SendFlags.DontWait);

        if (timeout is { } value) operation = operation.Timeout(value);

        return operation.Submit((result, replyParts) => callback(
            new ZLinkBackendActorJoinResult(
                result.Result,
                result.JoinResultCode,
                result.Actor.ToBackend(),
                result.JoinedSpotRid,
                result.JoinEpoch,
                result.Flags),
            replyParts));
    }

    public bool JoinActorEntrySpot(
        ZLinkBackendActorRef actor,
        RoutingId destNodeRid,
        Message request,
        ActorJoinEntrySpotCallback callback,
        TimeSpan? timeout)
    {
        var operation = nativeSpotNode.JoinActorEntrySpot(actor.ToNative(), destNodeRid, request);
        if (timeout is { } value) operation = operation.Timeout(value);

        return operation.Submit((result, replyParts) => callback(
            new ZLinkBackendActorJoinEntrySpotResult(
                result.Result,
                result.JoinResultCode,
                result.Actor.ToBackend(),
                result.TargetNodeRid,
                result.JoinedSpotRid,
                result.JoinEpoch,
                result.Flags),
            replyParts));
    }

    public async ValueTask DestroyActorAsync(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        await nativeSpotNode.DestroyActor(actor.ToNative())
            .Timeout(timeout)
            .Async(cancellationToken)
            .ConfigureAwait(false);
    }

    public bool SendActorBoundSession(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSpotNode.SendActorBoundSession(actor.ToNative())
            .Messages(parts)
            .Flags(flags)
            .Submit();
    }

    public bool ForwardActorBoundSessionPart(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags)
    {
        return nativeSpotNode.ForwardActorBoundSessionPart(
            actor.ToNative(),
            sourceNodeRid,
            sourceSessionRid,
            message,
            hasMore,
            flags);
    }

    public void BindRemoteActorBoundSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid)
    {
        nativeSpotNode.BindRemoteActorBoundSession(
            actor.ToNative(),
            sourceNodeRid,
            sourceSessionRid);
    }

    public ValueTask CloseActorBoundSessionAsync(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        nativeSpotNode.CloseActorBoundSession(actor.ToNative(), timeout);
        return ValueTask.CompletedTask;
    }

    public ValueTask DisposeAsync()
    {
        return nativeSpotNode.DisposeAsync();
    }
}