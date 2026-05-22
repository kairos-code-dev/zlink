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

    public void AddActorRemoteAddressResolver<TResolver>()
        where TResolver : class, IZLinkActorRemoteAddressResolver
    {
        EnsureActorRemoteAddressResolverAvailable();
        _registration.ActorRemoteAddressResolverType = typeof(TResolver);
    }

    public void AddSpotRemoteAddressResolver<TResolver>()
        where TResolver : class, IZLinkSpotRemoteAddressResolver
    {
        EnsureSpotRemoteAddressResolverAvailable();
        _registration.SpotRemoteAddressResolverType = typeof(TResolver);
    }

    public void UseRegistryActorRemoteAddresses(string namespaceName)
    {
        UseRegistryActorRemoteAddresses(namespaceName, static _ => { });
    }

    public void UseRegistryActorRemoteAddresses(
        string namespaceName,
        Action<IZLinkRegistryActorRemoteAddressesOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(configure);
        EnsureActorRemoteAddressResolverAvailable();

        var options = new ZLinkRegistryActorRemoteAddressesOptions();
        configure(options);
        _registration.RegistryActorRemoteAddresses = new ZLinkRegistryActorRemoteAddressesRegistration
        {
            Namespace = ValidateRegistryNamespace(namespaceName),
            RouterChannelId = NormalizeOptionalName(options.RouterChannelId, nameof(options.RouterChannelId)),
        };
    }

    public void UseRegistrySpotRemoteAddresses(string namespaceName)
    {
        UseRegistrySpotRemoteAddresses(namespaceName, static _ => { });
    }

    public void UseRegistrySpotRemoteAddresses(
        string namespaceName,
        Action<IZLinkRegistrySpotRemoteAddressesOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(configure);
        EnsureSpotRemoteAddressResolverAvailable();

        var options = new ZLinkRegistrySpotRemoteAddressesOptions();
        configure(options);
        _registration.RegistrySpotRemoteAddresses = new ZLinkRegistrySpotRemoteAddressesRegistration
        {
            Namespace = ValidateRegistryNamespace(namespaceName),
            RouterChannelId = NormalizeOptionalName(options.RouterChannelId, nameof(options.RouterChannelId)),
        };
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

    private void EnsureActorRemoteAddressResolverAvailable()
    {
        if (_registration.ActorRemoteAddressResolverType is not null
            || _registration.RegistryActorRemoteAddresses is not null)
        {
            throw new ZLinkConfigurationException("Actor remote address resolver is already configured.");
        }
    }

    private void EnsureSpotRemoteAddressResolverAvailable()
    {
        if (_registration.SpotRemoteAddressResolverType is not null
            || _registration.RegistrySpotRemoteAddresses is not null)
        {
            throw new ZLinkConfigurationException("SPOT remote address resolver is already registered.");
        }
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
        var spotNode = ZLinkRegistrationBuilderGuard.AddSpotNode(
            _registration.SpotNodes,
            spotNodeName);

        configure(new ZLinkSpotNodeBuilder(spotNode));
    }
}

internal sealed class ZLinkRegistryActorRemoteAddressesOptions : IZLinkRegistryActorRemoteAddressesOptions
{
    public string? RouterChannelId { get; set; }
}

internal sealed class ZLinkRegistrySpotRemoteAddressesOptions : IZLinkRegistrySpotRemoteAddressesOptions
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
        var spotNode = ZLinkRegistrationBuilderGuard.AddSpotNode(
            registration.SpotNodes,
            spotNodeName);

        configure(new ZLinkSpotNodeBuilder(spotNode));
    }
}

internal static class ZLinkRegistrationBuilderGuard
{
    public static ZLinkSpotNodeRegistration AddSpotNode(
        Dictionary<string, ZLinkSpotNodeRegistration> registrations,
        string spotNodeName)
    {
        return AddUnique(
            registrations,
            spotNodeName,
            () => new ZLinkSpotNodeRegistration { SpotNodeName = spotNodeName },
            "SPOT node name must not be empty.",
            $"Duplicate spot node name '{spotNodeName}'.");
    }

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
