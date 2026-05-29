using System.Reflection;

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
        Type? expectedActorType,
        Type? expectedRequestType,
        Type? expectedReplyType)
    {
        var matches = new List<ZLinkSpotActorJoinDescriptor>();
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (definition != typeof(IZLinkSpotActorJoinHandler<,,,>))
            {
                continue;
            }

            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            ValidateActorType(handlerType, expectedActorType, arguments[1]);

            if ((expectedRequestType is not null && arguments[2] != expectedRequestType)
                || (expectedReplyType is not null && arguments[3] != expectedReplyType))
            {
                throw new InvalidOperationException(
                    $"SPOT actor join handler '{handlerType}' targets '{arguments[2]}/{arguments[3]}', but registration expects '{expectedRequestType}/{expectedReplyType}'.");
            }

            matches.Add(CreateActorJoinDescriptor(
                handlerType,
                arguments[0],
                arguments[1],
                arguments[2],
                arguments[3],
                CreateInvoker(handlerType)));
        }

        foreach (var method in EnumerateAttributedMethods<ZLinkSpotActorJoinAttribute>(handlerType))
        {
            matches.Add(CreateAttributedActorJoinDescriptor(
                handlerType,
                method,
                expectedSpotType,
                expectedActorType,
                expectedRequestType,
                expectedReplyType));
        }

        return matches.Count switch
        {
            1 => matches[0],
            0 => throw new InvalidOperationException(
                $"SPOT actor join handler '{handlerType}' must implement IZLinkSpotActorJoinHandler<,,,> or declare one [ZLinkSpotActorJoin] method."),
            _ => throw new InvalidOperationException(
                $"SPOT actor join handler '{handlerType}' declares multiple actor join handlers."),
        };
    }

    private static void ValidateSpotType(Type handlerType, Type expectedSpotType, Type actualSpotType)
    {
        ZLinkHandlerContractDescriptorSupport.RequireExactType(
            handlerType,
            expectedSpotType,
            actualSpotType,
            "SPOT handler");
    }

    private static void ValidateActorType(Type handlerType, Type? expectedActorType, Type actualActorType)
    {
        if (expectedActorType is null)
        {
            if (!typeof(IZLinkActor).IsAssignableFrom(actualActorType))
            {
                throw new InvalidOperationException(
                    $"SPOT actor join handler '{handlerType}' targets '{actualActorType}', but actor type must implement '{typeof(IZLinkActor)}'.");
            }

            return;
        }

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

    private static ZLinkSpotActorJoinDescriptor CreateAttributedActorJoinDescriptor(
        Type handlerType,
        MethodInfo method,
        Type expectedSpotType,
        Type? expectedActorType,
        Type? expectedRequestType,
        Type? expectedReplyType)
    {
        var parameters = ZLinkHandlerMethodShape.RequireParameterCount(handlerType, method, 4, "SPOT actor join handler");
        var spotType = parameters[0].ParameterType;
        var actorType = parameters[1].ParameterType;
        var requestType = parameters[2].ParameterType;
        ZLinkHandlerMethodShape.RequireCancellationToken(
            handlerType,
            method,
            parameters[3],
            "SPOT actor join handler",
            "fourth");

        ValidateSpotType(handlerType, expectedSpotType, spotType);
        ValidateActorType(handlerType, expectedActorType, actorType);
        if ((expectedRequestType is not null && requestType != expectedRequestType)
            || (expectedReplyType is not null && GetReplyType(method.ReturnType) != expectedReplyType))
        {
            throw new InvalidOperationException(
                $"SPOT actor join handler '{handlerType}' targets '{requestType}/{GetReplyType(method.ReturnType)}', but registration expects '{expectedRequestType}/{expectedReplyType}'.");
        }

        return CreateActorJoinDescriptor(
            handlerType,
            spotType,
            actorType,
            requestType,
            GetReplyType(method.ReturnType),
            ZLinkHandlerMethodInvokerFactory.Create(method));
    }

    private static ZLinkSpotActorJoinDescriptor CreateActorJoinDescriptor(
        Type handlerType,
        Type spotType,
        Type actorType,
        Type requestType,
        Type replyType,
        ZLinkHandlerMethodInvoker invoker)
    {
        return new ZLinkSpotActorJoinDescriptor
        {
            HandlerType = handlerType,
            SpotType = spotType,
            ActorType = actorType,
            RequestType = requestType,
            ReplyType = replyType,
            Invoker = invoker,
            MessageName = ZLinkMessageNameResolver.ResolveFromType(requestType),
        };
    }

    private static Type GetReplyType(Type returnType)
        => ZLinkHandlerMethodShape.RequireReplyType(returnType, "SPOT actor join handler");

    private static IEnumerable<MethodInfo> EnumerateAttributedMethods<TAttribute>(Type handlerType)
        where TAttribute : Attribute
    {
        return handlerType
            .GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .Where(static method => method.GetCustomAttribute<TAttribute>() is not null);
    }
}
