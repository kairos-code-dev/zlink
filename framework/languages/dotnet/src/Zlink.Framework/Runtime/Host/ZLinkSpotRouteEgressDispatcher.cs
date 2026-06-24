namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkSpotRouteEgressDispatcher(
    ZLinkFrameworkRegistration registration,
    Func<string, ZLinkRouteChannelRuntime> getRouteChannel)
{
    public bool CanHandle(string localEgressChannelName)
    {
        return registration.RouteChannels.ContainsKey(localEgressChannelName);
    }

    public ValueTask SendAsync(
        string localEgressChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return ResolveTarget(localEgressChannelName).SendAsync(
            targetSpotRid,
            parts,
            cancellationToken);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestAsync(
        string localEgressChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await ResolveTarget(localEgressChannelName).RequestAsync(
                targetSpotRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private IEgressTarget ResolveTarget(string localEgressChannelName)
    {
        if (registration.RouteChannels.ContainsKey(localEgressChannelName))
        {
            return new RouteEgressTarget(localEgressChannelName, getRouteChannel);
        }

        throw new ZLinkConfigurationException(
            $"Routed SPOT egress channel '{localEgressChannelName}' is not a registered RouteMesh channel.");
    }

    private interface IEgressTarget
    {
        ValueTask SendAsync(
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken);

        ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken);
    }

    private sealed class RouteEgressTarget(
        string localEgressChannelName,
        Func<string, ZLinkRouteChannelRuntime> getRouteChannel)
        : IEgressTarget
    {
        public async ValueTask SendAsync(
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken)
        {
            var routeChannel = getRouteChannel(localEgressChannelName);
            var targetPeerRid = ResolveTargetPeerRid(routeChannel, targetSpotRid);
            await routeChannel
                .SubmitSpotRouteSendPartsAsync(
                    targetPeerRid,
                    targetSpotRid,
                    parts,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        public async ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            var routeChannel = getRouteChannel(localEgressChannelName);
            var targetPeerRid = ResolveTargetPeerRid(routeChannel, targetSpotRid);
            return await routeChannel
                .RequestToSpotPartsAsync(
                    targetPeerRid,
                    targetSpotRid,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        private static RoutingId ResolveTargetPeerRid(
            ZLinkRouteChannelRuntime routeChannel,
            RoutingId targetSpotRid)
        {
            return routeChannel.Discovery?.ResolveSpot(targetSpotRid).OwnerNodeRid
                ?? throw new ZLinkConfigurationException(
                    $"RouteMesh channel '{routeChannel.RouterChannelId}' cannot resolve target SPOT '{targetSpotRid}'. Configure registry discovery for SPOT ownership before routed SPOT egress.");
        }
    }
}
