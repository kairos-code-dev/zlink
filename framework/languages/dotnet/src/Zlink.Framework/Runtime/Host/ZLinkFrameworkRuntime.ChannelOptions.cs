namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal IZLinkClientServerChannelOptions ResolveClientServerRuntimeOptions(string channelName)
    {
        return ExecuteOperation(() =>
        {
            var channel = ResolveChannelRegistration(channelName);
            var state = GetOrStartState();
            IZLinkSocketConfig? serverSocket = null;
            IZLinkRouteConfig? serverRouting = null;
            if (channel.Server is { } server)
            {
                if (!state.ServerBundles.TryGetValue(channelName, out var bundle)
                    || bundle.Socket is not IZLinkBackendWeightedSocket weighted)
                    throw new ZLinkConfigurationException(
                        $"Channel '{channelName}' server role is not initialized or does not support weight.");
                serverSocket = new ZLinkLiveSocketConfig(this, weighted, server.SocketConfig);
                serverRouting = new ZLinkFrozenRouteConfig(server.RoutingConfig);
            }

            var clientSocket = channel.Client is { } client
                ? new ZLinkFrozenSocketConfig(client.SocketConfig)
                : null;
            var clientRouting = channel.Client is { } clientCapability
                ? new ZLinkFrozenOutboundRouteConfig(clientCapability.RoutingConfig)
                : null;
            return new ZLinkClientServerRuntimeOptions(
                serverSocket,
                serverRouting,
                clientSocket,
                clientRouting);
        });
    }

    internal IZLinkSocketConfig ResolveRouteMeshSocketConfig(string channelName)
    {
        return ExecuteOperation(() =>
        {
            if (!Registration.RouteChannels.TryGetValue(channelName, out var registration))
                throw new ZLinkConfigurationException(
                    $"Route mesh channel '{channelName}' is not registered on this node.");

            var state = GetOrStartState();
            if (!state.RouteChannels.TryGetValue(channelName, out var routeRuntime))
                throw new ZLinkConfigurationException(
                    $"Route mesh channel '{channelName}' has no serving role on this node.");

            return new ZLinkLiveSocketConfig(this, routeRuntime.ServingSocket, registration.SocketConfig);
        });
    }

    private ZLinkChannelRegistration ResolveChannelRegistration(string channelName)
    {
        if (!Registration.Channels.TryGetValue(channelName, out var channel))
            throw new ZLinkConfigurationException(
                $"Channel '{channelName}' is not registered on this node.");

        if (channel.AutoConnectType != ZLinkAutoConnectType.ClientServer)
            throw new ZLinkConfigurationException(
                $"Channel '{channelName}' is not a client-server channel.");

        return channel;
    }
}
