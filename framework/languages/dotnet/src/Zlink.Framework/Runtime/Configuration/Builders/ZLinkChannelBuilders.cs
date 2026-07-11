namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkClientServerChannelBuilder(ZLinkChannelRegistration registration)
    : IZLinkClientServerChannelBuilder
{
    public IZLinkEndpointConnections ClientConnections
        => (registration.Client ??= new ZLinkChannelClientCapabilityRegistration()).ManualConnections;

    public IZLinkClientServerChannelBuilder EnableServer(string endpoint)
    {
        registration.Server ??= new ZLinkChannelServerCapabilityRegistration();
        registration.Server.BindEndpoint = ZLinkChannelEndpointBuilderSupport.Validate(
            endpoint,
            "Channel server bind endpoint must not be empty.");
        return this;
    }

    public IZLinkClientServerChannelBuilder EnableClient()
    {
        registration.Client ??= new ZLinkChannelClientCapabilityRegistration();
        return this;
    }

    public IZLinkClientServerChannelBuilder EnableClient(string endpoint)
    {
        registration.Client ??= new ZLinkChannelClientCapabilityRegistration();
        ZLinkChannelEndpointBuilderSupport.AddManualConnection(
            registration.Client.ManualConnections,
            endpoint,
            "Channel client endpoint must not be empty.");
        return this;
    }

    public IZLinkClientServerChannelBuilder SetRoutingId(RoutingId routingId)
    {
        registration.RoutingId = routingId;
        return this;
    }

    public IZLinkSocketConfig ConfigureServerSocket()
    {
        registration.Server ??= new ZLinkChannelServerCapabilityRegistration();
        return registration.Server.SocketConfig;
    }

    public IZLinkRouteConfig ConfigureServerRouting()
    {
        registration.Server ??= new ZLinkChannelServerCapabilityRegistration();
        return registration.Server.RoutingConfig;
    }

    public IZLinkSocketConfig ConfigureClientSocket()
    {
        registration.Client ??= new ZLinkChannelClientCapabilityRegistration();
        return registration.Client.SocketConfig;
    }

    public IZLinkOutboundRouteConfig ConfigureClientRouting()
    {
        registration.Client ??= new ZLinkChannelClientCapabilityRegistration();
        return registration.Client.RoutingConfig;
    }

    public IZLinkClientServerChannelBuilder SetDefaultRequestTimeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        registration.DefaultRequestTimeout = timeout;
        return this;
    }

    public IZLinkClientServerChannelBuilder AddHandlerGroup(string groupName)
    {
        ZLinkHandlerGroupBuilderSupport.AddHandlerGroup(registration, groupName);
        return this;
    }

    public IZLinkClientServerChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>
    {
        ZLinkChannelHandlerRegistrationBuilder.AddSendHandler<THandler, TMessage>(
            registration,
            packetName);
        return this;
    }

    public IZLinkClientServerChannelBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        ZLinkChannelHandlerRegistrationBuilder.AddSendHandler<THandler>(
            registration,
            packetName);
        return this;
    }

    public IZLinkClientServerChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>
    {
        ZLinkChannelHandlerRegistrationBuilder.AddRequestHandler<THandler, TRequest, TReply>(
            registration,
            packetName);
        return this;
    }

    public IZLinkClientServerChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        ZLinkChannelHandlerRegistrationBuilder.AddRequestHandler<THandler>(
            registration,
            packetName);
        return this;
    }
}

internal sealed class ZLinkFanoutChannelBuilder(ZLinkChannelRegistration registration)
    : IZLinkFanoutChannelBuilder
{
    public IZLinkEndpointConnections SubscriberConnections
        => (registration.Subscriber ??= new ZLinkChannelSubscriberCapabilityRegistration()).ManualConnections;

    public IZLinkFanoutChannelBuilder EnablePublisher(string endpoint)
    {
        registration.Publisher ??= new ZLinkChannelPublisherCapabilityRegistration();
        registration.Publisher.BindEndpoint = ZLinkChannelEndpointBuilderSupport.Validate(
            endpoint,
            "Channel publisher bind endpoint must not be empty.");
        return this;
    }

    public IZLinkFanoutChannelBuilder EnableSubscriber()
    {
        registration.Subscriber ??= new ZLinkChannelSubscriberCapabilityRegistration();
        return this;
    }

    public IZLinkFanoutChannelBuilder EnableSubscriber(string endpoint)
    {
        registration.Subscriber ??= new ZLinkChannelSubscriberCapabilityRegistration();
        ZLinkChannelEndpointBuilderSupport.AddManualConnection(
            registration.Subscriber.ManualConnections,
            endpoint,
            "Channel subscriber endpoint must not be empty.");
        return this;
    }

    public IZLinkFanoutChannelBuilder SetRoutingId(RoutingId routingId)
    {
        registration.RoutingId = routingId;
        return this;
    }

    public IZLinkFanoutChannelBuilder AddHandlerGroup(string groupName)
    {
        ZLinkHandlerGroupBuilderSupport.AddHandlerGroup(registration, groupName);
        return this;
    }

    public IZLinkFanoutChannelBuilder AddPublishHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkPublishHandler<TMessage>
    {
        ZLinkChannelHandlerRegistrationBuilder.AddPublishHandler<THandler, TMessage>(
            registration,
            packetName);
        return this;
    }

    public IZLinkFanoutChannelBuilder AddPublishHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        ZLinkChannelHandlerRegistrationBuilder.AddPublishHandler<THandler>(
            registration,
            packetName);
        return this;
    }
}

internal static class ZLinkChannelEndpointBuilderSupport
{
    public static string Validate(string endpoint, string errorMessage)
    {
        if (string.IsNullOrWhiteSpace(endpoint)) throw new ZLinkConfigurationException(errorMessage);

        return endpoint;
    }

    public static void AddManualConnection(
        ZLinkEndpointConnections endpoints,
        string endpoint,
        string errorMessage)
    {
        endpoints.Connect(Validate(endpoint, errorMessage));
    }
}

internal static class ZLinkChannelHandlerRegistrationBuilder
{
    public static void AddSendHandler<THandler, TMessage>(
        ZLinkChannelRegistration registration,
        string? packetName)
        where THandler : class, IZLinkSendHandler<TMessage>
    {
        registration.SendHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            typeof(TMessage),
            null,
            packetName));
    }

    public static void AddSendHandler<THandler>(
        ZLinkChannelRegistration registration,
        string? packetName)
        where THandler : class
    {
        var args = ZLinkTypedHandlerBuilderSupport.ResolveSingleHandlerInterface(
                typeof(THandler),
                typeof(IZLinkSendHandler<>),
                "send")
            .GetGenericArguments();
        registration.SendHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            args[0],
            null,
            packetName));
    }

    public static void AddRequestHandler<THandler, TRequest, TReply>(
        ZLinkChannelRegistration registration,
        string? packetName)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>
    {
        registration.RequestHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            typeof(TRequest),
            typeof(TReply),
            packetName));
    }

    public static void AddRequestHandler<THandler>(
        ZLinkChannelRegistration registration,
        string? packetName)
        where THandler : class
    {
        var args = ZLinkTypedHandlerBuilderSupport.ResolveSingleHandlerInterface(
                typeof(THandler),
                typeof(IZLinkRequestHandler<,>),
                "request")
            .GetGenericArguments();
        registration.RequestHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            args[0],
            args[1],
            packetName));
    }

    public static void AddPublishHandler<THandler, TMessage>(
        ZLinkChannelRegistration registration,
        string? packetName)
        where THandler : class, IZLinkPublishHandler<TMessage>
    {
        registration.PublishHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            typeof(TMessage),
            null,
            packetName));
    }

    public static void AddPublishHandler<THandler>(
        ZLinkChannelRegistration registration,
        string? packetName)
        where THandler : class
    {
        var args = ZLinkTypedHandlerBuilderSupport.ResolveSingleHandlerInterface(
                typeof(THandler),
                typeof(IZLinkPublishHandler<>),
                "publish")
            .GetGenericArguments();
        registration.PublishHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            args[0],
            null,
            packetName));
    }
}

internal static class ZLinkHandlerGroupBuilderSupport
{
    public static void AddHandlerGroup(
        ZLinkChannelRegistration registration,
        string groupName)
    {
        if (string.IsNullOrWhiteSpace(groupName))
            throw new ZLinkConfigurationException("Handler group name must not be empty.");

        registration.HandlerGroups.Add(groupName);
    }
}

internal static class ZLinkTypedHandlerBuilderSupport
{
    public static Type ResolveSingleHandlerInterface(
        Type handlerType,
        Type handlerInterfaceDefinition,
        string handlerKind)
    {
        var matches = handlerType
            .GetInterfaces()
            .Where(handlerInterface => handlerInterface.IsGenericType
                                       && handlerInterface.GetGenericTypeDefinition() == handlerInterfaceDefinition)
            .ToArray();

        return matches.Length switch
        {
            1 => matches[0],
            0 => throw new ZLinkConfigurationException(
                $"Handler '{handlerType.FullName}' must implement {handlerKind} handler interface '{handlerInterfaceDefinition.Name}'."),
            _ => throw new ZLinkConfigurationException(
                $"Handler '{handlerType.FullName}' implements multiple {handlerKind} handler interfaces. Use the overload with explicit message types.")
        };
    }
}
