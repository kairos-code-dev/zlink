using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal ZLinkChannelRuntimeBundle GetOrCreateClientBundle(string channelName)
    {
        return _channelFacade.GetOrCreateClientBundle(channelName);
    }

    internal ZLinkChannelRuntimeBundle GetOrCreatePublisherBundle(string channelName)
    {
        return _channelFacade.GetOrCreatePublisherBundle(channelName);
    }

    internal ZLinkRouteChannelRuntime GetRouteChannel(string routerChannelId)
    {
        return _channelFacade.GetRouteChannel(routerChannelId);
    }

    internal async ValueTask SendToSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var state = GetOrStartState();
        if (TryResolveLocalAcceptedSpotNode(
                state,
                routerChannelId,
                targetNodeRid,
                out var localSpotNode))
        {
            var entrySpot = localSpotNode.Node.EntrySpot();
            try
            {
                if (!entrySpot.SendToSpot(
                        targetNodeRid,
                        targetSpotRid,
                        parts,
                        SendFlags.None))
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Local SPOT node for route channel '{routerChannelId}' is not ready for SPOT send.");
                }
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }

            return;
        }

        if (state.RouteChannels.TryGetValue(routerChannelId, out var routeChannel))
        {
            await routeChannel.SubmitSpotRouteSendPartsAsync(
                targetNodeRid,
                targetSpotRid,
                parts,
                cancellationToken).ConfigureAwait(false);
            return;
        }

        if (state.ServerBundles.TryGetValue(routerChannelId, out var serverBundle)
            && serverBundle.Socket is IZLinkBackendRouterSocket router)
        {
            try
            {
                var bridge = serverBundle.SpotRouteBridge
                    ?? throw new ZLinkConfigurationException(
                        $"Router channel '{routerChannelId}' is not attached to a SPOT route bridge.");
                bridge.SetTargetNode(routerChannelId, targetNodeRid);
                if (!bridge.Send(
                        routerChannelId,
                        targetSpotRid,
                        parts,
                        SendFlags.None))
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Router channel '{routerChannelId}' is not ready for SPOT send.");
                }
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }

            return;
        }

        if (state.SpotNodes.TryGetValue(routerChannelId, out var spotNodeRuntime))
        {
            var entrySpot = spotNodeRuntime.Node.EntrySpot();
            try
            {
                if (!entrySpot.SendToSpot(
                        targetNodeRid,
                        targetSpotRid,
                        parts,
                        SendFlags.None))
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"SpotNode router '{routerChannelId}' is not ready for SPOT send.");
                }
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }

            return;
        }

        throw new ZLinkConfigurationException(
            $"Router-capable channel '{routerChannelId}' is not registered in this process.");
    }

    internal ValueTask SendToSpotViaEgressChannelAsync(
        string localEgressChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        if (_registration.Channels.TryGetValue(localEgressChannelName, out var channel)
            && channel.SpotRouteEgress is { } channelEgress)
        {
            return SendToSpotViaClientEgressChannelAsync(
                localEgressChannelName,
                channelEgress.TargetSpotNodeChannelName,
                targetSpotRid,
                parts,
                cancellationToken);
        }

        if (_registration.RouteChannels.TryGetValue(localEgressChannelName, out var routeChannel)
            && routeChannel.SpotRouteEgress is { } routeEgress)
        {
            return SendToSpotViaRouteEgressChannelAsync(
                localEgressChannelName,
                routeEgress.TargetSpotNodeChannelName,
                targetSpotRid,
                parts,
                cancellationToken);
        }

        throw new ZLinkConfigurationException(
            $"Routed SPOT egress channel '{localEgressChannelName}' is not registered.");
    }

    internal async ValueTask<IReadOnlyList<Message>> RequestToSpotViaEgressChannelAsync(
        string localEgressChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        if (_registration.Channels.TryGetValue(localEgressChannelName, out var channel)
            && channel.SpotRouteEgress is { } channelEgress)
        {
            return await RequestToSpotViaClientEgressChannelAsync(
                    localEgressChannelName,
                    channelEgress.TargetSpotNodeChannelName,
                    targetSpotRid,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        if (_registration.RouteChannels.TryGetValue(localEgressChannelName, out var routeChannel)
            && routeChannel.SpotRouteEgress is { } routeEgress)
        {
            return await RequestToSpotViaRouteEgressChannelAsync(
                    localEgressChannelName,
                    routeEgress.TargetSpotNodeChannelName,
                    targetSpotRid,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        throw new ZLinkConfigurationException(
            $"Routed SPOT egress channel '{localEgressChannelName}' is not registered.");
    }

    internal RoutingId ResolveAcceptedSpotRouteNodeRid(string targetSpotNodeChannelName)
    {
        var state = GetOrStartState();
        ZLinkSpotNodeRuntime? matched = null;
        foreach (var spotNode in _registration.SpotNodes.Values)
        {
            if (!spotNode.AcceptedSpotRouteChannels.ContainsKey(targetSpotNodeChannelName)
                || !state.SpotNodes.TryGetValue(spotNode.SpotNodeName, out var runtime))
            {
                continue;
            }

            if (matched is not null)
            {
                throw new ZLinkConfigurationException(
                    $"Routed SPOT target channel '{targetSpotNodeChannelName}' is accepted by multiple SPOT nodes in this process.");
            }

            matched = runtime;
        }

        return matched?.Node.RoutingId
            ?? throw new ZLinkConfigurationException(
                $"Routed SPOT target channel '{targetSpotNodeChannelName}' is not accepted by a SPOT node in this process.");
    }

    private async ValueTask SendToSpotViaClientEgressChannelAsync(
        string localEgressChannelName,
        string targetSpotNodeChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var bundle = GetOrCreateClientBundle(localEgressChannelName);
        var bridge = GetOrCreateClientSpotRouteBridge(localEgressChannelName);
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

    private async ValueTask<IReadOnlyList<Message>> RequestToSpotViaClientEgressChannelAsync(
        string localEgressChannelName,
        string targetSpotNodeChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var bundle = GetOrCreateClientBundle(localEgressChannelName);
        var bridge = GetOrCreateClientSpotRouteBridge(localEgressChannelName);
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

    private IZLinkBackendSpotRouteBridge GetOrCreateClientSpotRouteBridge(
        string localEgressChannelName)
    {
        var state = GetOrStartState();
        var bundle = GetOrCreateClientBundle(localEgressChannelName);
        if (bundle.SpotRouteBridge is { } existing)
        {
            return existing;
        }

        lock (state.SyncRoot)
        {
            if (bundle.SpotRouteBridge is { } lockedExisting)
            {
                return lockedExisting;
            }

            var owner = ResolveSpotRouteBridgeOwner(state);
            var bridge = owner.Node.CreateRouteBridge();
            try
            {
                bridge.AttachDealerChannel(
                    localEgressChannelName,
                    (IZLinkBackendDealerSocket)bundle.Socket,
                    new SpotRouteBridgeEndpointOptions
                    {
                        Capabilities = SpotRouteBridgeEndpointCapabilities.RouteOnly
                    });
            }
            catch
            {
                _ = bridge.DisposeAsync();
                throw;
            }

            bundle.SpotRouteBridge = bridge;
            return bridge;
        }
    }

    private ZLinkSpotNodeRuntime ResolveSpotRouteBridgeOwner(ZLinkFrameworkRuntimeState state)
    {
        foreach (var registration in _registration.SpotNodes.Values)
        {
            if (registration.Router is null)
            {
                continue;
            }

            if (state.SpotNodes.TryGetValue(registration.SpotNodeName, out var runtime))
            {
                return runtime;
            }
        }

        throw new ZLinkConfigurationException(
            "Routed SPOT egress through a client/server channel requires a router-capable SPOT node in this process.");
    }

    private async ValueTask SendToSpotViaRouteEgressChannelAsync(
        string localEgressChannelName,
        string targetSpotNodeChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var targetPeerRid = await CreateSpotRouteEgressResolver().ResolveTargetPeerRidAsync(
                localEgressChannelName,
                targetSpotNodeChannelName)
            .ConfigureAwait(false);
        await GetRouteChannel(localEgressChannelName)
            .SubmitSpotRouteSendPartsAsync(
                targetPeerRid,
                targetSpotRid,
                parts,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<IReadOnlyList<Message>> RequestToSpotViaRouteEgressChannelAsync(
        string localEgressChannelName,
        string targetSpotNodeChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var targetPeerRid = await CreateSpotRouteEgressResolver().ResolveTargetPeerRidAsync(
                localEgressChannelName,
                targetSpotNodeChannelName)
            .ConfigureAwait(false);
        return await GetRouteChannel(localEgressChannelName)
            .RequestToSpotPartsAsync(
                targetPeerRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private ZLinkSpotRouteEgressResolver CreateSpotRouteEgressResolver()
    {
        return new ZLinkSpotRouteEgressResolver(
            _registration,
            _backendAdapterFactory,
            GetOrStartState,
            GetRouteChannel);
    }

    internal async ValueTask<IReadOnlyList<Message>> RequestToSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var state = GetOrStartState();
        if (TryResolveLocalAcceptedSpotNode(
                state,
                routerChannelId,
                targetNodeRid,
                out var localSpotNode))
        {
            return await RequestToSpotViaSpotNodeRouterAsync(
                routerChannelId,
                localSpotNode.Node.EntrySpot(),
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken).ConfigureAwait(false);
        }

        if (state.RouteChannels.TryGetValue(routerChannelId, out var routeChannel))
        {
            return await routeChannel.RequestToSpotPartsAsync(
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken).ConfigureAwait(false);
        }

        if (state.ServerBundles.TryGetValue(routerChannelId, out var serverBundle)
            && serverBundle.Socket is IZLinkBackendRouterSocket router)
        {
            return await RequestToSpotViaServerRouterAsync(
                routerChannelId,
                serverBundle.SpotRouteBridge
                    ?? throw new ZLinkConfigurationException(
                        $"Router channel '{routerChannelId}' is not attached to a SPOT route bridge."),
                serverBundle.ReceiveGate,
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken).ConfigureAwait(false);
        }

        if (state.SpotNodes.TryGetValue(routerChannelId, out var spotNodeRuntime))
        {
            return await RequestToSpotViaSpotNodeRouterAsync(
                routerChannelId,
                spotNodeRuntime.Node.EntrySpot(),
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken).ConfigureAwait(false);
        }

        throw new ZLinkConfigurationException(
            $"Router-capable channel '{routerChannelId}' is not registered in this process.");
    }

    private bool TryResolveLocalAcceptedSpotNode(
        ZLinkFrameworkRuntimeState state,
        string routerChannelId,
        RoutingId targetNodeRid,
        out ZLinkSpotNodeRuntime spotNodeRuntime)
    {
        foreach (var candidate in state.SpotNodes.Values)
        {
            if (candidate.Node.RoutingId == targetNodeRid
                && candidate.Registration.AcceptedSpotRouteChannels.ContainsKey(routerChannelId))
            {
                spotNodeRuntime = candidate;
                return true;
            }
        }

        spotNodeRuntime = null!;
        return false;
    }

    private static async ValueTask<IReadOnlyList<Message>> RequestToSpotViaSpotNodeRouterAsync(
        string routerChannelId,
        IZLinkBackendSpot entrySpot,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var completion = new TaskCompletionSource<IReadOnlyList<Message>>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        try
        {
            if (!entrySpot.RequestToSpot(
                    targetNodeRid,
                    targetSpotRid,
                    parts,
                    (result, reply) => ZLinkRawReplyCompletion.Complete(
                        result,
                        reply,
                        completion,
                        $"SpotNode router '{routerChannelId}' SPOT request failed with result '{result}'."),
                    SendFlags.None,
                    timeout))
            {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"SpotNode router '{routerChannelId}' is not ready for SPOT request.");
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

    private static async ValueTask<IReadOnlyList<Message>> RequestToSpotViaServerRouterAsync(
        string routerChannelId,
        IZLinkBackendSpotRouteBridge bridge,
        SemaphoreSlim receiveGate,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var completion = new TaskCompletionSource<IReadOnlyList<Message>>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        await receiveGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            try
            {
                bridge.SetTargetNode(routerChannelId, targetNodeRid);
                if (!bridge.Request(
                        routerChannelId,
                        targetSpotRid,
                        parts,
                        (result, reply) => ZLinkRawReplyCompletion.Complete(
                            result,
                            reply,
                            completion,
                            $"Router channel '{routerChannelId}' SPOT request failed with result '{result}'."),
                        SendFlags.None,
                        timeout))
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Router channel '{routerChannelId}' is not ready for SPOT request.");
                }
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }

            using var _ = cancellationToken.Register(
                static state => ((TaskCompletionSource<IReadOnlyList<Message>>)state!)
                    .TrySetCanceled(),
                completion);

            return await completion.Task.ConfigureAwait(false);
        }
        finally
        {
            receiveGate.Release();
        }
    }

    internal IZLinkBackendSocket GetMonitoringSocket(string sourceName)
    {
        return _channelFacade.GetMonitoringSocket(sourceName);
    }
}
