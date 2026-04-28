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
