namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotDescriptorFactory
{
    public static ZLinkSpotDescriptor CreatePacketDescriptor(Type handlerType, Type expectedSpotType)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (definition == typeof(IZLinkSpotPacketHandler<,>))
            {
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                return new ZLinkSpotDescriptor
                {
                    HandlerType = handlerType,
                    SpotType = arguments[0],
                    MessageType = arguments[1],
                    Invoker = CreateInvoker(handlerType),
                    MessageName = ZLinkMessageNameResolver.ResolveFromType(arguments[1]),
                };
            }

            if (definition == typeof(IZLinkSpotRequestHandler<,,>))
            {
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                return new ZLinkSpotDescriptor
                {
                    HandlerType = handlerType,
                    SpotType = arguments[0],
                    MessageType = arguments[1],
                    ReplyType = arguments[2],
                    Invoker = CreateInvoker(handlerType),
                    MessageName = ZLinkMessageNameResolver.ResolveFromType(arguments[1]),
                };
            }
        }

        throw new InvalidOperationException(
            $"SPOT packet handler '{handlerType}' must implement IZLinkSpotPacketHandler<,> or IZLinkSpotRequestHandler<,,>.");
    }

    public static ZLinkSpotSubscriptionDescriptor CreateSubscriptionDescriptor(
        string topic,
        Type handlerType,
        Type expectedSpotType)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (definition != typeof(IZLinkSpotSubscriptionHandler<,>))
            {
                continue;
            }

            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            return new ZLinkSpotSubscriptionDescriptor
            {
                Topic = topic,
                HandlerType = handlerType,
                SpotType = arguments[0],
                MessageType = arguments[1],
                Invoker = CreateInvoker(handlerType),
                MessageName = ZLinkMessageNameResolver.ResolveFromType(arguments[1]),
            };
        }

        throw new InvalidOperationException(
            $"SPOT subscription handler '{handlerType}' must implement IZLinkSpotSubscriptionHandler<,>.");
    }

    public static ZLinkSpotTimerDescriptor CreateTimerDescriptor(
        string name,
        TimeSpan period,
        Type handlerType,
        Type expectedSpotType)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (definition != typeof(IZLinkSpotTimerHandler<>))
            {
                continue;
            }

            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            return new ZLinkSpotTimerDescriptor
            {
                Name = name,
                Period = period,
                HandlerType = handlerType,
                SpotType = arguments[0],
                Invoker = CreateInvoker(handlerType),
            };
        }

        throw new InvalidOperationException(
            $"SPOT timer handler '{handlerType}' must implement IZLinkSpotTimerHandler<>.");
    }

    public static ZLinkSpotActorJoinDescriptor CreateActorJoinDescriptor(
        Type handlerType,
        Type expectedSpotType,
        Type expectedActorType,
        Type expectedRequestType,
        Type expectedReplyType)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (definition != typeof(IZLinkSpotActorJoinHandler<,,,>))
            {
                continue;
            }

            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            ValidateActorType(handlerType, expectedActorType, arguments[1]);

            if (arguments[2] != expectedRequestType || arguments[3] != expectedReplyType)
            {
                throw new InvalidOperationException(
                    $"SPOT actor join handler '{handlerType}' targets '{arguments[2]}/{arguments[3]}', but registration expects '{expectedRequestType}/{expectedReplyType}'.");
            }

            return new ZLinkSpotActorJoinDescriptor
            {
                HandlerType = handlerType,
                SpotType = arguments[0],
                ActorType = arguments[1],
                RequestType = arguments[2],
                ReplyType = arguments[3],
                Invoker = CreateInvoker(handlerType),
                MessageName = ZLinkMessageNameResolver.ResolveFromType(arguments[2]),
            };
        }

        throw new InvalidOperationException(
            $"SPOT actor join handler '{handlerType}' must implement IZLinkSpotActorJoinHandler<,,,>.");
    }

    private static void ValidateSpotType(Type handlerType, Type expectedSpotType, Type actualSpotType)
    {
        ZLinkHandlerContractDescriptorSupport.RequireExactType(
            handlerType,
            expectedSpotType,
            actualSpotType,
            "SPOT handler");
    }

    private static void ValidateActorType(Type handlerType, Type expectedActorType, Type actualActorType)
    {
        ZLinkHandlerContractDescriptorSupport.RequireExactType(
            handlerType,
            expectedActorType,
            actualActorType,
            "SPOT actor join handler");
    }

    private static ZLinkHandlerMethodInvoker CreateInvoker(Type handlerType)
    {
        return ZLinkHandlerContractDescriptorSupport.CreateHandleAsyncInvoker(handlerType, "Handler");
    }
}
