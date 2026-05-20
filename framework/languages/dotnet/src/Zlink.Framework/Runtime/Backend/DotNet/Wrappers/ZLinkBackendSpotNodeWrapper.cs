namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendSpotNodeWrapper(SpotNode nativeSpotNode) : IZLinkBackendSpotNode
{
    public object NativeInstance => nativeSpotNode;

    public RoutingId RoutingId => nativeSpotNode.RoutingId;

    public void SetRoutingId(RoutingId routingId)
    {
        nativeSpotNode.SetRoutingId(routingId);
    }

    public void Bind(string endpoint)
    {
        nativeSpotNode.Bind(endpoint);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSpotNode.AttachDiscovery(discovery.RequireNative<Discovery>());
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
            discovery.RequireNative<Discovery>());
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

    public ZLinkSpotNodeStatus StatusSnapshot()
    {
        return nativeSpotNode.StatusSnapshot().ToFramework();
    }

    public IReadOnlyList<ZLinkSpotNodePeerEntry> PeersSnapshot()
    {
        return nativeSpotNode.PeersSnapshot()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkSpotNodeSubjectEntry> SubjectsSnapshot()
    {
        return nativeSpotNode.SubjectsSnapshot()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public void AttachChannelDealer(IZLinkBackendDiscovery discovery, IZLinkBackendDealerSocket dealer)
    {
        nativeSpotNode.AttachChannelDealer(
            discovery.RequireNative<Discovery>(),
            dealer.RequireNative<DealerSocket>());
    }

    public void AttachChannelDealerManual(string channelName, IZLinkBackendDealerSocket dealer)
    {
        nativeSpotNode.AttachChannelDealerManual(
            channelName,
            dealer.RequireNative<DealerSocket>());
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
        RequestCallback callback,
        TimeSpan? timeout)
    {
        var operation = nativeSpotNode.JoinActor(actor.ToNative(), destNodeRid, destSpotRid)
            .Messages(parts)
            .Flags(SendFlags.DontWait);

        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

        return operation.Submit((result, replyParts) => callback(result.Result, replyParts));
    }

    public async ValueTask LeaveActorAsync(
        ZLinkBackendActorRef actor,
        RoutingId currentSpotRid,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        await nativeSpotNode.LeaveActor(actor.ToNative(), currentSpotRid)
            .Timeout(timeout)
            .SubmitAsync(cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask DestroyActorAsync(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        await nativeSpotNode.DestroyActor(actor.ToNative())
            .Timeout(timeout)
            .SubmitAsync(cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask DisposeAsync() => nativeSpotNode.DisposeAsync();
}
