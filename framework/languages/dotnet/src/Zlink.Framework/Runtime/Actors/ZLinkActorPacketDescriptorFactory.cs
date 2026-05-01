using System.Reflection;
using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Actors;

internal static class ZLinkActorPacketDescriptorFactory
{
    private static readonly ZlinkStreamPacketNameResolver MessageNameResolver = new();

    public static ZLinkActorPacketDescriptor Create(
        Type handlerType,
        Type expectedActorType,
        string? messageName)
    {
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkActorPacketHandler<,>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            if (!arguments[0].IsAssignableFrom(expectedActorType))
            {
                throw new InvalidOperationException(
                    $"Actor packet handler '{handlerType}' targets '{arguments[0]}', but the runtime actor type is '{expectedActorType}'.");
            }

            return new ZLinkActorPacketDescriptor
            {
                HandlerType = handlerType,
                ActorType = arguments[0],
                MessageType = arguments[1],
                ReplyType = null,
                Kind = ZLinkMessageKind.Command,
                HandleMethod = FindHandleMethod(handlerType),
                MessageName = messageName ?? MessageNameResolver.Resolve(arguments[1])
            };
        }

        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkActorRequestHandler<,,>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            if (!arguments[0].IsAssignableFrom(expectedActorType))
            {
                throw new InvalidOperationException(
                    $"Actor request handler '{handlerType}' targets '{arguments[0]}', but the runtime actor type is '{expectedActorType}'.");
            }

            return new ZLinkActorPacketDescriptor
            {
                HandlerType = handlerType,
                ActorType = arguments[0],
                MessageType = arguments[1],
                ReplyType = arguments[2],
                Kind = ZLinkMessageKind.Request,
                HandleMethod = FindHandleMethod(handlerType),
                MessageName = messageName ?? MessageNameResolver.Resolve(arguments[1])
            };
        }

        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkActorSendHandler<>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            return new ZLinkActorPacketDescriptor
            {
                HandlerType = handlerType,
                ActorType = null,
                MessageType = arguments[0],
                ReplyType = null,
                Kind = ZLinkMessageKind.Command,
                HandleMethod = FindHandleMethod(handlerType),
                MessageName = messageName ?? MessageNameResolver.Resolve(arguments[0])
            };
        }

        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkActorRequestHandler<,>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            return new ZLinkActorPacketDescriptor
            {
                HandlerType = handlerType,
                ActorType = null,
                MessageType = arguments[0],
                ReplyType = arguments[1],
                Kind = ZLinkMessageKind.Request,
                HandleMethod = FindHandleMethod(handlerType),
                MessageName = messageName ?? MessageNameResolver.Resolve(arguments[0])
            };
        }

        throw new InvalidOperationException(
            $"Actor packet handler '{handlerType}' must implement a supported actor handler interface.");
    }

    private static MethodInfo FindHandleMethod(Type handlerType)
    {
        return handlerType.GetMethod("HandleAsync", BindingFlags.Instance | BindingFlags.Public)
            ?? throw new InvalidOperationException($"Actor packet handler '{handlerType}' does not expose HandleAsync.");
    }
}
