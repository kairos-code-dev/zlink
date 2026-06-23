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

    internal ValueTask SubmitRouteSendAsync<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        TMessage message,
        CancellationToken cancellationToken)
    {
        if (_spotRouteEgress.CanHandle(routerChannelId))
        {
            var header = ZLinkClientCallCodec.CreateEnvelope(
                ZLinkMessageKind.Command,
                routerChannelId,
                packetName);
            var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
                header,
                message,
                _registration.Codecs);
            return _spotRouteEgress.SendAsync(
                routerChannelId,
                targetNodeRid,
                parts,
                cancellationToken);
        }

        return GetRouteChannel(routerChannelId).SubmitSendAsync(
            targetNodeRid,
            packetName,
            message,
            cancellationToken);
    }

    internal async ValueTask<TReply> SubmitRouteRequestAsync<TRequest, TReply>(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        TRequest request,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        if (!_spotRouteEgress.CanHandle(routerChannelId))
        {
            return await GetRouteChannel(routerChannelId).RequestAsync<TRequest, TReply>(
                    targetNodeRid,
                    packetName,
                    request,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            routerChannelId,
            packetName,
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
            header,
            request,
            _registration.Codecs);
        var reply = await _spotRouteEgress.RequestAsync(
                routerChannelId,
                targetNodeRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "Route SPOT reply was empty.",
            $"Route SPOT request failed for '{packetName}'.",
            _registration.Codecs);
    }

    internal async ValueTask SendToSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        await _spotRouteRouter.SendAsync(
            routerChannelId,
            targetNodeRid,
            targetSpotRid,
            parts,
            cancellationToken).ConfigureAwait(false);
    }

    internal RoutingId ResolveAcceptedSpotRouteNodeRid(string targetSpotNodeChannelName)
    {
        return _spotRouteRouter.ResolveAcceptedSpotRouteNodeRid(targetSpotNodeChannelName);
    }

    internal async ValueTask<IReadOnlyList<Message>> RequestToSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await _spotRouteRouter.RequestAsync(
                routerChannelId,
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal IZLinkBackendSocket GetMonitoringSocket(string sourceName)
    {
        return _channelFacade.GetMonitoringSocket(sourceName);
    }
}
