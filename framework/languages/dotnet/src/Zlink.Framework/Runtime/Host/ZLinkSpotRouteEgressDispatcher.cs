namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkSpotRouteEgressDispatcher(
    ZLinkFrameworkRegistration registration,
    Func<string, ZLinkRouteChannelRuntime> getRouteChannel,
    Func<string, ZLinkSpotNodeRuntime?> getRouteBridgeOwner,
    Func<ZLinkSpotLocationRidResolver?> getSpotLocationResolver,
    Func<IZLinkPeerLocationResolver?>? getPeerResolver = null)
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
        if (await target.ResolveTargetPeerRidAsync(targetSpotRid, cancellationToken)
                .ConfigureAwait(false) is not { } targetPeerRid)
            return false;

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
        if (await target.ResolveTargetPeerRidAsync(targetSpotRid, cancellationToken)
                .ConfigureAwait(false) is not { } targetPeerRid)
            return SpotRouteEgressRequestResult.NotHandled;

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
            return new RouteEgressTarget(
                localEgressChannelName,
                getRouteChannel,
                getRouteBridgeOwner,
                getSpotLocationResolver,
                getPeerResolver);

        throw new ZLinkConfigurationException(
            $"Routed SPOT egress channel '{localEgressChannelName}' is not a registered RouteMesh channel.");
    }

    private interface IEgressTarget
    {
        ValueTask<RoutingId?> ResolveTargetPeerRidAsync(
            RoutingId targetSpotRid,
            CancellationToken cancellationToken);

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
        Func<ZLinkSpotLocationRidResolver?> getSpotLocationResolver,
        Func<IZLinkPeerLocationResolver?>? getPeerResolver)
        : IEgressTarget
    {
        /// <summary>
        /// A live route mesh row for the target node in this channel's mesh
        /// means auto connect dials it directly, so the route socket is the
        /// delivery path; the spot route bridge only serves topologies where
        /// no direct route link exists.
        /// </summary>
        private async ValueTask<bool> IsDirectRoutePeerAsync(
            RoutingId targetPeerRid,
            CancellationToken cancellationToken)
        {
            if (getPeerResolver?.Invoke() is not { } peers)
            {
                return false;
            }

            try
            {
                var rows = await peers.ListPeersAsync(
                        new ZLinkPeerLocationFilter(
                            AutoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
                            MeshName: localEgressChannelName,
                            NodeRid: targetPeerRid),
                        cancellationToken: cancellationToken)
                    .ConfigureAwait(false);
                return rows.Count > 0;
            }
            catch (Exception)
            {
                // A store outage never breaks delivery: fall back to the
                // bridge-first order.
                return false;
            }
        }

        public async ValueTask<RoutingId?> ResolveTargetPeerRidAsync(
            RoutingId targetSpotRid,
            CancellationToken cancellationToken)
        {
            // The location row is authoritative when the store is enabled
            // (it may point off-node); the local route-bridge owner is the
            // fallback, mirroring the removed discovery-first ordering.
            if (getSpotLocationResolver() is { } resolver)
            {
                var row = await resolver.ResolveAsync(targetSpotRid, cancellationToken)
                    .ConfigureAwait(false);
                if (row is not null) return row.NodeRid;
            }

            return TryResolveBridgeOwnerRid(out var targetPeerRid)
                ? targetPeerRid
                : null;
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
                return;

            var routeChannel = getRouteChannel(localEgressChannelName);
            if (await IsDirectRoutePeerAsync(targetPeerRid, cancellationToken).ConfigureAwait(false))
            {
                await routeChannel
                    .SubmitSpotRouteSendPartsAsync(
                        targetPeerRid,
                        targetSpotRid,
                        parts,
                        cancellationToken)
                    .ConfigureAwait(false);
                return;
            }


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
            if (localOwnerReply is not null) return localOwnerReply;

            var routeChannel = getRouteChannel(localEgressChannelName);
            if (await IsDirectRoutePeerAsync(targetPeerRid, cancellationToken).ConfigureAwait(false))
            {
                return await routeChannel
                    .RequestToSpotPartsAsync(
                        targetPeerRid,
                        targetSpotRid,
                        parts,
                        timeout,
                        cancellationToken)
                    .ConfigureAwait(false);
            }

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

        private bool TrySendToLocalOwner(
            RoutingId targetPeerRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts)
        {
            var owner = getRouteBridgeOwner(localEgressChannelName);
            if (owner is null
                || owner.Node.RoutingId != targetPeerRid)
                return false;

            try
            {
                if (!owner.Node.EntrySpot().SendToSpot(
                        owner.Node.RoutingId,
                        targetSpotRid,
                        parts,
                        SendFlags.None))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Route channel '{localEgressChannelName}' could not dispatch SPOT egress send to '{targetSpotRid}'.");

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
                return null;

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
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Route channel '{localEgressChannelName}' could not dispatch SPOT egress request to '{targetSpotRid}'.");
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
                return null;

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
    {
        return new SpotRouteEgressRequestResult(true, reply);
    }
}
