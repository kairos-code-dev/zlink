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
            if (surface == ZLinkSpotActorHandlerSurface.EntrySpot
                && definition == typeof(IZLinkEntrySpotActorSendHandler<,>))
            {
                ValidateActorType(handlerType, expectedActorType, arguments[0]);
                return CreatePacketDescriptor(surface, handlerType, null, arguments[0], arguments[1], null, packetName);
            }

            if (surface == ZLinkSpotActorHandlerSurface.EntrySpot
                && definition == typeof(IZLinkEntrySpotActorRequestHandler<,,>))
            {
                ValidateActorType(handlerType, expectedActorType, arguments[0]);
                return CreatePacketDescriptor(surface, handlerType, null, arguments[0], arguments[1], arguments[2], packetName);
            }

            if (surface == ZLinkSpotActorHandlerSurface.EntrySpot
                && definition == typeof(IZLinkSpotActorSendHandler<,,>))
            {
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                ValidateActorType(handlerType, expectedActorType, arguments[1]);
                return CreatePacketDescriptor(surface, handlerType, arguments[0], arguments[1], arguments[2], null, packetName);
            }

            if (surface == ZLinkSpotActorHandlerSurface.EntrySpot
                && definition == typeof(IZLinkSpotActorRequestHandler<,,,>))
            {
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                ValidateActorType(handlerType, expectedActorType, arguments[1]);
                return CreatePacketDescriptor(surface, handlerType, arguments[0], arguments[1], arguments[2], arguments[3], packetName);
            }

            if (surface == ZLinkSpotActorHandlerSurface.UserSpot
                && definition == typeof(IZLinkSpotActorSendHandler<,,>))
            {
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                ValidateActorType(handlerType, expectedActorType, arguments[1]);
                return CreatePacketDescriptor(surface, handlerType, arguments[0], arguments[1], arguments[2], null, packetName);
            }

            if (surface == ZLinkSpotActorHandlerSurface.UserSpot
                && definition == typeof(IZLinkSpotActorRequestHandler<,,,>))
            {
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                ValidateActorType(handlerType, expectedActorType, arguments[1]);
                return CreatePacketDescriptor(surface, handlerType, arguments[0], arguments[1], arguments[2], arguments[3], packetName);
            }
        }

        throw new InvalidOperationException(
            $"Actor packet handler '{handlerType}' must implement a supported Entry Spot or user Spot actor handler interface.");
    }

    public static ZLinkSpotActorLifecycleDescriptor CreateLifecycle(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type expectedActorType,
        bool joined)
    {
        var entrySpotDefinition = joined
            ? typeof(IZLinkEntrySpotActorJoinedHandler<>)
            : typeof(IZLinkEntrySpotActorLeftHandler<>);
        var spotDefinition = joined
            ? typeof(IZLinkSpotActorJoinedHandler<,>)
            : typeof(IZLinkSpotActorLeftHandler<,>);
        var expectedDefinition = surface == ZLinkSpotActorHandlerSurface.EntrySpot
            ? $"{entrySpotDefinition} or {spotDefinition}"
            : spotDefinition.ToString();

        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (surface == ZLinkSpotActorHandlerSurface.EntrySpot
                && definition == entrySpotDefinition)
            {
                ValidateActorType(handlerType, expectedActorType, arguments[0]);
                return new ZLinkSpotActorLifecycleDescriptor
                {
                    HandlerType = handlerType,
                    ActorType = arguments[0],
                    Invoker = CreateInvoker(handlerType),
                    Surface = surface
                };
            }

            if (definition != spotDefinition)
            {
                continue;
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

        throw new InvalidOperationException(
            $"Actor lifecycle handler '{handlerType}' must implement '{expectedDefinition}'.");
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
        return new ZLinkSpotActorPacketDescriptor
        {
            HandlerType = handlerType,
            SpotType = spotType,
            ActorType = actorType,
            MessageType = messageType,
            ReplyType = replyType,
            Kind = replyType is null ? ZLinkMessageKind.Command : ZLinkMessageKind.Request,
            Invoker = CreateInvoker(handlerType),
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

    private static void ValidateActorType(Type handlerType, Type expectedActorType, Type actualActorType)
    {
        ZLinkHandlerContractDescriptorSupport.RequireAssignableFrom(
            handlerType,
            expectedActorType,
            actualActorType,
            "SPOT actor handler");
    }
}
