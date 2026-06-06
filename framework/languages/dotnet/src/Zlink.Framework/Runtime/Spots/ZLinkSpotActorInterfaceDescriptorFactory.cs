namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotActorInterfaceDescriptorFactory
{
    public static IEnumerable<ZLinkSpotActorInferredHandlerDescriptor> CreateInferredDescriptors(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        string? packetName)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            var packet = TryCreatePacket(
                surface,
                expectedSpotType,
                handlerType,
                null,
                definition,
                arguments,
                packetName);
            if (packet is not null)
            {
                yield return new ZLinkSpotActorInferredHandlerDescriptor { Packet = packet };
                continue;
            }

            var disconnected = TryCreateDisconnected(
                surface,
                expectedSpotType,
                handlerType,
                null,
                definition,
                arguments);
            if (disconnected is not null)
            {
                yield return new ZLinkSpotActorInferredHandlerDescriptor { Disconnected = disconnected };
            }
        }
    }

    public static ZLinkSpotActorPacketDescriptor? TryCreatePacket(
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

            return CreatePacket(surface, expectedSpotType, handlerType, expectedActorType, arguments, null, packetName);
        }

        if (definition == typeof(IZLinkEntrySpotActorRequestHandler<,,,>))
        {
            if (surface != ZLinkSpotActorHandlerSurface.EntrySpot)
            {
                return null;
            }

            return CreatePacket(surface, expectedSpotType, handlerType, expectedActorType, arguments, arguments[3], packetName);
        }

        if (definition == typeof(IZLinkSpotActorSendHandler<,,>))
        {
            if (surface != ZLinkSpotActorHandlerSurface.UserSpot)
            {
                return null;
            }

            return CreatePacket(surface, expectedSpotType, handlerType, expectedActorType, arguments, null, packetName);
        }

        if (definition == typeof(IZLinkSpotActorRequestHandler<,,,>))
        {
            if (surface != ZLinkSpotActorHandlerSurface.UserSpot)
            {
                return null;
            }

            return CreatePacket(surface, expectedSpotType, handlerType, expectedActorType, arguments, arguments[3], packetName);
        }

        return null;
    }

    public static ZLinkSpotActorLifecycleDescriptor? TryCreateDisconnected(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type? expectedActorType,
        Type definition,
        Type[] arguments)
    {
        if (definition == typeof(IZLinkEntrySpotActorDisconnectedHandler<,>))
        {
            if (surface != ZLinkSpotActorHandlerSurface.EntrySpot)
            {
                return null;
            }

            return CreateDisconnected(surface, expectedSpotType, handlerType, expectedActorType, arguments);
        }

        if (definition == typeof(IZLinkSpotActorDisconnectedHandler<,>))
        {
            if (surface != ZLinkSpotActorHandlerSurface.UserSpot)
            {
                return null;
            }

            return CreateDisconnected(surface, expectedSpotType, handlerType, expectedActorType, arguments);
        }

        return null;
    }

    private static ZLinkSpotActorPacketDescriptor CreatePacket(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type? expectedActorType,
        Type[] arguments,
        Type? replyType,
        string? packetName)
    {
        ZLinkSpotActorDescriptorBuilder.ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
        ZLinkSpotActorDescriptorBuilder.ValidateActorType(handlerType, expectedActorType, arguments[1]);
        return ZLinkSpotActorDescriptorBuilder.CreatePacket(
            surface,
            handlerType,
            arguments[0],
            arguments[1],
            arguments[2],
            replyType,
            packetName);
    }

    private static ZLinkSpotActorLifecycleDescriptor CreateDisconnected(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type? expectedActorType,
        Type[] arguments)
    {
        ZLinkSpotActorDescriptorBuilder.ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
        ZLinkSpotActorDescriptorBuilder.ValidateActorType(handlerType, expectedActorType, arguments[1]);
        return ZLinkSpotActorDescriptorBuilder.CreateLifecycle(
            surface,
            handlerType,
            arguments[0],
            arguments[1]);
    }
}
