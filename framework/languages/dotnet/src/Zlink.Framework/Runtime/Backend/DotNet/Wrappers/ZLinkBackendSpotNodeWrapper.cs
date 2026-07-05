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

    private IZLinkBackendSpot? _entrySpot;

    public IZLinkBackendSpotRouteBridge CreateRouteBridge()
    {
        return new ZLinkBackendSpotRouteBridgeWrapper(nativeSpotNode.CreateRouteBridge());
    }

    public IZLinkBackendSpot EntrySpot()
    {
        // One facade per node: every native EntrySpot() call creates and
        // registers a NEW facade, and actor-readable notifications go to
        // one arbitrary registered facade — which must be the one the
        // dispatch pump listens on. Handing out duplicates silently
        // black-holes bound-actor session relays for entry-resident actors.
        return _entrySpot ??= new ZLinkBackendSpotWrapper(nativeSpotNode.EntrySpot());
    }

    public ZLinkBackendActorRef CreateActor(string actorId, Message createRequest)
    {
        var actor = nativeSpotNode.CreateActor(actorId, createRequest);
        return EnsureConcreteActorRef(actor.Ref.ToBackend(), actorId);
    }

    public ZLinkBackendActorRef? ActorLookup(string actorId)
    {
        try
        {
            var actorRef = nativeSpotNode.ActorLookup(actorId);
            return EnsureConcreteActorRef(actorRef.ToBackend(), actorId);
        }
        catch (ZlinkConfigException ex) when (ex.Result == ZlinkConfigException.ErrorCode.NotFound)
        {
            return null;
        }
    }

    private ZLinkBackendActorRef EnsureConcreteActorRef(
        ZLinkBackendActorRef actorRef,
        string actorId)
    {
        if (!actorRef.NodeRid.IsEmpty) return actorRef;

        var nodeRid = nativeSpotNode.RoutingId;
        if (nodeRid.IsEmpty)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                $"Actor '{actorId}' was created on a SpotNode without a concrete routing id.");

        return actorRef with { NodeRid = nodeRid };
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

    public bool SendToActor(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSpotNode.SendToActor(actor.ToNative())
            .Messages(parts)
            .Flags(flags)
            .Submit();
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToActorAsync(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var operation = nativeSpotNode.RequestToActor(actor.ToNative())
            .Messages(parts);
        if (timeout is { } value) operation = operation.Timeout(value);

        return await operation.Async(cancellationToken).ConfigureAwait(false);
    }

    public void ReplyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        IReadOnlyList<Message> parts)
    {
        var info = new ActorRecvInfo(
            actor.ToNative(),
            sourceNodeRid,
            sourceSessionRid,
            requestId,
            flags);
        nativeSpotNode.ReplyActorNoBind(info, parts);
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
