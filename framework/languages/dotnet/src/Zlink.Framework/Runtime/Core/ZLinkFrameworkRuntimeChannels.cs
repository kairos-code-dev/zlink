using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

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
                        SendFlags.DontWait))
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
            if (!router.RequestToSpot(
                    targetNodeRid,
                    targetSpotRid,
                    parts,
                    (result, reply) => CompleteSpotReply(
                        routerChannelId,
                        result,
                        reply,
                        completion),
                    SendFlags.DontWait,
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

    private static void CompleteSpotReply(
        string routerChannelId,
        RequestResult result,
        IReadOnlyList<Message> reply,
        TaskCompletionSource<IReadOnlyList<Message>> completion)
    {
        if (result == RequestResult.Ok)
        {
            completion.TrySetResult(reply);
            return;
        }

        ZLinkMessageParts.DisposeAll(reply);
        completion.TrySetException(new TimeoutException(
            $"Router channel '{routerChannelId}' SPOT request failed with result '{result}'."));
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
