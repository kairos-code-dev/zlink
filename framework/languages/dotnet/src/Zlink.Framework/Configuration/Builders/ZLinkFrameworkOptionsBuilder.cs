namespace Zlink.Framework.Configuration.Builders;

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

    public void ConfigureMetadata(Action<IZLinkMetadataPolicyBuilder> configure)
    {
        ArgumentNullException.ThrowIfNull(configure);
        configure(new ZLinkMetadataPolicyBuilder(_registration.MetadataPolicy));
    }

    public void AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory
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

    public void AddActorPlayRouteResolver<TResolver>()
        where TResolver : class, IZLinkActorPlayRouteResolver
    {
        if (_registration.ActorPlayRouteResolverType is not null)
        {
            throw new ZLinkConfigurationException("Actor play route resolver is already configured.");
        }

        _registration.ActorPlayRouteResolverType = typeof(TResolver);
    }

    public void AddSpotRouteResolver<TResolver>()
        where TResolver : class, IZLinkSpotRouteResolver
    {
        if (_registration.SpotRouteResolverType is not null)
        {
            throw new ZLinkConfigurationException("SPOT route resolver is already registered.");
        }

        _registration.SpotRouteResolverType = typeof(TResolver);
    }

    public void AddActorSessionBindingStore<TStore>()
        where TStore : class, IZLinkActorSessionBindingStore
    {
        if (_registration.ActorSessionBindingStoreType is not null)
        {
            throw new ZLinkConfigurationException("Actor session binding store is already configured.");
        }

        _registration.ActorSessionBindingStoreType = typeof(TStore);
    }

    public void AddChannel(
        string channelName,
        Action<IZLinkChannelBuilder> configure)
    {
        var channel = AddChannelRegistration(channelName, ZLinkAutoConnectType.Invalid);
        configure(new ZLinkChannelBuilder(channel));
        channel.AutoConnectType = InferCompatibilityAutoConnectType(channel);
    }

    public void AddClientServerChannel(
        string channelName,
        Action<IZLinkClientServerChannelBuilder> configure)
    {
        var channel = AddChannelRegistration(channelName, ZLinkAutoConnectType.ClientServer);
        configure(new ZLinkClientServerChannelBuilder(channel));
    }

    public void AddFanoutChannel(
        string channelName,
        Action<IZLinkFanoutChannelBuilder> configure)
    {
        var channel = AddChannelRegistration(channelName, ZLinkAutoConnectType.Fanout);
        configure(new ZLinkFanoutChannelBuilder(channel));
    }

    public void AddDealerMeshChannel(
        string channelName,
        Action<IZLinkDealerMeshChannelBuilder> configure)
    {
        var channel = AddChannelRegistration(channelName, ZLinkAutoConnectType.DealerMesh);
        configure(new ZLinkDealerMeshChannelBuilder(channel));
    }

    public void AddRouteChannel(
        string routerChannelId,
        Action<IZLinkRouteChannelBuilder> configure)
    {
        AddRouteMeshChannel(routerChannelId, configure);
    }

    public void AddRouteMeshChannel(
        string channelName,
        Action<IZLinkRouteMeshChannelBuilder> configure)
    {
        if (string.IsNullOrWhiteSpace(channelName))
        {
            throw new ZLinkConfigurationException("Route mesh channel name must not be empty.");
        }

        if (!_registration.RouteChannels.TryAdd(
                channelName,
                new ZLinkRouteChannelRegistration { RouterChannelId = channelName }))
        {
            throw new ZLinkConfigurationException($"Duplicate route mesh channel name '{channelName}'.");
        }

        configure(new ZLinkRouteChannelBuilder(_registration.RouteChannels[channelName]));
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
            UseDiscoveryCalled = true,
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
        AddSpotNodeRegistration(spotNodeName, node => configure(node));
    }

    public void AddSpotMesh(
        string channelName,
        Action<IZLinkSpotMeshBuilder> configure)
    {
        if (string.IsNullOrWhiteSpace(channelName))
        {
            throw new ZLinkConfigurationException("SPOT mesh channel name must not be empty.");
        }

        if (_registration.SpotDiscovery is not null)
        {
            throw new ZLinkConfigurationException("SPOT mesh is already configured.");
        }

        var discovery = new ZLinkSpotDiscoveryRegistration
        {
            ChannelName = channelName,
            RequiresUseDiscovery = true,
        };

        _registration.SpotDiscovery = discovery;
        configure(new ZLinkSpotMeshBuilder(_registration, discovery));
    }

    private ZLinkChannelRegistration AddChannelRegistration(
        string channelName,
        ZLinkAutoConnectType autoConnectType)
    {
        if (string.IsNullOrWhiteSpace(channelName))
        {
            throw new ZLinkConfigurationException("Channel name must not be empty.");
        }

        if (!_registration.Channels.TryAdd(
                channelName,
                new ZLinkChannelRegistration
                {
                    ChannelName = channelName,
                    AutoConnectType = autoConnectType,
                }))
        {
            throw new ZLinkConfigurationException($"Duplicate channel name '{channelName}'.");
        }

        return _registration.Channels[channelName];
    }

    private static ZLinkAutoConnectType InferCompatibilityAutoConnectType(
        ZLinkChannelRegistration channel)
    {
        var hasClientServerCapabilities = channel.Server is not null || channel.Client is not null;
        var hasFanoutCapabilities = channel.Publisher is not null || channel.Subscriber is not null;

        if (hasClientServerCapabilities && hasFanoutCapabilities)
        {
            throw new ZLinkConfigurationException(
                $"Channel '{channel.ChannelName}' mixes client/server and fanout capabilities. Use AddClientServerChannel(...) and AddFanoutChannel(...) with separate channel names.");
        }

        if (hasClientServerCapabilities)
        {
            return ZLinkAutoConnectType.ClientServer;
        }

        if (hasFanoutCapabilities)
        {
            return ZLinkAutoConnectType.Fanout;
        }

        throw new ZLinkConfigurationException(
            $"Channel '{channel.ChannelName}' must enable at least one capability.");
    }

    private void AddSpotNodeRegistration(
        string spotNodeName,
        Action<ZLinkSpotNodeBuilder> configure)
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

internal sealed class ZLinkSpotMeshBuilder(
    ZLinkFrameworkRegistration registration,
    ZLinkSpotDiscoveryRegistration discovery)
    : IZLinkSpotMeshBuilder
{
    public void UseDiscovery(Action<IZLinkDiscoveryBuilder> configure)
    {
        discovery.UseDiscoveryCalled = true;
        configure(new ZLinkDiscoveryBuilder(discovery.Endpoints));
    }

    public void AddNode(
        string spotNodeName,
        Action<IZLinkSpotMeshNodeBuilder> configure)
    {
        if (string.IsNullOrWhiteSpace(spotNodeName))
        {
            throw new ZLinkConfigurationException("SPOT node name must not be empty.");
        }

        if (!registration.SpotNodes.TryAdd(
                spotNodeName,
                new ZLinkSpotNodeRegistration { SpotNodeName = spotNodeName }))
        {
            throw new ZLinkConfigurationException($"Duplicate spot node name '{spotNodeName}'.");
        }

        configure(new ZLinkSpotNodeBuilder(registration.SpotNodes[spotNodeName]));
    }
}

internal sealed class ZLinkMetadataPolicyBuilder(ZLinkMetadataPolicyRegistration registration)
    : IZLinkMetadataPolicyBuilder
{
    public void ForwardApplicationKey(string key)
    {
        if (string.IsNullOrWhiteSpace(key))
        {
            throw new ZLinkConfigurationException("Metadata key must not be empty.");
        }

        registration.ForwardedApplicationKeys.Add(key);
    }
}
