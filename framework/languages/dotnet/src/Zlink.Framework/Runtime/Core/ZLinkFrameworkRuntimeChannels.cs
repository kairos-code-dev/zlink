using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.Runtime.Core;

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

    internal async ValueTask SendSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var state = GetOrStartState();
        if (state.RouteChannels.TryGetValue(routerChannelId, out var routeChannel))
        {
            await routeChannel.SubmitSpotSendPartsAsync(
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
                if (!router.SendToSpot(
                        targetNodeRid,
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

        throw new ZLinkConfigurationException(
            $"Router-capable channel '{routerChannelId}' is not registered in this process.");
    }

    internal ValueTask SendSpotViaEgressChannelAsync(
        string localEgressChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        if (_registration.Channels.TryGetValue(localEgressChannelName, out var channel)
            && channel.SpotRouteEgress is { } channelEgress)
        {
            return SendSpotViaClientEgressChannelAsync(
                localEgressChannelName,
                channelEgress.TargetSpotNodeChannelName,
                targetSpotRid,
                parts,
                cancellationToken);
        }

        if (_registration.RouteChannels.TryGetValue(localEgressChannelName, out var routeChannel)
            && routeChannel.SpotRouteEgress is { } routeEgress)
        {
            return SendSpotViaRouteEgressChannelAsync(
                localEgressChannelName,
                routeEgress.TargetSpotNodeChannelName,
                targetSpotRid,
                parts,
                cancellationToken);
        }

        throw new ZLinkConfigurationException(
            $"Routed SPOT egress channel '{localEgressChannelName}' is not registered.");
    }

    internal async ValueTask<IReadOnlyList<Message>> RequestSpotViaEgressChannelAsync(
        string localEgressChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        if (_registration.Channels.TryGetValue(localEgressChannelName, out var channel)
            && channel.SpotRouteEgress is { } channelEgress)
        {
            return await RequestSpotViaClientEgressChannelAsync(
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
            return await RequestSpotViaRouteEgressChannelAsync(
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

    private async ValueTask SendSpotViaClientEgressChannelAsync(
        string localEgressChannelName,
        string targetSpotNodeChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var bundle = GetOrCreateClientBundle(localEgressChannelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var relayParts = ZLinkRoutedSpotRelayPackets.CreateRelayParts(
            ZLinkMessageKind.Command,
            targetSpotNodeChannelName,
            targetSpotRid,
            parts);
        await (bundle.Submitter
                ?? throw new InvalidOperationException("ZLink routed SPOT egress submitter is not initialized."))
            .SubmitAsync(
                relayParts,
                pending => dealer.Send(pending, SendFlags.DontWait),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<IReadOnlyList<Message>> RequestSpotViaClientEgressChannelAsync(
        string localEgressChannelName,
        string targetSpotNodeChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var bundle = GetOrCreateClientBundle(localEgressChannelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var relayParts = ZLinkRoutedSpotRelayPackets.CreateRelayParts(
            ZLinkMessageKind.Request,
            targetSpotNodeChannelName,
            targetSpotRid,
            parts,
            timeout);
        return await (bundle.Submitter
                ?? throw new InvalidOperationException("ZLink routed SPOT egress submitter is not initialized."))
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                relayParts,
                (pending, complete, fail) => dealer.Request(
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

    private ValueTask SendSpotViaRouteEgressChannelAsync(
        string localEgressChannelName,
        string targetSpotNodeChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var relayHeader = ZLinkRoutedSpotRelayPackets.CreateRelayHeader(
            ZLinkMessageKind.Command,
            targetSpotNodeChannelName);
        var relayPayload = ZLinkRoutedSpotRelayPackets.CreateRelayPayloadParts(
            targetSpotRid,
            parts);
        var targetPeerRid = ResolveRouteEgressTargetPeerRid(targetSpotNodeChannelName);
        return GetRouteChannel(localEgressChannelName).SubmitSendPartsAsync(
            targetPeerRid,
            relayHeader,
            relayPayload,
            cancellationToken);
    }

    private async ValueTask<IReadOnlyList<Message>> RequestSpotViaRouteEgressChannelAsync(
        string localEgressChannelName,
        string targetSpotNodeChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var relayHeader = ZLinkRoutedSpotRelayPackets.CreateRelayHeader(
            ZLinkMessageKind.Request,
            targetSpotNodeChannelName,
            timeout);
        var relayPayload = ZLinkRoutedSpotRelayPackets.CreateRelayPayloadParts(
            targetSpotRid,
            parts);
        var targetPeerRid = ResolveRouteEgressTargetPeerRid(targetSpotNodeChannelName);
        var reply = await GetRouteChannel(localEgressChannelName)
            .RequestPartsAsync<ZLinkRoutedSpotRelayReply>(
                targetPeerRid,
                relayHeader,
                relayPayload,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
        return reply.ToMessages();
    }

    private RoutingId ResolveRouteEgressTargetPeerRid(string targetSpotNodeChannelName)
    {
        if (_registration.RouteChannels.TryGetValue(targetSpotNodeChannelName, out var targetRoute)
            && targetRoute.RoutingConfig.RoutingId.Size > 0)
        {
            return targetRoute.RoutingConfig.RoutingId;
        }

        throw new ZLinkConfigurationException(
            $"Routed SPOT route mesh egress target channel '{targetSpotNodeChannelName}' does not expose a target route peer routing id.");
    }

    internal async ValueTask<IReadOnlyList<Message>> RequestSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var state = GetOrStartState();
        if (state.RouteChannels.TryGetValue(routerChannelId, out var routeChannel))
        {
            return await routeChannel.RequestSpotPartsAsync(
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken).ConfigureAwait(false);
        }

        if (state.ServerBundles.TryGetValue(routerChannelId, out var serverBundle)
            && serverBundle.Socket is IZLinkBackendRouterSocket router)
        {
            return await RequestSpotViaServerRouterAsync(
                routerChannelId,
                router,
                serverBundle.ReceiveGate,
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken).ConfigureAwait(false);
        }

        throw new ZLinkConfigurationException(
            $"Router-capable channel '{routerChannelId}' is not registered in this process.");
    }

    private static async ValueTask<IReadOnlyList<Message>> RequestSpotViaServerRouterAsync(
        string routerChannelId,
        IZLinkBackendRouterSocket router,
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
                if (!router.RequestToSpot(
                        targetNodeRid,
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

    internal async ValueTask<IZLinkEndpointConnections> GetClientConnectionsAsync(
        string channelName,
        CancellationToken cancellationToken)
    {
        return await _channelFacade.GetClientConnectionsAsync(channelName, cancellationToken);
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSubscriberConnectionsAsync(
        string channelName,
        CancellationToken cancellationToken)
    {
        return await _channelFacade.GetSubscriberConnectionsAsync(channelName, cancellationToken);
    }

    internal IZLinkBackendSocket GetMonitoringSocket(string sourceName)
    {
        return _channelFacade.GetMonitoringSocket(sourceName);
    }
}
