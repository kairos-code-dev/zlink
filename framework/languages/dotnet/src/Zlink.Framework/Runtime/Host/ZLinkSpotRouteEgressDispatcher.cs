namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkSpotRouteEgressDispatcher(
    ZLinkFrameworkRegistration registration,
    Func<string, ZLinkRouteChannelRuntime> getRouteChannel,
    Func<string, ZLinkSpotNodeRuntime?> getRouteBridgeOwner,
    Func<string, IZLinkBackendDiscovery?> getRegistrySpotDiscovery)
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
                getRouteBridgeOwner,
                getRegistrySpotDiscovery);
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
        Func<string, ZLinkSpotNodeRuntime?> getRouteBridgeOwner,
        Func<string, IZLinkBackendDiscovery?> getRegistrySpotDiscovery)
        : IEgressTarget
    {
        public bool TryResolveTargetPeerRid(
            RoutingId targetSpotRid,
            out RoutingId targetPeerRid)
        {
            var routeChannel = getRouteChannel(localEgressChannelName);
            if (routeChannel.Discovery is null)
            {
                var registryDiscovery = getRegistrySpotDiscovery(localEgressChannelName);
                if (registryDiscovery is not null)
                {
                    if (TryResolveRegistrySpotDiscovery(
                        registryDiscovery,
                        targetSpotRid,
                        out targetPeerRid))
                    {
                        return true;
                    }
                }

                return TryResolveBridgeOwnerRid(out targetPeerRid);
            }

            try
            {
                targetPeerRid = routeChannel.Discovery.ResolveSpot(targetSpotRid).OwnerNodeRid;
                return true;
            }
            catch (ZlinkConfigException error) when (error.NativeErrno is 2 or 95)
            {
                var registryDiscovery = getRegistrySpotDiscovery(localEgressChannelName);
                if (registryDiscovery is not null)
                {
                    if (TryResolveRegistrySpotDiscovery(
                        registryDiscovery,
                        targetSpotRid,
                        out targetPeerRid))
                    {
                        return true;
                    }
                }

                targetPeerRid = default;
                return false;
            }
        }

        private bool TryResolveRegistrySpotDiscovery(
            IZLinkBackendDiscovery discovery,
            RoutingId targetSpotRid,
            out RoutingId targetPeerRid)
        {
            try
            {
                targetPeerRid = discovery.ResolveSpot(targetSpotRid).OwnerNodeRid;
                return true;
            }
            catch (ZlinkConfigException error) when (error.NativeErrno is 2 or 95)
            {
                targetPeerRid = default;
                return false;
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
            if (TrySendToLocalOwner(
                    targetPeerRid,
                    targetSpotRid,
                    parts))
            {
                return;
            }

            var routeChannel = getRouteChannel(localEgressChannelName);
            if (routeChannel.TrySendViaSpotRouteBridge(
                    targetPeerRid,
                    targetSpotRid,
                    parts))
            {
                ZLinkMessageParts.DisposeAll(parts);
                return;
            }

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
            var localOwnerReply = await TryRequestToLocalOwnerAsync(
                    targetPeerRid,
                    targetSpotRid,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
            if (localOwnerReply is not null)
            {
                return localOwnerReply;
            }

            var routeChannel = getRouteChannel(localEgressChannelName);
            var bridgeReply = await TryRequestViaSpotRouteBridgeAsync(
                    routeChannel,
                    targetPeerRid,
                    targetSpotRid,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
            if (bridgeReply is not null)
            {
                ZLinkMessageParts.DisposeAll(parts);
                return bridgeReply;
            }

            return await routeChannel
                .RequestToSpotPartsAsync(
                    targetPeerRid,
                    targetSpotRid,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        private bool TrySendToLocalOwner(
            RoutingId targetPeerRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts)
        {
            var owner = getRouteBridgeOwner(localEgressChannelName);
            if (owner is null
                || owner.Node.RoutingId != targetPeerRid)
            {
                return false;
            }

            try
            {
                if (!owner.Node.EntrySpot().SendToSpot(
                        owner.Node.RoutingId,
                        targetSpotRid,
                        parts,
                        SendFlags.None))
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Route channel '{localEgressChannelName}' could not dispatch SPOT egress send to '{targetSpotRid}'.");
                }

                return true;
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }
        }

        private async ValueTask<IReadOnlyList<Message>?> TryRequestToLocalOwnerAsync(
            RoutingId targetPeerRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            var owner = getRouteBridgeOwner(localEgressChannelName);
            if (owner is null
                || owner.Node.RoutingId != targetPeerRid)
            {
                return null;
            }

            var completion = new TaskCompletionSource<IReadOnlyList<Message>>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            try
            {
                if (!owner.Node.EntrySpot().RequestToSpot(
                        owner.Node.RoutingId,
                        targetSpotRid,
                        parts,
                        (result, reply) => ZLinkRawReplyCompletion.Complete(
                            result,
                            reply,
                            completion,
                            $"SPOT egress request for '{localEgressChannelName}' failed with result '{result}'."),
                        SendFlags.None,
                        timeout))
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Route channel '{localEgressChannelName}' could not dispatch SPOT egress request to '{targetSpotRid}'.");
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
            try
            {
                return await completion.Task.ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                throw new TimeoutException(
                    $"SPOT egress request for '{localEgressChannelName}' to '{targetSpotRid}' timed out.");
            }
        }

        private static async ValueTask<IReadOnlyList<Message>?> TryRequestViaSpotRouteBridgeAsync(
            ZLinkRouteChannelRuntime routeChannel,
            RoutingId targetPeerRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            var completion = new TaskCompletionSource<IReadOnlyList<Message>>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            if (!routeChannel.TryRequestViaSpotRouteBridge(
                    targetPeerRid,
                    targetSpotRid,
                    parts,
                    (result, reply) => ZLinkRawReplyCompletion.Complete(
                        result,
                        reply,
                        completion,
                        $"SPOT route bridge request failed with result '{result}'."),
                    timeout))
            {
                return null;
            }

            using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutSource.CancelAfter(timeout);
            using var _ = timeoutSource.Token.Register(
                static state => ((TaskCompletionSource<IReadOnlyList<Message>>)state!).TrySetCanceled(),
                completion);
            try
            {
                return await completion.Task.ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                throw new TimeoutException(
                    $"SPOT route bridge request to '{targetSpotRid}' timed out.");
            }
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
