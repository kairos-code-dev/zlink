namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkRouteChannelBuilder(ZLinkRouteChannelRegistration registration)
    : IZLinkRouteChannelBuilder, IZLinkRouteMeshChannelBuilder
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Route channel bind endpoint must not be empty.");
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

    public void UseManualConnections(Action<IRouteChannelConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }

    public void AddHandlerGroup(string groupName)
    {
        if (string.IsNullOrWhiteSpace(groupName))
        {
            throw new ZLinkConfigurationException("Handler group name must not be empty.");
        }

        registration.HandlerGroups.Add(groupName);
    }

    public void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>
    {
        registration.SendHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            typeof(TMessage),
            null,
            packetName));
    }

    public void AddSendHandler<THandler>(string? packetName = null)
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
    }

    public void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>
    {
        registration.RequestHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            typeof(TRequest),
            typeof(TReply),
            packetName));
    }

    public void AddRequestHandler<THandler>(string? packetName = null)
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
    }

    public void EnableSpotRouteEgress(string targetSpotNodeChannelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(targetSpotNodeChannelName);
        registration.SpotRouteEgress = new ZLinkSpotRouteEgressRegistration(targetSpotNodeChannelName);
    }
}
