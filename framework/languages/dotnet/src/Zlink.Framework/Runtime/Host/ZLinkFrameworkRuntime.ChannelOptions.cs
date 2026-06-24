using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Channels;
using Zlink.Framework.Runtime.Configuration;

namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal IZLinkSocketConfig ResolveClientServerServerSocketConfig(string channelName)
    {
        var channel = ResolveChannelRegistration(channelName);
        var state = GetOrStartState();
        if (channel.Server is null || !state.ServerBundles.TryGetValue(channelName, out var bundle))
        {
            throw new ZLinkConfigurationException(
                $"Channel '{channelName}' has no client-server server role on this node.");
        }

        if (bundle.Socket is not IZLinkBackendWeightedSocket weighted)
        {
            throw new ZLinkConfigurationException(
                $"Channel '{channelName}' server socket does not support weight.");
        }

        return new ZLinkLiveSocketConfig(weighted, channel.Server.SocketConfig);
    }

    internal IZLinkSocketConfig ResolveRouteMeshSocketConfig(string channelName)
    {
        if (!_registration.RouteChannels.TryGetValue(channelName, out var registration))
        {
            throw new ZLinkConfigurationException(
                $"Route mesh channel '{channelName}' is not registered on this node.");
        }

        var state = GetOrStartState();
        if (!state.RouteChannels.TryGetValue(channelName, out var routeRuntime))
        {
            throw new ZLinkConfigurationException(
                $"Route mesh channel '{channelName}' has no serving role on this node.");
        }

        return new ZLinkLiveSocketConfig(routeRuntime.ServingSocket, registration.SocketConfig);
    }

    private ZLinkChannelRegistration ResolveChannelRegistration(string channelName)
    {
        if (!_registration.Channels.TryGetValue(channelName, out var channel))
        {
            throw new ZLinkConfigurationException(
                $"Channel '{channelName}' is not registered on this node.");
        }

        if (channel.AutoConnectType != ZLinkAutoConnectType.ClientServer)
        {
            throw new ZLinkConfigurationException(
                $"Channel '{channelName}' is not a client-server channel.");
        }

        return channel;
    }
}
