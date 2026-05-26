using System.Reflection;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotActorHandlerDescriptorFactory
{
    public static ZLinkSpotActorPacketDescriptor CreatePacket(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type expectedActorType,
        string? packetName)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            var descriptor = TryCreatePacketDescriptor(
                surface,
                expectedSpotType,
                handlerType,
                expectedActorType,
                definition,
                arguments,
                packetName);
            if (descriptor is not null)
            {
                return descriptor;
            }
        }

        foreach (var method in EnumerateAttributedPacketMethods(handlerType))
        {
            var descriptor = TryCreateAttributedPacketDescriptor(
                surface,
                expectedSpotType,
                handlerType,
                expectedActorType,
                method,
                packetName);
            if (descriptor is not null)
            {
                return descriptor;
            }
        }

        throw new InvalidOperationException(
            $"Actor packet handler '{handlerType}' must implement a supported Entry Spot or user Spot actor handler interface or declare one SPOT actor packet attribute.");
    }

    public static ZLinkSpotActorInferredHandlerDescriptor CreateInferred(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        string? packetName)
    {
        var matches = new List<ZLinkSpotActorInferredHandlerDescriptor>();
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            var packet = TryCreatePacketDescriptor(
                surface,
                expectedSpotType,
                handlerType,
                null,
                definition,
                arguments,
                packetName);
            if (packet is not null)
            {
                matches.Add(new ZLinkSpotActorInferredHandlerDescriptor { Packet = packet });
                continue;
            }

            var joined = TryCreateLifecycleDescriptor(
                surface,
                expectedSpotType,
                handlerType,
                null,
                definition,
                arguments,
                joined: true);
            if (joined is not null)
            {
                matches.Add(new ZLinkSpotActorInferredHandlerDescriptor { Joined = joined });
                continue;
            }

            var left = TryCreateLifecycleDescriptor(
                surface,
                expectedSpotType,
                handlerType,
                null,
                definition,
                arguments,
                joined: false);
            if (left is not null)
            {
                matches.Add(new ZLinkSpotActorInferredHandlerDescriptor { Left = left });
            }
        }

        foreach (var method in EnumerateAttributedPacketMethods(handlerType))
        {
            var packet = TryCreateAttributedPacketDescriptor(
                surface,
                expectedSpotType,
                handlerType,
                null,
                method,
                packetName);
            if (packet is not null)
            {
                matches.Add(new ZLinkSpotActorInferredHandlerDescriptor { Packet = packet });
            }
        }

        foreach (var method in EnumerateAttributedLifecycleMethods(handlerType))
        {
            var lifecycle = TryCreateAttributedLifecycleDescriptor(
                surface,
                expectedSpotType,
                handlerType,
                null,
                method);
            if (lifecycle is not null)
            {
                matches.Add(lifecycle);
            }
        }

        return matches.Count switch
        {
            1 => matches[0],
            0 => throw new InvalidOperationException(
                $"Actor handler '{handlerType}' must implement exactly one supported Entry Spot or user Spot actor handler interface or declare exactly one SPOT actor handler attribute."),
            _ => throw new InvalidOperationException(
                $"Actor handler '{handlerType}' implements multiple supported actor handler interfaces. Use the explicit actor registration method."),
        };
    }

    private static ZLinkSpotActorPacketDescriptor? TryCreatePacketDescriptor(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type? expectedActorType,
        Type definition,
        Type[] arguments,
        string? packetName)
    {
        if (definition == typeof(IZLinkEntrySpotActorSendHandler<,,>))
        {
            if (surface != ZLinkSpotActorHandlerSurface.EntrySpot)
            {
                return null;
            }

            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            ValidateActorType(handlerType, expectedActorType, arguments[1]);
            return CreatePacketDescriptor(
                surface,
                handlerType,
                arguments[0],
                arguments[1],
                arguments[2],
                null,
                packetName);
        }

        if (definition == typeof(IZLinkEntrySpotActorRequestHandler<,,,>))
        {
            if (surface != ZLinkSpotActorHandlerSurface.EntrySpot)
            {
                return null;
            }

            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            ValidateActorType(handlerType, expectedActorType, arguments[1]);
            return CreatePacketDescriptor(
                surface,
                handlerType,
                arguments[0],
                arguments[1],
                arguments[2],
                arguments[3],
                packetName);
        }

        if (definition == typeof(IZLinkSpotActorSendHandler<,,>))
        {
            if (surface != ZLinkSpotActorHandlerSurface.UserSpot)
            {
                return null;
            }

            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            ValidateActorType(handlerType, expectedActorType, arguments[1]);
            return CreatePacketDescriptor(
                surface,
                handlerType,
                arguments[0],
                arguments[1],
                arguments[2],
                null,
                packetName);
        }

        if (definition == typeof(IZLinkSpotActorRequestHandler<,,,>))
        {
            if (surface != ZLinkSpotActorHandlerSurface.UserSpot)
            {
                return null;
            }

            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            ValidateActorType(handlerType, expectedActorType, arguments[1]);
            return CreatePacketDescriptor(
                surface,
                handlerType,
                arguments[0],
                arguments[1],
                arguments[2],
                arguments[3],
                packetName);
        }

        return null;
    }

    public static ZLinkSpotActorLifecycleDescriptor CreateLifecycle(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type expectedActorType,
        bool joined)
    {
        var expectedDefinition = joined
            ? typeof(IZLinkSpotPostActorJoinedHandler<,>).ToString()
            : typeof(IZLinkSpotActorLeftHandler<,>).ToString();

        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            var descriptor = TryCreateLifecycleDescriptor(
                surface,
                expectedSpotType,
                handlerType,
                expectedActorType,
                definition,
                arguments,
                joined);
            if (descriptor is null)
            {
                continue;
            }

            return descriptor;
        }

        foreach (var method in EnumerateAttributedLifecycleMethods(handlerType))
        {
            var descriptor = TryCreateAttributedLifecycleDescriptor(
                surface,
                expectedSpotType,
                handlerType,
                expectedActorType,
                method);
            var candidate = joined ? descriptor?.Joined : descriptor?.Left;
            if (candidate is not null)
            {
                return candidate;
            }
        }

        throw new InvalidOperationException(
            $"Actor lifecycle handler '{handlerType}' must implement '{expectedDefinition}' or declare a matching SPOT actor lifecycle attribute.");
    }

    private static ZLinkSpotActorLifecycleDescriptor? TryCreateLifecycleDescriptor(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type? expectedActorType,
        Type definition,
        Type[] arguments,
        bool joined)
    {
        var postActorJoinedDefinition = typeof(IZLinkSpotPostActorJoinedHandler<,>);
        var spotDefinition = joined
            ? postActorJoinedDefinition
            : typeof(IZLinkSpotActorLeftHandler<,>);

        if (definition != spotDefinition)
        {
            return null;
        }

        ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
        ValidateActorType(handlerType, expectedActorType, arguments[1]);
        return new ZLinkSpotActorLifecycleDescriptor
        {
            HandlerType = handlerType,
            SpotType = arguments[0],
            ActorType = arguments[1],
            Invoker = CreateInvoker(handlerType),
            Surface = surface
        };
    }

    private static ZLinkSpotActorPacketDescriptor CreatePacketDescriptor(
        ZLinkSpotActorHandlerSurface surface,
        Type handlerType,
        Type? spotType,
        Type actorType,
        Type messageType,
        Type? replyType,
        string? packetName)
    {
        return CreatePacketDescriptor(
            surface,
            handlerType,
            spotType,
            actorType,
            messageType,
            replyType,
            packetName,
            CreateInvoker(handlerType));
    }

    private static ZLinkSpotActorPacketDescriptor CreatePacketDescriptor(
        ZLinkSpotActorHandlerSurface surface,
        Type handlerType,
        Type? spotType,
        Type actorType,
        Type messageType,
        Type? replyType,
        string? packetName,
        ZLinkHandlerMethodInvoker invoker)
    {
        return new ZLinkSpotActorPacketDescriptor
        {
            HandlerType = handlerType,
            SpotType = spotType,
            ActorType = actorType,
            MessageType = messageType,
            ReplyType = replyType,
            Kind = replyType is null ? ZLinkMessageKind.Command : ZLinkMessageKind.Request,
            Invoker = invoker,
            MessageName = packetName ?? ZLinkMessageNameResolver.ResolveFromType(messageType),
            Surface = surface
        };
    }

    private static ZLinkHandlerMethodInvoker CreateInvoker(Type handlerType)
    {
        return ZLinkHandlerContractDescriptorSupport.CreateHandleAsyncInvoker(handlerType, "Handler");
    }

    private static void ValidateSpotType(Type handlerType, Type? expectedSpotType, Type actualSpotType)
    {
        if (expectedSpotType is null)
        {
            throw new InvalidOperationException(
                $"SPOT actor handler '{handlerType}' targets SPOT '{actualSpotType}', but registration expects '{expectedSpotType}'.");
        }

        ZLinkHandlerContractDescriptorSupport.RequireExactType(
            handlerType,
            expectedSpotType,
            actualSpotType,
            "SPOT actor handler");
    }

    private static void ValidateActorType(Type handlerType, Type? expectedActorType, Type actualActorType)
    {
        if (expectedActorType is null)
        {
            return;
        }

        ZLinkHandlerContractDescriptorSupport.RequireAssignableFrom(
            handlerType,
            expectedActorType,
            actualActorType,
            "SPOT actor handler");
    }

    private static IEnumerable<MethodInfo> EnumerateAttributedPacketMethods(Type handlerType)
    {
        return handlerType
            .GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .Where(static method =>
                method.GetCustomAttribute<ZLinkSpotActorSendAttribute>() is not null
                || method.GetCustomAttribute<ZLinkSpotActorRequestAttribute>() is not null);
    }

    private static IEnumerable<MethodInfo> EnumerateAttributedLifecycleMethods(Type handlerType)
    {
        return handlerType
            .GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .Where(static method =>
                method.GetCustomAttribute<ZLinkSpotPostActorJoinedAttribute>() is not null
                || method.GetCustomAttribute<ZLinkSpotActorLeftAttribute>() is not null);
    }

    private static ZLinkSpotActorPacketDescriptor? TryCreateAttributedPacketDescriptor(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type? expectedActorType,
        MethodInfo method,
        string? packetNameOverride)
    {
        var send = method.GetCustomAttribute<ZLinkSpotActorSendAttribute>();
        var request = method.GetCustomAttribute<ZLinkSpotActorRequestAttribute>();
        if (send is not null && request is not null)
        {
            throw new InvalidOperationException(
                $"SPOT actor handler '{handlerType}' method '{method.Name}' cannot declare both send and request attributes.");
        }

        if (send is null && request is null)
        {
            return null;
        }

        var parameters = RequireParameterCount(handlerType, method, 4, "SPOT actor packet handler");
        var spotType = parameters[0].ParameterType;
        var actorType = parameters[1].ParameterType;
        var messageType = parameters[2].ParameterType;
        RequireCancellationToken(handlerType, method, parameters[3], "SPOT actor packet handler");
        ValidateSpotType(handlerType, expectedSpotType, spotType);
        ValidateActorType(handlerType, expectedActorType, actorType);
        var replyType = request is null ? null : GetReplyType(method.ReturnType);
        if (send is not null)
        {
            RequireNoReply(handlerType, method, "SPOT actor send handler");
        }

        var packetName = packetNameOverride ?? send?.PacketName ?? request?.PacketName;
        return CreatePacketDescriptor(
            surface,
            handlerType,
            spotType,
            actorType,
            messageType,
            replyType,
            packetName,
            ZLinkHandlerMethodInvokerFactory.Create(method));
    }

    private static ZLinkSpotActorInferredHandlerDescriptor? TryCreateAttributedLifecycleDescriptor(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type? expectedActorType,
        MethodInfo method)
    {
        var postJoined = method.GetCustomAttribute<ZLinkSpotPostActorJoinedAttribute>();
        var left = method.GetCustomAttribute<ZLinkSpotActorLeftAttribute>();
        if (postJoined is not null && left is not null)
        {
            throw new InvalidOperationException(
                $"SPOT actor handler '{handlerType}' method '{method.Name}' cannot declare both joined and left attributes.");
        }

        if (postJoined is null && left is null)
        {
            return null;
        }

        var parameters = RequireParameterCount(handlerType, method, 4, "SPOT actor lifecycle handler");
        var spotType = parameters[0].ParameterType;
        var actorType = parameters[1].ParameterType;
        if (parameters[2].ParameterType != typeof(ZLinkSpotActorChangeResult))
        {
            throw new InvalidOperationException(
                $"SPOT actor lifecycle handler '{handlerType}' method '{method.Name}' must use ZLinkSpotActorChangeResult as the third parameter.");
        }

        RequireCancellationToken(handlerType, method, parameters[3], "SPOT actor lifecycle handler");
        RequireNoReply(handlerType, method, "SPOT actor lifecycle handler");
        ValidateSpotType(handlerType, expectedSpotType, spotType);
        ValidateActorType(handlerType, expectedActorType, actorType);
        var descriptor = new ZLinkSpotActorLifecycleDescriptor
        {
            HandlerType = handlerType,
            SpotType = spotType,
            ActorType = actorType,
            Invoker = ZLinkHandlerMethodInvokerFactory.Create(method),
            Surface = surface
        };
        return postJoined is not null
            ? new ZLinkSpotActorInferredHandlerDescriptor { Joined = descriptor }
            : new ZLinkSpotActorInferredHandlerDescriptor { Left = descriptor };
    }

    private static ParameterInfo[] RequireParameterCount(
        Type handlerType,
        MethodInfo method,
        int expectedCount,
        string description)
    {
        var parameters = method.GetParameters();
        if (parameters.Length != expectedCount)
        {
            throw new InvalidOperationException(
                $"{description} '{handlerType}' method '{method.Name}' must declare exactly {expectedCount} parameters.");
        }

        return parameters;
    }

    private static void RequireCancellationToken(
        Type handlerType,
        MethodInfo method,
        ParameterInfo parameter,
        string description)
    {
        if (parameter.ParameterType != typeof(CancellationToken))
        {
            throw new InvalidOperationException(
                $"{description} '{handlerType}' method '{method.Name}' must use CancellationToken as the fourth parameter.");
        }
    }

    private static Type GetReplyType(Type returnType)
    {
        if (returnType.IsGenericType
            && (returnType.GetGenericTypeDefinition() == typeof(ValueTask<>)
                || returnType.GetGenericTypeDefinition() == typeof(Task<>)))
        {
            return returnType.GetGenericArguments()[0];
        }

        if (returnType == typeof(ValueTask)
            || returnType == typeof(Task)
            || returnType == typeof(void))
        {
            throw new InvalidOperationException("SPOT actor request handler must return a reply value.");
        }

        return returnType;
    }

    private static void RequireNoReply(Type handlerType, MethodInfo method, string description)
    {
        var returnType = method.ReturnType;
        if (returnType == typeof(void)
            || returnType == typeof(ValueTask)
            || returnType == typeof(Task))
        {
            return;
        }

        throw new InvalidOperationException(
            $"{description} '{handlerType}' method '{method.Name}' must not return a reply value.");
    }
}
