namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkActorRouteState(ZLinkActorRoute route)
{
    private readonly object _gate = new();
    private ZLinkActorRoute _route = route;

    public ZLinkActorRoute Snapshot
    {
        get
        {
            lock (_gate)
            {
                return _route;
            }
        }
    }

    public bool TryUpdate(
        string routerChannelId,
        RoutingId targetNodeRid,
        ulong expectedActorGeneration,
        ulong newActorGeneration)
    {
        if (expectedActorGeneration == 0 || newActorGeneration == 0)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                "Actor route update requires a concrete actor generation.",
                isRetriable: false);
        }

        lock (_gate)
        {
            if (_route.ActorGeneration == newActorGeneration
                && _route.TargetNodeRid.Equals(targetNodeRid)
                && string.Equals(_route.RouterChannelId, routerChannelId, StringComparison.Ordinal))
            {
                return true;
            }

            if (_route.ActorGeneration != expectedActorGeneration)
            {
                return false;
            }

            _route = new ZLinkActorRoute(
                routerChannelId,
                targetNodeRid,
                newActorGeneration);
            return true;
        }
    }
}
