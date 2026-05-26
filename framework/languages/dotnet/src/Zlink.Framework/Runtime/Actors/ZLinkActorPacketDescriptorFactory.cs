namespace Zlink.Framework.Runtime.Actors;

internal static class ZLinkActorPacketDescriptorFactory
{
    private static readonly IZlinkStreamPacketNameResolver MessageNameResolver = ZLinkStreamProtocolDefaults.PacketNameResolver;

    public static ZLinkActorPacketDescriptor Create(
        Type handlerType,
        Type expectedActorType,
        string? messageName)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (definition != typeof(IZLinkActorPacketHandler<,>))
            {
                continue;
            }

            ValidateActorType(handlerType, expectedActorType, arguments[0]);

            return new ZLinkActorPacketDescriptor
            {
                HandlerType = handlerType,
                ActorType = arguments[0],
                MessageType = arguments[1],
                ReplyType = null,
                Kind = ZLinkMessageKind.Command,
                Invoker = CreateInvoker(handlerType),
                MessageName = messageName ?? MessageNameResolver.Resolve(arguments[1])
            };
        }

        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (definition != typeof(IZLinkActorRequestHandler<,,>))
            {
                continue;
            }

            ValidateActorType(handlerType, expectedActorType, arguments[0]);

            return new ZLinkActorPacketDescriptor
            {
                HandlerType = handlerType,
                ActorType = arguments[0],
                MessageType = arguments[1],
                ReplyType = arguments[2],
                Kind = ZLinkMessageKind.Request,
                Invoker = CreateInvoker(handlerType),
                MessageName = messageName ?? MessageNameResolver.Resolve(arguments[1])
            };
        }

        throw new InvalidOperationException(
            $"Actor packet handler '{handlerType}' must implement a supported actor handler interface.");
    }

    private static ZLinkHandlerMethodInvoker CreateInvoker(Type handlerType)
    {
        return ZLinkHandlerContractDescriptorSupport.CreateHandleAsyncInvoker(
            handlerType,
            "Actor packet handler");
    }

    private static void ValidateActorType(Type handlerType, Type expectedActorType, Type actualActorType)
    {
        ZLinkHandlerContractDescriptorSupport.RequireAssignableFrom(
            handlerType,
            expectedActorType,
            actualActorType,
            "Actor packet handler");
    }
}
