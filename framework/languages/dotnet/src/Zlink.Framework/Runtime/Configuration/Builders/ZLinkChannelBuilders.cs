namespace Zlink.Framework.Runtime.Configuration.Builders;

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

    public void MapHandlerGroup(string groupName)
    {
        ZLinkHandlerGroupBuilderSupport.AddHandlerGroup(registration, groupName);
    }

    public void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>
    {
        registration.SendHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            typeof(TMessage),
            null,
            packetName));
    }

    public void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>
    {
        registration.RequestHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            typeof(TRequest),
            typeof(TReply),
            packetName));
    }

    public void EnableSpotRouteEgress(string targetSpotNodeChannelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(targetSpotNodeChannelName);
        registration.SpotRouteEgress = new ZLinkSpotRouteEgressRegistration(targetSpotNodeChannelName);
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

    public void MapHandlerGroup(string groupName)
    {
        ZLinkHandlerGroupBuilderSupport.AddHandlerGroup(registration, groupName);
    }

    public void AddPublishHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkPublishHandler<TMessage>
    {
        registration.PublishHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            typeof(TMessage),
            null,
            packetName));
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

internal static class ZLinkHandlerGroupBuilderSupport
{
    public static void AddHandlerGroup(
        ZLinkChannelRegistration registration,
        string groupName)
    {
        if (string.IsNullOrWhiteSpace(groupName))
        {
            throw new ZLinkConfigurationException("Handler group name must not be empty.");
        }

        registration.HandlerGroups.Add(groupName);
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

    public void ConfigureSocket(Action<IZLinkSocketConfig> configure)
    {
        configure(registration.SocketConfig);
    }

    public void ConfigureRouting(Action<IZLinkRouteConfig> configure)
    {
        configure(registration.RoutingConfig);
    }
}

internal sealed class ZLinkChannelClientCapabilityBuilder(ZLinkChannelClientCapabilityRegistration registration)
    : IChannelClientCapabilityBuilder
{
    public void ConfigureSocket(Action<IZLinkSocketConfig> configure)
    {
        configure(registration.SocketConfig);
    }

    public void ConfigureRouting(Action<IZLinkOutboundRouteConfig> configure)
    {
        configure(registration.RoutingConfig);
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

    public void ConfigureSocket(Action<IZLinkSocketConfig> configure)
    {
        configure(registration.SocketConfig);
    }

    public void ConfigureRouting(Action<IZLinkOutboundRouteConfig> configure)
    {
        configure(registration.RoutingConfig);
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

    public void ConfigureSocket(Action<IZLinkSocketConfig> configure)
    {
        configure(registration.SocketConfig);
    }
}

internal sealed class ZLinkChannelSubscriberCapabilityBuilder(ZLinkChannelSubscriberCapabilityRegistration registration)
    : IChannelSubscriberCapabilityBuilder
{
    public void ConfigureSocket(Action<IZLinkSocketConfig> configure)
    {
        configure(registration.SocketConfig);
    }

    public void UseManualConnections(Action<IChannelSubscriberConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}
