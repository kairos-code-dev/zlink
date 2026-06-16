namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkRouteChannelBuilder(ZLinkRouteChannelRegistration registration)
    : IZLinkRouteMeshChannelBuilder
{
    public IZLinkRouteMeshChannelBuilder EnableServer(string endpoint)
    {
        new ZLinkRouteMeshChannelServerCapabilityBuilder(registration).Bind(endpoint);
        return this;
    }

    public IZLinkRouteMeshChannelBuilder EnableClient()
    {
        return this;
    }

    public IZLinkRouteMeshChannelBuilder EnableClient(string endpoint)
    {
        ZLinkChannelEndpointBuilderSupport.AddManualConnection(
            registration.ManualConnections,
            endpoint,
            "Route mesh channel client endpoint must not be empty.");
        return this;
    }

    private void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Route channel bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public IZLinkSocketConfig ConfigureSocket()
    {
        return registration.SocketConfig;
    }

    public IZLinkRouteConfig ConfigureRouting()
    {
        return registration.RoutingConfig;
    }

    public IZLinkRouteMeshChannelBuilder AddHandlerGroup(string groupName)
    {
        if (string.IsNullOrWhiteSpace(groupName))
        {
            throw new ZLinkConfigurationException("Handler group name must not be empty.");
        }

        registration.HandlerGroups.Add(groupName);
        return this;
    }

    public IZLinkRouteMeshChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>
    {
        registration.SendHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            typeof(TMessage),
            null,
            packetName));
        return this;
    }

    public IZLinkRouteMeshChannelBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        var handlerInterface = ZLinkTypedHandlerBuilderSupport.ResolveSingleHandlerInterface(
            typeof(THandler),
            typeof(IZLinkRouteSendHandler<>),
            "route send");
        var args = handlerInterface.GetGenericArguments();
        registration.SendHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            args[0],
            null,
            packetName));
        return this;
    }

    public IZLinkRouteMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>
    {
        registration.RequestHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            typeof(TRequest),
            typeof(TReply),
            packetName));
        return this;
    }

    public IZLinkRouteMeshChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        var handlerInterface = ZLinkTypedHandlerBuilderSupport.ResolveSingleHandlerInterface(
            typeof(THandler),
            typeof(IZLinkRouteRequestHandler<,>),
            "route request");
        var args = handlerInterface.GetGenericArguments();
        registration.RequestHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            args[0],
            args[1],
            packetName));
        return this;
    }

    public IZLinkRouteMeshChannelBuilder EnableSpotRouteEgress(string targetSpotNodeChannelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(targetSpotNodeChannelName);
        registration.SpotRouteEgress = new ZLinkSpotRouteEgressRegistration(targetSpotNodeChannelName);
        return this;
    }
}

internal sealed class ZLinkRouteMeshChannelServerCapabilityBuilder(
    ZLinkRouteChannelRegistration registration)
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Route mesh channel server bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public IZLinkSocketConfig ConfigureSocket()
    {
        return registration.SocketConfig;
    }

    public IZLinkRouteConfig ConfigureRouting()
    {
        return registration.RoutingConfig;
    }
}

internal sealed class ZLinkRouteMeshOutboundRouteConfigAdapter(ZLinkRouteConfig routeConfig)
    : IZLinkOutboundRouteConfig
{
    public RoutingId RoutingId
    {
        get => routeConfig.RoutingId;
        set => routeConfig.RoutingId = value;
    }

    public bool ProbeRouterOnConnect
    {
        get => routeConfig.EnablePeerProbe;
        set => routeConfig.EnablePeerProbe = value;
    }
}
