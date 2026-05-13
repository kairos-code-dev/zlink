namespace Zlink.Framework.Configuration.Builders;

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

internal sealed class ZLinkClientServerChannelBuilder(ZLinkChannelRegistration registration)
    : IZLinkClientServerChannelBuilder
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
}

internal sealed class ZLinkFanoutChannelBuilder(ZLinkChannelRegistration registration)
    : IZLinkFanoutChannelBuilder
{
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

internal sealed class ZLinkDealerMeshChannelBuilder(ZLinkChannelRegistration registration)
    : IZLinkDealerMeshChannelBuilder
{
    public void EnableClient(Action<IDealerMeshChannelClientCapabilityBuilder>? configure = null)
    {
        registration.Client ??= new ZLinkChannelClientCapabilityRegistration();
        configure?.Invoke(new ZLinkDealerMeshChannelClientCapabilityBuilder(registration.Client));
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

    public void ConfigureRouting(Action<IZLinkRoutePolicyOptions> configure)
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

    public void ConfigureRouting(Action<IZLinkOutboundRoutePolicyOptions> configure)
    {
        configure(registration.RoutingOptions);
    }

    public void UseManualConnections(Action<IChannelClientConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}

internal sealed class ZLinkDealerMeshChannelClientCapabilityBuilder(ZLinkChannelClientCapabilityRegistration registration)
    : IDealerMeshChannelClientCapabilityBuilder
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Dealer mesh channel client bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure)
    {
        configure(registration.SocketOptions);
    }

    public void ConfigureRouting(Action<IZLinkOutboundRoutePolicyOptions> configure)
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
