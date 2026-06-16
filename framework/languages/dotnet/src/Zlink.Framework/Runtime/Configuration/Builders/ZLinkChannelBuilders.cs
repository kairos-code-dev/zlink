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

    public void AddHandlerGroup(string groupName)
    {
        ZLinkHandlerGroupBuilderSupport.AddHandlerGroup(registration, groupName);
    }

    public void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>
    {
        ZLinkChannelHandlerRegistrationBuilder.AddSendHandler<THandler, TMessage>(
            registration,
            packetName);
    }

    public void AddSendHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        ZLinkChannelHandlerRegistrationBuilder.AddSendHandler<THandler>(
            registration,
            packetName);
    }

    public void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>
    {
        ZLinkChannelHandlerRegistrationBuilder.AddRequestHandler<THandler, TRequest, TReply>(
            registration,
            packetName);
    }

    public void AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        ZLinkChannelHandlerRegistrationBuilder.AddRequestHandler<THandler>(
            registration,
            packetName);
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

    public void AddHandlerGroup(string groupName)
    {
        ZLinkHandlerGroupBuilderSupport.AddHandlerGroup(registration, groupName);
    }

    public void AddPublishHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkPublishHandler<TMessage>
    {
        ZLinkChannelHandlerRegistrationBuilder.AddPublishHandler<THandler, TMessage>(
            registration,
            packetName);
    }

    public void AddPublishHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        ZLinkChannelHandlerRegistrationBuilder.AddPublishHandler<THandler>(
            registration,
            packetName);
    }
}

internal sealed class ZLinkDealerMeshChannelBuilder(ZLinkChannelRegistration registration)
    : IZLinkDealerMeshChannelBuilder
{
    public void EnableServer(Action<IChannelServerCapabilityBuilder>? configure = null)
    {
        // dealer mesh 의 server·client 는 같은 DEALER 소켓을 공유한다. server(제공) 설정도
        // client capability registration(= 그 DEALER)에 기록한다. ROUTER 를 만드는
        // registration.Server 는 쓰지 않는다.
        registration.Client ??= new ZLinkChannelClientCapabilityRegistration();
        configure?.Invoke(new ZLinkDealerMeshChannelServerCapabilityBuilder(registration.Client));
    }

    public void EnableClient(Action<IDealerMeshChannelClientCapabilityBuilder>? configure = null)
    {
        registration.Client ??= new ZLinkChannelClientCapabilityRegistration();
        configure?.Invoke(new ZLinkDealerMeshChannelClientCapabilityBuilder(registration.Client));
    }

    public void AddHandlerGroup(string groupName)
    {
        ZLinkHandlerGroupBuilderSupport.AddHandlerGroup(registration, groupName);
    }

    public void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>
    {
        ZLinkChannelHandlerRegistrationBuilder.AddSendHandler<THandler, TMessage>(
            registration,
            packetName);
    }

    public void AddSendHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        ZLinkChannelHandlerRegistrationBuilder.AddSendHandler<THandler>(
            registration,
            packetName);
    }

    public void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>
    {
        ZLinkChannelHandlerRegistrationBuilder.AddRequestHandler<THandler, TRequest, TReply>(
            registration,
            packetName);
    }

    public void AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        ZLinkChannelHandlerRegistrationBuilder.AddRequestHandler<THandler>(
            registration,
            packetName);
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

    public void UseManualConnections(Action<IZLinkEndpointConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}

internal sealed class ZLinkDealerMeshChannelServerCapabilityBuilder(ZLinkChannelClientCapabilityRegistration registration)
    : IChannelServerCapabilityBuilder
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Dealer mesh channel server bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public void ConfigureSocket(Action<IZLinkSocketConfig> configure)
    {
        configure(registration.SocketConfig);
    }

    public void ConfigureRouting(Action<IZLinkRouteConfig> configure)
    {
        // dealer mesh server 는 DEALER 소켓이라 inbound ROUTER routing 설정이 없다.
        throw new ZLinkConfigurationException(
            "Dealer mesh server capability does not support inbound routing configuration; "
            + "dealer mesh uses a DEALER socket shared by server and client.");
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

    public void UseManualConnections(Action<IZLinkEndpointConnections> configure)
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

    public void UseManualConnections(Action<IZLinkEndpointConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }
}
