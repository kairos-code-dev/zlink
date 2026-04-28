namespace Zlink.Framework.Configuration.Builders;

internal sealed class ZLinkSpotNodeBuilder(ZLinkSpotNodeRegistration registration) : IZLinkSpotNodeBuilder
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("SPOT bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

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
            throw new ZLinkConfigurationException("Attached channel client name must not be empty.");
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

    public void AddSpotFactory<TSpot>(string spotName)
        where TSpot : ZLinkSpot
    {
        if (string.IsNullOrWhiteSpace(spotName))
        {
            throw new ZLinkConfigurationException("Spot factory name must not be empty.");
        }

        if (!registration.SpotFactories.TryAdd(spotName, typeof(TSpot)))
        {
            throw new ZLinkConfigurationException(
                $"Duplicate SPOT factory '{spotName}' on node '{registration.SpotNodeName}'.");
        }
    }
}

internal sealed class ZLinkSpotRouterCapabilityBuilder(ZLinkSpotRouterCapabilityRegistration registration)
    : ISpotRouterCapabilityBuilder
{
    public void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure)
    {
        configure(registration.SocketOptions);
    }

    public void ConfigureRouting(Action<IRoutedPeerOptions> configure)
    {
        configure(registration.RoutingOptions);
    }

    public void UseManualConnections(Action<ISpotRouterConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}

internal sealed class ZLinkSpotPubSubCapabilityBuilder(ZLinkSpotPubSubCapabilityRegistration registration)
    : ISpotPubSubCapabilityBuilder
{
    public void ConfigurePublisherOptions(Action<ISpotNodePublisherOptions> configure)
    {
        configure(registration.PublisherOptions);
    }

    public void ConfigureSubscriberOptions(Action<ISpotNodeSubscriberOptions> configure)
    {
        configure(registration.SubscriberOptions);
    }

    public void UseManualConnections(Action<ISpotPubSubConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}

internal sealed class ZLinkSpotPublisherClientCapabilityBuilder(ZLinkSpotPublisherClientRegistration registration)
    : ISpotPublisherClientCapabilityBuilder
{
    public void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure)
    {
        configure(registration.SocketOptions);
    }

    public void UseManualConnections(Action<ISpotPublisherConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}

internal sealed class ZLinkSpotChannelClientCapabilityBuilder(ZLinkSpotChannelClientRegistration registration)
    : ISpotChannelClientCapabilityBuilder
{
    public void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure)
    {
        configure(registration.SocketOptions);
    }

    public void ConfigureRouting(Action<IOutboundPeerOptions> configure)
    {
        configure(registration.RoutingOptions);
    }

    public void UseManualConnections(Action<IChannelClientConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}
