namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkClientServerChannelBuilder(ZLinkChannelRegistration registration)
    : IZLinkClientServerChannelBuilder
{
    public IZLinkClientServerChannelBuilder EnableServer(string endpoint)
    {
        registration.Server ??= new ZLinkChannelServerCapabilityRegistration();
        new ZLinkChannelServerCapabilityBuilder(registration.Server).Bind(endpoint);
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

    public IZLinkClientServerChannelBuilder EnableSpotRouteEgress(string targetSpotNodeChannelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(targetSpotNodeChannelName);
        registration.SpotRouteEgress = new ZLinkSpotRouteEgressRegistration(targetSpotNodeChannelName);
        return this;
    }
}

internal sealed class ZLinkFanoutChannelBuilder(ZLinkChannelRegistration registration)
    : IZLinkFanoutChannelBuilder
{
    public IZLinkFanoutChannelBuilder EnablePublisher(string endpoint)
    {
        registration.Publisher ??= new ZLinkChannelPublisherCapabilityRegistration();
        new ZLinkChannelPublisherCapabilityBuilder(registration.Publisher).Bind(endpoint);
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

internal sealed class ZLinkDealerMeshChannelBuilder(ZLinkChannelRegistration registration)
    : IZLinkDealerMeshChannelBuilder
{
    public IZLinkDealerMeshChannelBuilder EnableServer(string endpoint)
    {
        // dealer mesh 의 server·client 는 같은 DEALER 소켓을 공유한다. server(제공) 설정도
        // client capability registration(= 그 DEALER)에 기록한다. ROUTER 를 만드는
        // registration.Server 는 쓰지 않는다.
        registration.Client ??= new ZLinkChannelClientCapabilityRegistration();
        new ZLinkDealerMeshChannelServerCapabilityBuilder(registration.Client).Bind(endpoint);
        return this;
    }

    public IZLinkDealerMeshChannelBuilder EnableClient()
    {
        registration.Client ??= new ZLinkChannelClientCapabilityRegistration();
        return this;
    }

    public IZLinkDealerMeshChannelBuilder EnableClient(string endpoint)
    {
        registration.Client ??= new ZLinkChannelClientCapabilityRegistration();
        ZLinkChannelEndpointBuilderSupport.AddManualConnection(
            registration.Client.ManualConnections,
            endpoint,
            "Dealer mesh channel client endpoint must not be empty.");
        return this;
    }

    public IZLinkDealerMeshChannelBuilder AddHandlerGroup(string groupName)
    {
        ZLinkHandlerGroupBuilderSupport.AddHandlerGroup(registration, groupName);
        return this;
    }

    public IZLinkDealerMeshChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>
    {
        ZLinkChannelHandlerRegistrationBuilder.AddSendHandler<THandler, TMessage>(
            registration,
            packetName);
        return this;
    }

    public IZLinkDealerMeshChannelBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        ZLinkChannelHandlerRegistrationBuilder.AddSendHandler<THandler>(
            registration,
            packetName);
        return this;
    }

    public IZLinkDealerMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>
    {
        ZLinkChannelHandlerRegistrationBuilder.AddRequestHandler<THandler, TRequest, TReply>(
            registration,
            packetName);
        return this;
    }

    public IZLinkDealerMeshChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        ZLinkChannelHandlerRegistrationBuilder.AddRequestHandler<THandler>(
            registration,
            packetName);
        return this;
    }
}

internal static class ZLinkChannelEndpointBuilderSupport
{
    public static void AddManualConnection(
        ICollection<string> endpoints,
        string endpoint,
        string errorMessage)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException(errorMessage);
        }

        endpoints.Add(endpoint);
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
        {
            throw new ZLinkConfigurationException("Handler group name must not be empty.");
        }

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
                $"Handler '{handlerType.FullName}' implements multiple {handlerKind} handler interfaces. Use the overload with explicit message types."),
        };
    }
}

internal sealed class ZLinkChannelServerCapabilityBuilder(ZLinkChannelServerCapabilityRegistration registration)
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Channel server bind endpoint must not be empty.");
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

internal sealed class ZLinkDealerMeshChannelServerCapabilityBuilder(ZLinkChannelClientCapabilityRegistration registration)
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Dealer mesh channel server bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public IZLinkSocketConfig ConfigureSocket()
    {
        return registration.SocketConfig;
    }

    public IZLinkRouteConfig ConfigureRouting()
    {
        // dealer mesh server 는 DEALER 소켓이라 inbound ROUTER routing 설정이 없다.
        throw new ZLinkConfigurationException(
            "Dealer mesh server capability does not support inbound routing configuration; "
            + "dealer mesh uses a DEALER socket shared by server and client.");
    }
}

internal sealed class ZLinkChannelPublisherCapabilityBuilder(ZLinkChannelPublisherCapabilityRegistration registration)
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Channel publisher bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public IZLinkSocketConfig ConfigureSocket()
    {
        return registration.SocketConfig;
    }
}
