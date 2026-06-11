namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendSpotNodeWrapper(ISpotNode nativeSpotNode) : IZLinkBackendSpotNode
{
    public object NativeInstance => nativeSpotNode;

    public RoutingId RoutingId => nativeSpotNode.RoutingId;

    public void SetRoutingId(RoutingId routingId)
    {
        nativeSpotNode.SetRoutingId(routingId);
    }

    public void SetRouterBind(string endpoint)
    {
        nativeSpotNode.SetRouterBind(endpoint);
    }

    public void SetPubBind(string endpoint)
    {
        nativeSpotNode.SetPubBind(endpoint);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSpotNode.AttachDiscovery(discovery.RequireNative<IDiscovery>());
    }

    public void ConnectPeer(string endpoint)
    {
        nativeSpotNode.ConnectPeer(endpoint);
    }

    public void DisconnectPeer(string endpoint)
    {
        nativeSpotNode.DisconnectPeer(endpoint);
    }

    public void ConnectRouterChannelPeer(string channelName, string endpoint)
    {
        nativeSpotNode.ConnectRouterChannelPeer(channelName, endpoint);
    }

    public void ConnectRouterChannelPeerRid(
        string channelName,
        RoutingId peerRid,
        string endpoint)
    {
        nativeSpotNode.ConnectRouterChannelPeerRid(channelName, peerRid, endpoint);
    }

    public void DisconnectRouterChannelPeer(string channelName, string endpoint)
    {
        nativeSpotNode.DisconnectRouterChannelPeer(channelName, endpoint);
    }

    public void DisconnectRouterChannelPeerRid(string channelName, RoutingId peerRid)
    {
        nativeSpotNode.DisconnectRouterChannelPeerRid(channelName, peerRid);
    }

    public void AttachSpotRouteChannelDiscovery(string channelName, IZLinkBackendDiscovery discovery)
    {
        nativeSpotNode.AttachSpotRouteChannelDiscovery(
            channelName,
            discovery.RequireNative<IDiscovery>());
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

    public void AttachChannelDealer(IZLinkBackendDiscovery discovery, IZLinkBackendDealerSocket dealer)
    {
        nativeSpotNode.AttachChannelDealer(
            discovery.RequireNative<IDiscovery>(),
            dealer.RequireNative<IDealerSocket>());
    }

    public void AttachChannelDealerManual(string channelName, IZLinkBackendDealerSocket dealer)
    {
        nativeSpotNode.AttachChannelDealerManual(
            channelName,
            dealer.RequireNative<IDealerSocket>());
    }

    public IZLinkBackendSpot EntrySpot()
    {
        return new ZLinkBackendSpotWrapper(nativeSpotNode.EntrySpot());
    }

    public ZLinkBackendActorRef CreateActor(string actorId)
    {
        var actor = nativeSpotNode.CreateActor(actorId);
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
        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

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

        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

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
        ActorJoinEntrySpotCallback callback,
        TimeSpan? timeout)
    {
        var operation = nativeSpotNode.JoinActorEntrySpot(actor.ToNative(), destNodeRid);
        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

        return operation.Submit(result => callback(
            new ZLinkBackendActorJoinEntrySpotResult(
                result.Result,
                result.Actor.ToBackend(),
                result.TargetNodeRid,
                result.JoinEpoch,
                result.Flags)));
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

    public ValueTask CloseActorBoundSessionAsync(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        nativeSpotNode.CloseActorBoundSession(actor.ToNative(), timeout);
        return ValueTask.CompletedTask;
    }

    public ValueTask DisposeAsync() => nativeSpotNode.DisposeAsync();
}
