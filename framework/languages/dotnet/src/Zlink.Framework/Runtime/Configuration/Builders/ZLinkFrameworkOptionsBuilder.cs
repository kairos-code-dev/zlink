using System.Reflection;

namespace Zlink.Framework.Runtime.Configuration.Builders;

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

    public void AddHandlersFromAssemblyOf<TMarker>()
    {
        AddHandlersFromAssembly(typeof(TMarker).Assembly);
    }

    public void AddHandlersFromAssemblyOf(Type markerType)
    {
        ArgumentNullException.ThrowIfNull(markerType);

        AddHandlersFromAssembly(markerType.Assembly);
    }

    public void AddHandlersFromAssembly(Assembly assembly)
    {
        ArgumentNullException.ThrowIfNull(assembly);

        _registration.HandlerAssemblies.Add(assembly);
    }

    public void ConfigureMetadata(Action<IZLinkMetadataPolicyBuilder> configure)
    {
        ArgumentNullException.ThrowIfNull(configure);
        configure(new ZLinkMetadataPolicyBuilder(_registration.MetadataPolicy));
    }

    public void AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory
    {
        ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.ActorFactories,
            actorType,
            typeof(TFactory),
            "Actor factory name must not be empty.",
            $"Duplicate actor factory '{actorType}'.");
    }

    public void AddActorPlayRouteResolver<TResolver>()
        where TResolver : class, IZLinkActorPlayRouteResolver
    {
        if (_registration.ActorPlayRouteResolverType is not null
            || _registration.RegistryActorRoutes is not null)
        {
            throw new ZLinkConfigurationException("Actor play route resolver is already configured.");
        }

        _registration.ActorPlayRouteResolverType = typeof(TResolver);
    }

    public void AddSpotRouteResolver<TResolver>()
        where TResolver : class, IZLinkSpotRouteResolver
    {
        if (_registration.SpotRouteResolverType is not null
            || _registration.RegistrySpotRoutes is not null)
        {
            throw new ZLinkConfigurationException("SPOT route resolver is already registered.");
        }

        _registration.SpotRouteResolverType = typeof(TResolver);
    }

    public void AddActorSessionBindingStore<TStore>()
        where TStore : class, IZLinkActorSessionBindingStore
    {
        if (_registration.ActorSessionBindingStoreType is not null
            || _registration.RegistryActorSessionBindings is not null)
        {
            throw new ZLinkConfigurationException("Actor session binding store is already configured.");
        }

        _registration.ActorSessionBindingStoreType = typeof(TStore);
    }

    public void UseRegistryActorRoutes(string namespaceName)
    {
        UseRegistryActorRoutes(namespaceName, static _ => { });
    }

    public void UseRegistryActorRoutes(
        string namespaceName,
        Action<IZLinkRegistryActorRoutesOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(configure);
        if (_registration.ActorPlayRouteResolverType is not null
            || _registration.RegistryActorRoutes is not null)
        {
            throw new ZLinkConfigurationException("Actor play route resolver is already configured.");
        }

        var options = new ZLinkRegistryActorRoutesOptions();
        configure(options);
        _registration.RegistryActorRoutes = new ZLinkRegistryActorRoutesRegistration
        {
            Namespace = ValidateRegistryNamespace(namespaceName),
            RouterChannelId = NormalizeOptionalName(options.RouterChannelId, nameof(options.RouterChannelId)),
        };
    }

    public void UseRegistryActorSessionBindings(string namespaceName)
    {
        if (_registration.ActorSessionBindingStoreType is not null
            || _registration.RegistryActorSessionBindings is not null)
        {
            throw new ZLinkConfigurationException("Actor session binding store is already configured.");
        }

        _registration.RegistryActorSessionBindings = new ZLinkRegistryActorSessionBindingsRegistration
        {
            Namespace = ValidateRegistryNamespace(namespaceName),
        };
    }

    public void UseRegistrySpotRoutes(string namespaceName)
    {
        UseRegistrySpotRoutes(namespaceName, static _ => { });
    }

    public void UseRegistrySpotRoutes(
        string namespaceName,
        Action<IZLinkRegistrySpotRoutesOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(configure);
        if (_registration.SpotRouteResolverType is not null
            || _registration.RegistrySpotRoutes is not null)
        {
            throw new ZLinkConfigurationException("SPOT route resolver is already registered.");
        }

        var options = new ZLinkRegistrySpotRoutesOptions();
        configure(options);
        _registration.RegistrySpotRoutes = new ZLinkRegistrySpotRoutesRegistration
        {
            Namespace = ValidateRegistryNamespace(namespaceName),
            RouterChannelId = NormalizeOptionalName(options.RouterChannelId, nameof(options.RouterChannelId)),
        };
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
        var routeChannel = ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.RouteChannels,
            channelName,
            () => new ZLinkRouteChannelRegistration { RouterChannelId = channelName },
            "Route mesh channel name must not be empty.",
            $"Duplicate route mesh channel name '{channelName}'.");

        configure(new ZLinkRouteChannelBuilder(routeChannel));
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
        var streamNode = ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.StreamNodes,
            streamNodeName,
            () => new ZLinkStreamNodeRegistration { StreamNodeName = streamNodeName },
            "STREAM node name must not be empty.",
            $"Duplicate stream node name '{streamNodeName}'.");

        configure(new ZLinkStreamNodeBuilder(streamNode));
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
        return ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.Channels,
            channelName,
            () => new ZLinkChannelRegistration
            {
                ChannelName = channelName,
                AutoConnectType = autoConnectType,
            },
            "Channel name must not be empty.",
            $"Duplicate channel name '{channelName}'.");
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

    private static string ValidateRegistryNamespace(string namespaceName)
    {
        if (string.IsNullOrWhiteSpace(namespaceName)
            || !string.Equals(namespaceName, namespaceName.Trim(), StringComparison.Ordinal))
        {
            throw new ZLinkConfigurationException("Registry route namespace must not be empty or padded.");
        }

        return namespaceName;
    }

    private static string? NormalizeOptionalName(string? value, string name)
    {
        if (value is null)
        {
            return null;
        }

        if (string.IsNullOrWhiteSpace(value)
            || !string.Equals(value, value.Trim(), StringComparison.Ordinal))
        {
            throw new ZLinkConfigurationException($"{name} must not be empty or padded.");
        }

        return value;
    }

    private void AddSpotNodeRegistration(
        string spotNodeName,
        Action<ZLinkSpotNodeBuilder> configure)
    {
        var spotNode = ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.SpotNodes,
            spotNodeName,
            () => new ZLinkSpotNodeRegistration { SpotNodeName = spotNodeName },
            "SPOT node name must not be empty.",
            $"Duplicate spot node name '{spotNodeName}'.");

        configure(new ZLinkSpotNodeBuilder(spotNode));
    }
}

internal sealed class ZLinkRegistryActorRoutesOptions : IZLinkRegistryActorRoutesOptions
{
    public string? RouterChannelId { get; set; }
}

internal sealed class ZLinkRegistrySpotRoutesOptions : IZLinkRegistrySpotRoutesOptions
{
    public string? RouterChannelId { get; set; }
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
        var spotNode = ZLinkRegistrationBuilderGuard.AddUnique(
            registration.SpotNodes,
            spotNodeName,
            () => new ZLinkSpotNodeRegistration { SpotNodeName = spotNodeName },
            "SPOT node name must not be empty.",
            $"Duplicate spot node name '{spotNodeName}'.");

        configure(new ZLinkSpotNodeBuilder(spotNode));
    }
}

internal static class ZLinkRegistrationBuilderGuard
{
    public static void AddUnique<TValue>(
        Dictionary<string, TValue> registrations,
        string name,
        TValue value,
        string emptyMessage,
        string duplicateMessage)
    {
        AddUnique(registrations, name, () => value, emptyMessage, duplicateMessage);
    }

    public static TValue AddUnique<TValue>(
        Dictionary<string, TValue> registrations,
        string name,
        Func<TValue> create,
        string emptyMessage,
        string duplicateMessage)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            throw new ZLinkConfigurationException(emptyMessage);
        }

        var value = create();
        if (!registrations.TryAdd(name, value))
        {
            throw new ZLinkConfigurationException(duplicateMessage);
        }

        return value;
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
