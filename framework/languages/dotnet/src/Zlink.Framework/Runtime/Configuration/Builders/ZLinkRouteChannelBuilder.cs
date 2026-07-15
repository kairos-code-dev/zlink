namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkRouteChannelBuilder(ZLinkRouteChannelRegistration registration)
    : IZLinkRouteMeshChannelBuilder
{
    public IZLinkEndpointConnections ClientConnections => registration.ManualConnections;

    public IZLinkRouteMeshChannelBuilder EnableServer(string endpoint)
    {
        registration.BindEndpoint = ZLinkChannelEndpointBuilderSupport.Validate(
            endpoint,
            "Route mesh channel server bind endpoint must not be empty.");
        return this;
    }

    public IZLinkRouteMeshChannelBuilder EnableClient()
    {
        registration.ClientEnabled = true;
        return this;
    }

    public IZLinkRouteMeshChannelBuilder EnableClient(string endpoint)
    {
        registration.ClientEnabled = true;
        ZLinkChannelEndpointBuilderSupport.AddManualConnection(
            registration.ManualConnections,
            endpoint,
            "Route mesh channel client endpoint must not be empty.");
        return this;
    }

    public IZLinkSocketConfig ConfigureSocket()
    {
        return registration.SocketConfig;
    }

    public IZLinkRouteMeshChannelBuilder SetRoutingId(RoutingId routingId)
    {
        registration.RoutingId = routingId;
        registration.HasExplicitRoutingId = true;
        return this;
    }

    public IZLinkRouteMeshChannelBuilder UseAllocatedRoutingId(int slotCount) =>
        UseAllocatedRoutingId(slotCount, registration.RouterChannelId);

    public IZLinkRouteMeshChannelBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix)
    {
        registration.RoutingIdAllocation = ZLinkRoutingIdAllocationBuilderSupport.Create(
            slotCount,
            routingIdPrefix,
            registration.RoutingIdAllocation?.GroupName);
        return this;
    }

    public IZLinkRouteMeshChannelBuilder SetRoutingIdAllocationGroup(string groupName)
    {
        registration.RoutingIdAllocation = ZLinkRoutingIdAllocationBuilderSupport.WithGroup(
            groupName,
            registration.RoutingIdAllocation,
            registration.RouterChannelId);
        return this;
    }

    public IZLinkRouteMeshChannelBuilder SetDefaultRequestTimeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        registration.DefaultRequestTimeout = timeout;
        return this;
    }

    public IZLinkRouteMeshChannelBuilder AddHandlerGroup(string groupName)
    {
        if (string.IsNullOrWhiteSpace(groupName))
            throw new ZLinkConfigurationException("Handler group name must not be empty.");

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
}
