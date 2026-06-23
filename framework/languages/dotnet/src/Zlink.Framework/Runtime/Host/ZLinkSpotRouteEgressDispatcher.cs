namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkSpotRouteEgressDispatcher(
    ZLinkFrameworkRegistration registration,
    Func<string, ZLinkChannelRuntimeBundle> getClientBundle,
    ZLinkSpotRouteBridgeProvider bridges)
{
    public bool CanHandle(string localEgressChannelName)
    {
        return registration.Channels.TryGetValue(localEgressChannelName, out var channel)
                && channel.SpotRouteEgress is not null
            || registration.RouteChannels.TryGetValue(localEgressChannelName, out var routeChannel)
                && routeChannel.SpotRouteEgress is not null;
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
        if (registration.Channels.TryGetValue(localEgressChannelName, out var channel)
            && channel.SpotRouteEgress is not null)
        {
            return new ClientEgressTarget(localEgressChannelName, getClientBundle, bridges);
        }

        if (registration.RouteChannels.TryGetValue(localEgressChannelName, out var routeChannel)
            && routeChannel.SpotRouteEgress is { } routeEgress)
        {
            return new RouteEgressTarget(
                localEgressChannelName,
                routeEgress.TargetSpotNodeChannelName,
                bridges);
        }

        throw new ZLinkConfigurationException(
            $"Routed SPOT egress channel '{localEgressChannelName}' is not registered.");
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

    private sealed class ClientEgressTarget(
        string localEgressChannelName,
        Func<string, ZLinkChannelRuntimeBundle> getClientBundle,
        ZLinkSpotRouteBridgeProvider bridges)
        : IEgressTarget
    {
        public async ValueTask SendAsync(
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken)
        {
            var bundle = getClientBundle(localEgressChannelName);
            var bridge = bridges.GetOrCreateClientBridge(localEgressChannelName);
            await (bundle.Submitter
                    ?? throw new InvalidOperationException("ZLink routed SPOT egress submitter is not initialized."))
                .Async(
                    parts,
                    pending => bridge.Send(
                        localEgressChannelName,
                        targetSpotRid,
                        pending,
                        SendFlags.DontWait),
                    cancellationToken)
                .ConfigureAwait(false);
        }

        public async ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            var bundle = getClientBundle(localEgressChannelName);
            var bridge = bridges.GetOrCreateClientBridge(localEgressChannelName);
            return await (bundle.Submitter
                    ?? throw new InvalidOperationException("ZLink routed SPOT egress submitter is not initialized."))
                .SubmitRequestAsync<IReadOnlyList<Message>>(
                    parts,
                    (pending, complete, fail) => bridge.Request(
                        localEgressChannelName,
                        targetSpotRid,
                        pending,
                        (result, reply) => ZLinkRawReplyCompletion.Complete(
                            result,
                            reply,
                            complete,
                            fail,
                            $"Routed SPOT egress channel '{localEgressChannelName}' request failed with result '{result}'."),
                        SendFlags.DontWait,
                        timeout),
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private sealed class RouteEgressTarget(
        string localEgressChannelName,
        string targetSpotNodeChannelName,
        ZLinkSpotRouteBridgeProvider bridges)
        : IEgressTarget
    {
        public async ValueTask SendAsync(
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken)
        {
            var targetPeerRid = await ResolveTargetPeerRidAsync().ConfigureAwait(false);
            await bridges.GetRouteChannelWithBridge(localEgressChannelName)
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
            var targetPeerRid = await ResolveTargetPeerRidAsync().ConfigureAwait(false);
            return await bridges.GetRouteChannelWithBridge(localEgressChannelName)
                .RequestToSpotPartsAsync(
                    targetPeerRid,
                    targetSpotRid,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        private ValueTask<RoutingId> ResolveTargetPeerRidAsync()
        {
            return bridges.CreateEgressResolver().ResolveTargetPeerRidAsync(
                localEgressChannelName,
                targetSpotNodeChannelName);
        }
    }
}
