namespace Zlink.Framework;

internal sealed class ZLinkFrameworkOptionsBuilder : IZLinkFrameworkOptions
{
    private readonly ZLinkFrameworkRegistration _registration;

    public ZLinkFrameworkOptionsBuilder(ZLinkFrameworkRegistration registration)
    {
        _registration = registration;
    }

    public TimeSpan DefaultTimeout
    {
        get => _registration.DefaultTimeout;
        set => _registration.DefaultTimeout = value;
    }

    public IZLinkCodecRegistryBuilder Codecs => _registration.Codecs;

    public void AddActorFactory<TFactory>(string actorType)
        where TFactory : class
    {
        if (string.IsNullOrWhiteSpace(actorType))
        {
            throw new ZLinkConfigurationException("Actor factory name must not be empty.");
        }

        if (!_registration.ActorFactories.TryAdd(actorType, typeof(TFactory)))
        {
            throw new ZLinkConfigurationException($"Duplicate actor factory '{actorType}'.");
        }
    }

    public void AddChannel(
        string channelName,
        Action<IZLinkChannelBuilder> configure)
    {
        if (string.IsNullOrWhiteSpace(channelName))
        {
            throw new ZLinkConfigurationException("Channel name must not be empty.");
        }

        if (!_registration.Channels.TryAdd(
                channelName,
                new ZLinkChannelRegistration { ChannelName = channelName }))
        {
            throw new ZLinkConfigurationException($"Duplicate channel name '{channelName}'.");
        }

        configure(new ZLinkChannelBuilder(_registration.Channels[channelName]));
    }

    public void UseDiscovery(Action<IZLinkDiscoveryBuilder> configure)
    {
        if (_registration.Discovery is not null)
        {
            throw new ZLinkConfigurationException("Discovery is already configured.");
        }

        var discovery = new ZLinkDiscoveryRegistration();
        configure(new ZLinkDiscoveryBuilder(discovery.Endpoints));
        _registration.Discovery = discovery;
    }

    public void UseSpotDiscovery(
        string channelName,
        Action<IZLinkDiscoveryBuilder> configure)
    {
        if (string.IsNullOrWhiteSpace(channelName))
        {
            throw new ZLinkConfigurationException("SPOT discovery channel name must not be empty.");
        }

        if (_registration.SpotDiscovery is not null)
        {
            throw new ZLinkConfigurationException("SPOT discovery is already configured.");
        }

        var discovery = new ZLinkSpotDiscoveryRegistration
        {
            ChannelName = channelName,
        };

        configure(new ZLinkDiscoveryBuilder(discovery.Endpoints));
        _registration.SpotDiscovery = discovery;
    }

    public void UseFilter<TFilter>()
        where TFilter : class, IZLinkHandlerFilter
    {
        _registration.Filters.Add(typeof(TFilter));
    }

    public void ConfigureDispatch(Action<IZLinkDispatchOptions> configure)
    {
        configure(_registration.DispatchOptions);
    }

    public void AddStreamNode(
        string streamNodeName,
        Action<IZLinkStreamNodeBuilder> configure)
    {
        if (string.IsNullOrWhiteSpace(streamNodeName))
        {
            throw new ZLinkConfigurationException("STREAM node name must not be empty.");
        }

        if (!_registration.StreamNodes.TryAdd(
                streamNodeName,
                new ZLinkStreamNodeRegistration { StreamNodeName = streamNodeName }))
        {
            throw new ZLinkConfigurationException($"Duplicate stream node name '{streamNodeName}'.");
        }

        configure(new ZLinkStreamNodeBuilder(_registration.StreamNodes[streamNodeName]));
    }

    public void AddSpotNode(
        string spotNodeName,
        Action<IZLinkSpotNodeBuilder> configure)
    {
        if (string.IsNullOrWhiteSpace(spotNodeName))
        {
            throw new ZLinkConfigurationException("SPOT node name must not be empty.");
        }

        if (!_registration.SpotNodes.TryAdd(
                spotNodeName,
                new ZLinkSpotNodeRegistration { SpotNodeName = spotNodeName }))
        {
            throw new ZLinkConfigurationException($"Duplicate spot node name '{spotNodeName}'.");
        }

        configure(new ZLinkSpotNodeBuilder(_registration.SpotNodes[spotNodeName]));
    }
}

internal sealed class ZLinkDiscoveryBuilder(List<string> endpoints) : IZLinkDiscoveryBuilder
{
    public void Add(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Discovery endpoint must not be empty.");
        }

        endpoints.Add(endpoint);
    }
}

internal sealed class ZLinkChannelBuilder(ZLinkChannelRegistration registration) : IZLinkChannelBuilder
{
    public void EnableServer(Action<IChannelServerCapabilityBuilder>? configure = null)
    {
        registration.Server ??= new ZLinkChannelServerCapabilityRegistration();
        configure?.Invoke(new ZLinkChannelServerCapabilityBuilder(registration.Server));
    }

    public void EnableClient(Action<IChannelClientCapabilityBuilder>? configure = null)
    {
        registration.Client ??= new ZLinkChannelClientCapabilityRegistration();
        configure?.Invoke(new ZLinkChannelClientCapabilityBuilder(registration.Client));
    }

    public void EnablePublisher(Action<IChannelPublisherCapabilityBuilder>? configure = null)
    {
        registration.Publisher ??= new ZLinkChannelPublisherCapabilityRegistration();
        configure?.Invoke(new ZLinkChannelPublisherCapabilityBuilder(registration.Publisher));
    }

    public void EnableSubscriber(Action<IChannelSubscriberCapabilityBuilder>? configure = null)
    {
        registration.Subscriber ??= new ZLinkChannelSubscriberCapabilityRegistration();
        configure?.Invoke(new ZLinkChannelSubscriberCapabilityBuilder(registration.Subscriber));
    }
}

internal sealed class ZLinkChannelServerCapabilityBuilder(ZLinkChannelServerCapabilityRegistration registration)
    : IChannelServerCapabilityBuilder
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Channel server bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure)
    {
        configure(registration.SocketOptions);
    }

    public void ConfigureRouting(Action<IRoutedPeerOptions> configure)
    {
        configure(registration.RoutingOptions);
    }
}

internal sealed class ZLinkChannelClientCapabilityBuilder(ZLinkChannelClientCapabilityRegistration registration)
    : IChannelClientCapabilityBuilder
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

internal sealed class ZLinkChannelPublisherCapabilityBuilder(ZLinkChannelPublisherCapabilityRegistration registration)
    : IChannelPublisherCapabilityBuilder
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Channel publisher bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure)
    {
        configure(registration.SocketOptions);
    }
}

internal sealed class ZLinkChannelSubscriberCapabilityBuilder(ZLinkChannelSubscriberCapabilityRegistration registration)
    : IChannelSubscriberCapabilityBuilder
{
    public void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure)
    {
        configure(registration.SocketOptions);
    }

    public void UseManualConnections(Action<IChannelSubscriberConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}

internal sealed class ZLinkStreamNodeBuilder(ZLinkStreamNodeRegistration registration) : IZLinkStreamNodeBuilder
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("STREAM bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public void AddPacketSession<TSession>()
        where TSession : class, IZLinkPacketStreamSession
    {
        if (registration.PacketSessionType is not null)
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{registration.StreamNodeName}' already has a packet session.");
        }

        registration.PacketSessionType = typeof(TSession);
    }

    public void AddRawSession<TSession>()
        where TSession : class, IZLinkRawStreamSession
    {
        if (registration.RawSessionType is not null)
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{registration.StreamNodeName}' already has a raw session.");
        }

        registration.RawSessionType = typeof(TSession);
    }
}

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

internal sealed class ZLinkMutableConnections(List<string> endpoints)
    : IChannelClientConnections,
      IChannelSubscriberConnections,
      ISpotRouterConnections,
      ISpotPubSubConnections,
      ISpotPublisherConnections
{
    public void Connect(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Connection endpoint must not be empty.");
        }

        endpoints.Add(endpoint);
    }

    public void Disconnect(string endpoint)
    {
        endpoints.Remove(endpoint);
    }

    public IReadOnlyList<string> ListConnections()
    {
        return endpoints.AsReadOnly();
    }
}
