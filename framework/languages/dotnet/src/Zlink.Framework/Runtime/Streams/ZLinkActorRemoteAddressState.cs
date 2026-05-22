namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkActorRemoteAddressState(ZLinkActorRemoteAddress remoteAddress)
{
    private readonly object _gate = new();
    private ZLinkActorRemoteAddress _remoteAddress = remoteAddress;

    public ZLinkActorRemoteAddress Snapshot
    {
        get
        {
            lock (_gate)
            {
                return _remoteAddress;
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
                "Actor remote address update requires a concrete actor generation.",
                isRetriable: false);
        }

        lock (_gate)
        {
            if (_remoteAddress.ActorGeneration == newActorGeneration
                && _remoteAddress.TargetNodeRid.Equals(targetNodeRid)
                && string.Equals(_remoteAddress.RouterChannelId, routerChannelId, StringComparison.Ordinal))
            {
                return true;
            }

            if (_remoteAddress.ActorGeneration != expectedActorGeneration)
            {
                return false;
            }

            _remoteAddress = new ZLinkActorRemoteAddress(
                routerChannelId,
                targetNodeRid,
                newActorGeneration);
            return true;
        }
    }
}
