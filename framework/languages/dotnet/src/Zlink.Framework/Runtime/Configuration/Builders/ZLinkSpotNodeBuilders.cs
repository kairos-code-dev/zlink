namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkSpotNodeBuilder(ZLinkSpotNodeRegistration registration)
    : IZLinkSpotNodeBuilder, IZLinkSpotMeshNodeBuilder
{
    public IZLinkSpotNodeBuilder EnableRouter(string endpoint)
    {
        var router = EnsureRouter();
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("SPOT router bind endpoint must not be empty.");
        }

        router.BindEndpoint = endpoint;
        return this;
    }

    public IZLinkSpotNodeBuilder ConnectRouter(string endpoint)
    {
        AddRouterManualConnection(endpoint, peerRid: null);
        return this;
    }

    public IZLinkSpotNodeBuilder ConnectRouter(RoutingId peerRid, string endpoint)
    {
        if (peerRid.Size == 0)
        {
            throw new ZLinkConfigurationException("Manual SPOT router peer routing id must not be empty.");
        }

        AddRouterManualConnection(endpoint, peerRid);
        return this;
    }

    public IZLinkSpotNodeBuilder SetRouterRoutingId(RoutingId routingId)
    {
        EnsureRouter().RoutingConfig.RoutingId = routingId;
        return this;
    }

    public IZLinkSocketConfig ConfigureRouterSocket()
    {
        return EnsureRouter().SocketConfig;
    }

    public IZLinkRouteConfig ConfigureRouterRouting()
    {
        return EnsureRouter().RoutingConfig;
    }

    public IZLinkSpotNodeBuilder EnablePubSub(string endpoint)
    {
        var pubSub = EnsurePubSub();
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("SPOT pub/sub bind endpoint must not be empty.");
        }

        pubSub.BindEndpoint = endpoint;
        return this;
    }

    public IZLinkSpotNodeBuilder ConnectPeerPub(string endpoint)
    {
        ZLinkChannelEndpointBuilderSupport.AddManualConnection(
            EnsurePubSub().ManualConnections,
            endpoint,
            "Manual SPOT peer PUB endpoint must not be empty.");
        return this;
    }

    public IZLinkSpotNodeBuilder ConnectPubSub(string endpoint)
    {
        return ConnectPeerPub(endpoint);
    }

    public IZLinkSpotNodeBuilder SetPubSubRoutingId(RoutingId routingId)
    {
        EnsurePubSub().RoutingId = routingId;
        return this;
    }

    public IZLinkSpotPublisherConfig ConfigurePubSubPublisher()
    {
        return EnsurePubSub().PublisherConfig;
    }

    public IZLinkSpotSubscriberConfig ConfigurePubSubSubscriber()
    {
        return EnsurePubSub().SubscriberConfig;
    }

    public IZLinkSpotNodeBuilder AttachChannelClient(string channelName)
    {
        EnsureChannelClient(channelName);
        return this;
    }

    public IZLinkSpotNodeBuilder AttachChannelClient(string channelName, string endpoint)
    {
        ZLinkChannelEndpointBuilderSupport.AddManualConnection(
            EnsureChannelClient(channelName).ManualConnections,
            endpoint,
            "Attached client/server channel endpoint must not be empty.");
        return this;
    }

    public IZLinkSocketConfig ConfigureChannelClientSocket(string channelName)
    {
        return EnsureChannelClient(channelName).SocketConfig;
    }

    public IZLinkOutboundRouteConfig ConfigureChannelClientRouting(string channelName)
    {
        return EnsureChannelClient(channelName).RoutingConfig;
    }

    public IZLinkSpotNodeBuilder AttachSpotPublisherClient(string channelName)
    {
        EnsureSpotPublisherClient(channelName);
        return this;
    }

    public IZLinkSpotNodeBuilder AttachSpotPublisherClient(string channelName, string endpoint)
    {
        ZLinkChannelEndpointBuilderSupport.AddManualConnection(
            EnsureSpotPublisherClient(channelName).ManualConnections,
            endpoint,
            "Attached SPOT publisher channel endpoint must not be empty.");
        return this;
    }

    public IZLinkSocketConfig ConfigureSpotPublisherClientSocket(string channelName)
    {
        return EnsureSpotPublisherClient(channelName).SocketConfig;
    }

    public IZLinkSpotNodeBuilder AcceptSpotRoutesFromChannel(string channelName)
    {
        EnsureAcceptedRouteChannel(channelName);
        return this;
    }

    public IZLinkSpotNodeBuilder AcceptSpotRoutesFromChannel(string channelName, string endpoint)
    {
        ZLinkChannelEndpointBuilderSupport.AddManualConnection(
            EnsureAcceptedRouteChannel(channelName).ManualConnections,
            endpoint,
            "Manual SPOT route channel endpoint must not be empty.");
        return this;
    }

    private ZLinkSpotRouterCapabilityRegistration EnsureRouter()
    {
        registration.Router ??= new ZLinkSpotRouterCapabilityRegistration();
        return registration.Router;
    }

    private void AddRouterManualConnection(string endpoint, RoutingId? peerRid)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Manual SPOT router endpoint must not be empty.");
        }

        EnsureRouter().ManualConnections.Add(new ZLinkSpotRouterManualConnectionRegistration(endpoint, peerRid));
    }

    private ZLinkSpotPubSubCapabilityRegistration EnsurePubSub()
    {
        registration.PubSub ??= new ZLinkSpotPubSubCapabilityRegistration();
        return registration.PubSub;
    }

    private ZLinkSpotChannelClientRegistration EnsureChannelClient(string channelName)
    {
        if (string.IsNullOrWhiteSpace(channelName))
        {
            throw new ZLinkConfigurationException("Attached client/server channel client name must not be empty.");
        }

        if (!registration.AttachedChannelClients.TryGetValue(channelName, out var attached))
        {
            attached = new ZLinkSpotChannelClientRegistration { ChannelName = channelName };
            registration.AttachedChannelClients.Add(channelName, attached);
        }

        return attached;
    }

    private ZLinkSpotPublisherClientRegistration EnsureSpotPublisherClient(string channelName)
    {
        if (string.IsNullOrWhiteSpace(channelName))
        {
            throw new ZLinkConfigurationException("Attached SPOT publisher channel name must not be empty.");
        }

        if (!registration.AttachedSpotPublisherClients.TryGetValue(channelName, out var attached))
        {
            attached = new ZLinkSpotPublisherClientRegistration { ChannelName = channelName };
            registration.AttachedSpotPublisherClients.Add(channelName, attached);
        }

        return attached;
    }

    private ZLinkSpotRouteChannelAcceptanceRegistration EnsureAcceptedRouteChannel(string channelName)
    {
        if (string.IsNullOrWhiteSpace(channelName))
        {
            throw new ZLinkConfigurationException("Accepted SPOT route channel name must not be empty.");
        }

        if (!registration.AcceptedSpotRouteChannels.TryAdd(
                channelName,
                new ZLinkSpotRouteChannelAcceptanceRegistration { ChannelName = channelName }))
        {
            throw new ZLinkConfigurationException(
                $"Duplicate accepted SPOT route channel '{channelName}' on node '{registration.SpotNodeName}'.");
        }

        return registration.AcceptedSpotRouteChannels[channelName];
    }

    public IZLinkEntrySpotOptions ConfigureEntrySpot()
    {
        return registration.EntrySpotOptions;
    }

    public IZLinkSpotNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot
    {
        if (!registration.SpotFactories.Add(typeof(TSpot)))
        {
            throw new ZLinkConfigurationException(
                $"Duplicate SPOT factory '{typeof(TSpot)}' on node '{registration.SpotNodeName}'.");
        }

        return this;
    }

    public IZLinkSpotNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot
    {
        if (registration.EntrySpotType is not null)
        {
            throw new ZLinkConfigurationException(
                $"Duplicate Entry Spot registry on node '{registration.SpotNodeName}'.");
        }

        registration.EntrySpotType = typeof(TEntrySpot);
        return this;
    }
}
