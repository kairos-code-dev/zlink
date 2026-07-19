namespace Zlink.Framework.Runtime.Configuration.Builders;


internal sealed class ZLinkFanoutChannelBuilder(ZLinkChannelRegistration registration)
    : IZLinkFanoutChannelBuilder
{
    public IZLinkFanoutChannelBuilder EnablePublisher(string endpoint)
    {
        registration.Publisher ??= new ZLinkChannelPublisherCapabilityRegistration();
        registration.Publisher.BindEndpoint = ZLinkChannelEndpointBuilderSupport.Validate(
            endpoint,
            "Channel publisher bind endpoint must not be empty.");
        return this;
    }

    public IZLinkFanoutChannelBuilder ConnectSubscriber(string endpoint)
    {
        registration.Subscriber ??= new ZLinkChannelSubscriberCapabilityRegistration();
        ZLinkChannelEndpointBuilderSupport.AddManualConnection(
            registration.Subscriber.ManualConnections,
            endpoint,
            "Channel subscriber endpoint must not be empty.");
        return this;
    }

    public IZLinkFanoutChannelBuilder AddHandler<THandler, TEvent>(string? packetName = null)
        where THandler : class, IZLinkFanoutHandler<TEvent>
    {
        ZLinkChannelHandlerRegistrationBuilder.AddFanoutHandler<THandler, TEvent>(
            registration,
            packetName);
        return this;
    }
}

internal static class ZLinkRoutingIdAllocationBuilderSupport
{
    public static ZLinkRoutingIdAllocationRegistration Create(
        int slotCount,
        string routingIdPrefix,
        string? groupName)
    {
        if (slotCount < 1)
            throw new ZLinkConfigurationException("Routing-id allocation slot count must be at least one.");
        if (string.IsNullOrWhiteSpace(routingIdPrefix))
            throw new ZLinkConfigurationException("Routing-id allocation prefix must not be empty.");

        return new ZLinkRoutingIdAllocationRegistration
        {
            SlotCount = slotCount,
            RoutingIdPrefix = routingIdPrefix,
            GroupName = groupName
        };
    }

    public static ZLinkRoutingIdAllocationRegistration WithGroup(
        string groupName,
        ZLinkRoutingIdAllocationRegistration? allocation,
        string defaultPrefix)
    {
        if (string.IsNullOrWhiteSpace(groupName))
            throw new ZLinkConfigurationException("Routing-id allocation group name must not be empty.");

        allocation ??= new ZLinkRoutingIdAllocationRegistration
        {
            SlotCount = 0,
            RoutingIdPrefix = defaultPrefix
        };
        allocation.GroupName = groupName;
        return allocation;
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

    public static void AddFanoutHandler<THandler, TEvent>(
        ZLinkChannelRegistration registration,
        string? packetName)
        where THandler : class, IZLinkFanoutHandler<TEvent>
    {
        registration.PublishHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            typeof(TEvent),
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
