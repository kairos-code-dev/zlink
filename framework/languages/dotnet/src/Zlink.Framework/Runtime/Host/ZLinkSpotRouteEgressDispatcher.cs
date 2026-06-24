namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkSpotRouteEgressDispatcher(
    ZLinkFrameworkRegistration registration,
    Func<string, ZLinkRouteChannelRuntime> getRouteChannel,
    Func<string, ZLinkSpotNodeRuntime?> getRouteBridgeOwner)
{
    public bool CanHandle(string localEgressChannelName)
    {
        return registration.RouteChannels.ContainsKey(localEgressChannelName);
    }

    public async ValueTask<bool> TrySendAsync(
        string localEgressChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var target = ResolveTarget(localEgressChannelName);
        if (!target.TryResolveTargetPeerRid(targetSpotRid, out var targetPeerRid))
        {
            return false;
        }

        await target.SendAsync(
                targetPeerRid,
                targetSpotRid,
                parts,
                cancellationToken)
            .ConfigureAwait(false);
        return true;
    }

    public async ValueTask<SpotRouteEgressRequestResult> TryRequestAsync(
        string localEgressChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var target = ResolveTarget(localEgressChannelName);
        if (!target.TryResolveTargetPeerRid(targetSpotRid, out var targetPeerRid))
        {
            return SpotRouteEgressRequestResult.NotHandled;
        }

        var reply = await target.RequestAsync(
                targetPeerRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
        return SpotRouteEgressRequestResult.Handled(reply);
    }

    private IEgressTarget ResolveTarget(string localEgressChannelName)
    {
        if (registration.RouteChannels.ContainsKey(localEgressChannelName))
        {
            return new RouteEgressTarget(
                localEgressChannelName,
                getRouteChannel,
                getRouteBridgeOwner);
        }

        throw new ZLinkConfigurationException(
            $"Routed SPOT egress channel '{localEgressChannelName}' is not a registered RouteMesh channel.");
    }

    private interface IEgressTarget
    {
        bool TryResolveTargetPeerRid(
            RoutingId targetSpotRid,
            out RoutingId targetPeerRid);

        ValueTask SendAsync(
            RoutingId targetPeerRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken);

        ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetPeerRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken);
    }

    private sealed class RouteEgressTarget(
        string localEgressChannelName,
        Func<string, ZLinkRouteChannelRuntime> getRouteChannel,
        Func<string, ZLinkSpotNodeRuntime?> getRouteBridgeOwner)
        : IEgressTarget
    {
        public bool TryResolveTargetPeerRid(
            RoutingId targetSpotRid,
            out RoutingId targetPeerRid)
        {
            var routeChannel = getRouteChannel(localEgressChannelName);
            if (routeChannel.Discovery is null)
            {
                return TryResolveBridgeOwnerRid(out targetPeerRid);
            }

            try
            {
                targetPeerRid = routeChannel.Discovery.ResolveSpot(targetSpotRid).OwnerNodeRid;
                return true;
            }
            catch (ZlinkConfigException error) when (error.NativeErrno is 2 or 95)
            {
                return TryResolveBridgeOwnerRid(out targetPeerRid);
            }
        }

        private bool TryResolveBridgeOwnerRid(out RoutingId targetPeerRid)
        {
            var owner = getRouteBridgeOwner(localEgressChannelName);
            if (owner is null)
            {
                targetPeerRid = default;
                return false;
            }

            targetPeerRid = owner.Node.RoutingId;
            return true;
        }

        public async ValueTask SendAsync(
            RoutingId targetPeerRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken)
        {
            if (TrySendViaLocalBridgeOwner(
                    targetPeerRid,
                    targetSpotRid,
                    parts))
            {
                return;
            }

            var routeChannel = getRouteChannel(localEgressChannelName);
            await routeChannel
                .SubmitSpotRouteSendPartsAsync(
                    targetPeerRid,
                    targetSpotRid,
                    parts,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        public async ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetPeerRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            var localReply = await TryRequestViaLocalBridgeOwnerAsync(
                    targetPeerRid,
                    targetSpotRid,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
            if (localReply is not null)
            {
                return localReply;
            }

            var routeChannel = getRouteChannel(localEgressChannelName);
            return await routeChannel
                .RequestToSpotPartsAsync(
                    targetPeerRid,
                    targetSpotRid,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        private bool TrySendViaLocalBridgeOwner(
            RoutingId targetPeerRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts)
        {
            var owner = getRouteBridgeOwner(localEgressChannelName);
            if (owner is null || owner.Node.RoutingId != targetPeerRid)
            {
                return false;
            }

            try
            {
                if (!owner.Node.EntrySpot().SendToSpot(
                        targetPeerRid,
                        targetSpotRid,
                        parts,
                        SendFlags.None))
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"SPOT route bridge owner for '{localEgressChannelName}' is not ready for SPOT send.");
                }

                return true;
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }
        }

        private async ValueTask<IReadOnlyList<Message>?> TryRequestViaLocalBridgeOwnerAsync(
            RoutingId targetPeerRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            var owner = getRouteBridgeOwner(localEgressChannelName);
            if (owner is null || owner.Node.RoutingId != targetPeerRid)
            {
                return null;
            }

            var completion = new TaskCompletionSource<IReadOnlyList<Message>>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            try
            {
                if (!owner.Node.EntrySpot().RequestToSpot(
                        targetPeerRid,
                        targetSpotRid,
                        parts,
                        (result, reply) => ZLinkRawReplyCompletion.Complete(
                            result,
                            reply,
                            completion,
                            $"SPOT route bridge owner for '{localEgressChannelName}' request failed with result '{result}'."),
                        SendFlags.None,
                        timeout))
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"SPOT route bridge owner for '{localEgressChannelName}' is not ready for SPOT request.");
                }
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }

            using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutSource.CancelAfter(timeout);
            using var _ = timeoutSource.Token.Register(
                static state => ((TaskCompletionSource<IReadOnlyList<Message>>)state!).TrySetCanceled(),
                completion);
            return await completion.Task.ConfigureAwait(false);
        }
    }
}

internal sealed record SpotRouteEgressRequestResult(
    bool WasHandled,
    IReadOnlyList<Message> Reply)
{
    public static SpotRouteEgressRequestResult NotHandled { get; } = new(false, []);

    public static SpotRouteEgressRequestResult Handled(IReadOnlyList<Message> reply)
        => new(true, reply);
}
