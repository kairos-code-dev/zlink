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
            var descriptor = ZLinkSpotActorInterfaceDescriptorFactory.TryCreatePacket(
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

        foreach (var descriptor in ZLinkSpotActorAttributedDescriptorFactory.CreatePacketDescriptors(
                     surface,
                     expectedSpotType,
                     handlerType,
                     expectedActorType,
                     packetName))
        {
            return descriptor;
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
        matches.AddRange(ZLinkSpotActorInterfaceDescriptorFactory.CreateInferredDescriptors(
            surface,
            expectedSpotType,
            handlerType,
            packetName));
        matches.AddRange(ZLinkSpotActorAttributedDescriptorFactory.CreateInferredDescriptors(
            surface,
            expectedSpotType,
            handlerType,
            packetName));

        return matches.Count switch
        {
            1 => matches[0],
            0 => throw new InvalidOperationException(
                $"Actor handler '{handlerType}' must implement exactly one supported Entry Spot or user Spot actor handler interface or declare exactly one SPOT actor handler attribute."),
            _ => throw new InvalidOperationException(
                $"Actor handler '{handlerType}' implements multiple supported actor handler interfaces. Use AddActorPacket or AddActorDisconnected to select the actor surface explicitly."),
        };
    }

    public static ZLinkSpotActorLifecycleDescriptor CreateDisconnected(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type expectedActorType)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            var descriptor = ZLinkSpotActorInterfaceDescriptorFactory.TryCreateDisconnected(
                surface,
                expectedSpotType,
                handlerType,
                expectedActorType,
                definition,
                arguments);
            if (descriptor is not null)
            {
                return descriptor;
            }
        }

        var attributed = ZLinkSpotActorAttributedDescriptorFactory.TryCreateDisconnected(
            surface,
            expectedSpotType,
            handlerType,
            expectedActorType);
        if (attributed is not null)
        {
            return attributed;
        }

        throw new InvalidOperationException(
            $"Actor disconnected handler '{handlerType}' must implement a supported SPOT actor disconnected handler interface or declare a matching SPOT actor disconnected attribute.");
    }
}
