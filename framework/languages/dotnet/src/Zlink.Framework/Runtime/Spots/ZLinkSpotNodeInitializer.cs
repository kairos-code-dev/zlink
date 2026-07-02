using System.Security.Cryptography;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeInitializer(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration)
{
    public async ValueTask InitializeAsync(ZLinkFrameworkRuntimeState state)
    {
        if (registration.SpotNodes.Count == 0) return;

        var channelAdapter = backendAdapterFactory.CreateChannelAdapter();
        var spotAdapter = backendAdapterFactory.CreateSpotAdapter();

        foreach (var spotNodeRegistration in registration.SpotNodes.Values)
        {
            var node = spotAdapter.CreateSpotNode(
                state.Context,
                ResolveSpotNodeMode(spotNodeRegistration));
            var nodeRoutingId = CreateNodeRoutingId(spotNodeRegistration);
            node.SetRoutingId(nodeRoutingId);
            if (nodeRoutingId.Size > 0 && spotNodeRegistration.PubSub is not null)
            {
                node.SetPublisherRoutingId(
                    ZLinkRoutingIdPolicy.Derive(nodeRoutingId, "pub"));
                node.SetSubscriberRoutingId(
                    ZLinkRoutingIdPolicy.Derive(nodeRoutingId, "sub"));
            }

            var nodeRuntime = new ZLinkSpotNodeRuntime(
                services,
                runtime,
                registration,
                spotNodeRegistration,
                state.Context,
                channelAdapter,
                node,
                spotNodeRegistration.SpotDiscoveryChannelName ?? spotNodeRegistration.SpotNodeName);

            nodeRuntime.ApplyEntrySpotRoutingIdBeforeBind();
            if (spotNodeRegistration.Router is not null
                && spotNodeRegistration.Router.BindEndpoint is { Length: > 0 } routerEndpoint)
                node.SetRouterBind(routerEndpoint);
            if (spotNodeRegistration.PubSub is not null
                && spotNodeRegistration.PubSub.BindEndpoint is { Length: > 0 } pubEndpoint)
                node.SetPubBind(pubEndpoint);

            state.SpotNodes.Add(spotNodeRegistration.SpotNodeName, nodeRuntime);
            try
            {
                ConnectManualPeers(spotNodeRegistration, nodeRuntime);

                await nodeRuntime.InitializeEntrySpotAsync().ConfigureAwait(false);
            }
            catch
            {
                state.SpotNodes.Remove(spotNodeRegistration.SpotNodeName);
                await nodeRuntime.DisposeAsync().ConfigureAwait(false);
                throw;
            }
        }
    }

    private static SpotNodeMode ResolveSpotNodeMode(ZLinkSpotNodeRegistration registration)
    {
        return (registration.Router is not null, registration.PubSub is not null) switch
        {
            (true, true) => SpotNodeMode.All,
            (true, false) => SpotNodeMode.Routed,
            (false, true) => SpotNodeMode.PubSub,
            _ => throw new ZLinkConfigurationException(
                $"SPOT node '{registration.SpotNodeName}' must enable router or pub/sub capability.")
        };
    }

    private static void ConnectManualPeers(
        ZLinkSpotNodeRegistration registration,
        ZLinkSpotNodeRuntime nodeRuntime)
    {
        foreach (var connection in registration.Router?.ManualConnections ?? [])
            _ = connection.PeerRid is { } peerRid
                ? nodeRuntime.ConnectRouterAsync(peerRid, connection.Endpoint, CancellationToken.None)
                : nodeRuntime.ConnectRouterAsync(connection.Endpoint, CancellationToken.None);

        foreach (var endpoint in registration.PubSub?.ManualConnections ?? [])
            _ = nodeRuntime.ConnectPubSubAsync(endpoint, CancellationToken.None);
    }

    private static RoutingId CreateNodeRoutingId(ZLinkSpotNodeRegistration registration)
    {
        if (registration.RoutingId.Size > 0) return registration.RoutingId;

        var bytes = RandomNumberGenerator.GetBytes(16);
        bytes[0] = 0x10;
        return RoutingId.From(bytes);
    }
}