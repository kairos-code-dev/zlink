namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkSpotNodeBuilder(ZLinkSpotNodeRegistration registration)
    : IZLinkSpotNodeBuilder, IZLinkSpotMeshNodeBuilder
{
    public void EnableRouter(Action<ISpotRouterCapabilityBuilder>? configure = null)
    {
        registration.Router ??= new ZLinkSpotRouterCapabilityRegistration();
        configure?.Invoke(new ZLinkSpotRouterCapabilityBuilder(registration.Router));
    }

    public void EnablePubSub(Action<ISpotPubSubCapabilityBuilder>? configure = null)
    {
        registration.PubSub ??= new ZLinkSpotPubSubCapabilityRegistration();
        configure?.Invoke(new ZLinkSpotPubSubCapabilityBuilder(registration.PubSub));
    }

    public void AttachChannelClient(
        string channelName,
        Action<ISpotChannelClientCapabilityBuilder>? configure = null)
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

        configure?.Invoke(new ZLinkSpotChannelClientCapabilityBuilder(attached));
    }

    public void AttachSpotPublisherClient(
        string channelName,
        Action<ISpotPublisherClientCapabilityBuilder>? configure = null)
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

        configure?.Invoke(new ZLinkSpotPublisherClientCapabilityBuilder(attached));
    }

    public void AcceptSpotRoutesFromChannel(
        string channelName,
        Action<IZLinkSpotRouteChannelAcceptanceBuilder>? configure = null)
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

        configure?.Invoke(new ZLinkSpotRouteChannelAcceptanceBuilder(
            registration.AcceptedSpotRouteChannels[channelName]));
    }

    public void ConfigureEntrySpot(Action<IZLinkEntrySpotOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(configure);
        configure(registration.EntrySpotOptions);
    }

    public void AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot
    {
        if (!registration.SpotFactories.Add(typeof(TSpot)))
        {
            throw new ZLinkConfigurationException(
                $"Duplicate SPOT factory '{typeof(TSpot)}' on node '{registration.SpotNodeName}'.");
        }
    }

    public void AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot
    {
        if (registration.EntrySpotType is not null)
        {
            throw new ZLinkConfigurationException(
                $"Duplicate Entry Spot registry on node '{registration.SpotNodeName}'.");
        }

        registration.EntrySpotType = typeof(TEntrySpot);
    }
}

internal sealed class ZLinkSpotRouteChannelAcceptanceBuilder(
    ZLinkSpotRouteChannelAcceptanceRegistration registration)
    : IZLinkSpotRouteChannelAcceptanceBuilder
{
    public void UseManualConnections(Action<IZLinkEndpointConnections> configure)
    {
        configure(new ZLinkSpotRouteChannelConnections(registration.ManualConnections));
    }
}

internal sealed class ZLinkSpotRouteChannelConnections(ICollection<string> endpoints)
    : IZLinkEndpointConnections
{
    public void Connect(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Manual SPOT route channel endpoint must not be empty.");
        }

        endpoints.Add(endpoint);
    }

    public void Disconnect(string endpoint)
    {
        endpoints.Remove(endpoint);
    }

    public IReadOnlyList<string> ListConnections()
    {
        return endpoints.ToArray();
    }
}

internal sealed class ZLinkSpotRouterCapabilityBuilder(ZLinkSpotRouterCapabilityRegistration registration)
    : ISpotRouterCapabilityBuilder
{
    public void SetRouterBind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("SPOT router bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public void SetRoutingId(RoutingId routingId)
    {
        registration.RoutingConfig.RoutingId = routingId;
    }

    public void ConfigureSocket(Action<IZLinkSocketConfig> configure)
    {
        configure(registration.SocketConfig);
    }

    public void ConfigureRouting(Action<IZLinkRouteConfig> configure)
    {
        configure(registration.RoutingConfig);
    }

    public void UseManualConnections(Action<IZLinkEndpointConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}

internal sealed class ZLinkSpotPubSubCapabilityBuilder(ZLinkSpotPubSubCapabilityRegistration registration)
    : ISpotPubSubCapabilityBuilder
{
    public void SetPubBind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("SPOT pub/sub bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public void SetRoutingId(RoutingId routingId)
    {
        registration.RoutingId = routingId;
    }

    public void ConfigurePublisherConfig(Action<IZLinkSpotPublisherConfig> configure)
    {
        configure(registration.PublisherConfig);
    }

    public void ConfigureSubscriberConfig(Action<IZLinkSpotSubscriberConfig> configure)
    {
        configure(registration.SubscriberConfig);
    }

    public void UseManualConnections(Action<IZLinkEndpointConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}

internal sealed class ZLinkSpotPublisherClientCapabilityBuilder(ZLinkSpotPublisherClientRegistration registration)
    : ISpotPublisherClientCapabilityBuilder
{
    public void ConfigureSocket(Action<IZLinkSocketConfig> configure)
    {
        configure(registration.SocketConfig);
    }

    public void UseManualConnections(Action<IZLinkEndpointConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}

internal sealed class ZLinkSpotChannelClientCapabilityBuilder(ZLinkSpotChannelClientRegistration registration)
    : ISpotChannelClientCapabilityBuilder
{
    public void ConfigureSocket(Action<IZLinkSocketConfig> configure)
    {
        configure(registration.SocketConfig);
    }

    public void ConfigureRouting(Action<IZLinkOutboundRouteConfig> configure)
    {
        configure(registration.RoutingConfig);
    }

    public void UseManualConnections(Action<IZLinkEndpointConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}
