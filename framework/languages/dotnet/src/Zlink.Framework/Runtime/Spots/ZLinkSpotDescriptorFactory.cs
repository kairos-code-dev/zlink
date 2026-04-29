namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotDescriptorFactory
{
    public static ZLinkSpotDescriptor CreatePacketDescriptor(Type handlerType, Type expectedSpotType)
    {
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType)
            {
                continue;
            }

            var definition = implemented.GetGenericTypeDefinition();
            if (definition == typeof(IZLinkSpotPacketHandler<,>))
            {
                var arguments = implemented.GetGenericArguments();
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                return new ZLinkSpotDescriptor
                {
                    HandlerType = handlerType,
                    SpotType = arguments[0],
                    MessageType = arguments[1],
                    HandleMethod = handlerType.GetMethod("HandleAsync")!,
                    MessageName = ZLinkMessageNameResolver.ResolveFromType(arguments[1]),
                };
            }

            if (definition == typeof(IZLinkSpotRequestHandler<,,>))
            {
                var arguments = implemented.GetGenericArguments();
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                return new ZLinkSpotDescriptor
                {
                    HandlerType = handlerType,
                    SpotType = arguments[0],
                    MessageType = arguments[1],
                    ReplyType = arguments[2],
                    HandleMethod = handlerType.GetMethod("HandleAsync")!,
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
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkSpotSubscriptionHandler<,>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            return new ZLinkSpotSubscriptionDescriptor
            {
                Topic = topic,
                HandlerType = handlerType,
                SpotType = arguments[0],
                MessageType = arguments[1],
                HandleMethod = handlerType.GetMethod("HandleAsync")!,
                MessageName = ZLinkMessageNameResolver.ResolveFromType(arguments[1]),
            };
        }

        throw new InvalidOperationException(
            $"SPOT subscription handler '{handlerType}' must implement IZLinkSpotSubscriptionHandler<,>.");
    }

    public static ZLinkSpotTimerDescriptor CreateTimerDescriptor(
        string name,
        Type handlerType,
        Type expectedSpotType)
    {
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkSpotTimerHandler<>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            return new ZLinkSpotTimerDescriptor
            {
                Name = name,
                Period = TimeSpan.Zero,
                HandlerType = handlerType,
                SpotType = arguments[0],
                HandleMethod = handlerType.GetMethod("HandleAsync")!,
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
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkSpotActorJoinHandler<,,,>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
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
                HandleMethod = handlerType.GetMethod("HandleAsync")!,
                MessageName = ZLinkMessageNameResolver.ResolveFromType(arguments[2]),
            };
        }

        throw new InvalidOperationException(
            $"SPOT actor join handler '{handlerType}' must implement IZLinkSpotActorJoinHandler<,,,>.");
    }

    private static void ValidateSpotType(Type handlerType, Type expectedSpotType, Type actualSpotType)
    {
        if (actualSpotType != expectedSpotType)
        {
            throw new InvalidOperationException(
                $"SPOT handler '{handlerType}' targets '{actualSpotType}', but the runtime spot type is '{expectedSpotType}'.");
        }
    }

    private static void ValidateActorType(Type handlerType, Type expectedActorType, Type actualActorType)
    {
        if (actualActorType != expectedActorType)
        {
            throw new InvalidOperationException(
                $"SPOT actor join handler '{handlerType}' targets actor '{actualActorType}', but registration expects '{expectedActorType}'.");
        }
    }
}
