namespace Zlink.Framework.Backend.DotNet.Wrappers;


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

    public IZLinkBackendSpot CreateSpot()
    {
        return new ZLinkBackendSpotWrapper(nativeSpotNode.CreateSpot());
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

    public void LeaveActor(
        ZLinkBackendActorRef actor,
        RoutingId currentSpotRid,
        TimeSpan timeout)
    {
        nativeSpotNode.LeaveActor(actor.ToNative(), currentSpotRid)
            .Timeout(timeout)
            .SubmitAsync()
            .GetAwaiter()
            .GetResult();
    }

    public void DestroyActor(
        ZLinkBackendActorRef actor,
        TimeSpan timeout)
    {
        nativeSpotNode.DestroyActor(actor.ToNative())
            .Timeout(timeout)
            .SubmitAsync()
            .GetAwaiter()
            .GetResult();
    }

    public ValueTask DisposeAsync() => nativeSpotNode.DisposeAsync();
}
