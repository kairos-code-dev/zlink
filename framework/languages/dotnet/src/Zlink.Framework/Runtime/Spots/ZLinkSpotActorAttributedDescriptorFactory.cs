using System.Reflection;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotActorAttributedDescriptorFactory
{
    public static IEnumerable<ZLinkSpotActorPacketDescriptor> CreatePacketDescriptors(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type? expectedActorType,
        string? packetNameOverride)
    {
        foreach (var method in EnumeratePacketMethods(handlerType))
        {
            var descriptor = TryCreatePacket(
                surface,
                expectedSpotType,
                handlerType,
                expectedActorType,
                method,
                packetNameOverride);
            if (descriptor is not null)
            {
                yield return descriptor;
            }
        }
    }

    public static IEnumerable<ZLinkSpotActorInferredHandlerDescriptor> CreateInferredDescriptors(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        string? packetName)
    {
        foreach (var descriptor in CreatePacketDescriptors(surface, expectedSpotType, handlerType, null, packetName))
        {
            yield return new ZLinkSpotActorInferredHandlerDescriptor { Packet = descriptor };
        }

        foreach (var method in EnumerateLifecycleMethods(handlerType))
        {
            var descriptor = TryCreateLifecycle(
                surface,
                expectedSpotType,
                handlerType,
                null,
                method);
            if (descriptor is not null)
            {
                yield return descriptor;
            }
        }

        foreach (var method in EnumerateDisconnectedMethods(handlerType))
        {
            var descriptor = TryCreateDisconnected(
                surface,
                expectedSpotType,
                handlerType,
                null,
                method);
            if (descriptor is not null)
            {
                yield return new ZLinkSpotActorInferredHandlerDescriptor { Disconnected = descriptor };
            }
        }
    }

    public static ZLinkSpotActorLifecycleDescriptor? TryCreateLifecycle(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type expectedActorType,
        bool joined)
    {
        foreach (var method in EnumerateLifecycleMethods(handlerType))
        {
            var descriptor = TryCreateLifecycle(
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

        return null;
    }

    public static ZLinkSpotActorLifecycleDescriptor? TryCreateDisconnected(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type expectedActorType)
    {
        foreach (var method in EnumerateDisconnectedMethods(handlerType))
        {
            var descriptor = TryCreateDisconnected(
                surface,
                expectedSpotType,
                handlerType,
                expectedActorType,
                method);
            if (descriptor is not null)
            {
                return descriptor;
            }
        }

        return null;
    }

    private static ZLinkSpotActorPacketDescriptor? TryCreatePacket(
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

        var parameters = ZLinkHandlerMethodShape.RequireParameterCount(handlerType, method, 5, "SPOT actor packet handler");
        var spotType = parameters[0].ParameterType;
        var actorType = parameters[1].ParameterType;
        var expectedContextType = request is null
            ? typeof(ZLinkSpotActorSendContext)
            : typeof(ZLinkSpotActorRequestContext);
        if (parameters[2].ParameterType != expectedContextType)
        {
            throw new InvalidOperationException(
                $"SPOT actor packet handler '{handlerType}' method '{method.Name}' must use {expectedContextType.Name} as the third parameter.");
        }

        var messageType = parameters[3].ParameterType;
        ZLinkHandlerMethodShape.RequireCancellationToken(handlerType, method, parameters[4], "SPOT actor packet handler");
        ZLinkSpotActorDescriptorBuilder.ValidateSpotType(handlerType, expectedSpotType, spotType);
        ZLinkSpotActorDescriptorBuilder.ValidateActorType(handlerType, expectedActorType, actorType);
        var replyType = request is null
            ? null
            : ZLinkSpotActorDescriptorBuilder.GetRequestReplyType(method.ReturnType);
        if (send is not null)
        {
            ZLinkHandlerMethodShape.RequireNoReply(handlerType, method, "SPOT actor send handler");
        }

        var packetName = packetNameOverride ?? send?.PacketName ?? request?.PacketName;
        return ZLinkSpotActorDescriptorBuilder.CreatePacket(
            surface,
            handlerType,
            spotType,
            actorType,
            messageType,
            replyType,
            packetName,
            ZLinkHandlerMethodInvokerFactory.Create(method));
    }

    private static ZLinkSpotActorInferredHandlerDescriptor? TryCreateLifecycle(
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

        var parameters = ZLinkHandlerMethodShape.RequireParameterCount(handlerType, method, 4, "SPOT actor lifecycle handler");
        var spotType = parameters[0].ParameterType;
        var actorType = parameters[1].ParameterType;
        if (parameters[2].ParameterType != typeof(ZLinkSpotActorChangeResult))
        {
            throw new InvalidOperationException(
                $"SPOT actor lifecycle handler '{handlerType}' method '{method.Name}' must use ZLinkSpotActorChangeResult as the third parameter.");
        }

        ZLinkHandlerMethodShape.RequireCancellationToken(handlerType, method, parameters[3], "SPOT actor lifecycle handler");
        ZLinkHandlerMethodShape.RequireNoReply(handlerType, method, "SPOT actor lifecycle handler");
        ZLinkSpotActorDescriptorBuilder.ValidateSpotType(handlerType, expectedSpotType, spotType);
        ZLinkSpotActorDescriptorBuilder.ValidateActorType(handlerType, expectedActorType, actorType);
        var descriptor = ZLinkSpotActorDescriptorBuilder.CreateLifecycle(
            surface,
            handlerType,
            spotType,
            actorType,
            ZLinkHandlerMethodInvokerFactory.Create(method));
        return postJoined is not null
            ? new ZLinkSpotActorInferredHandlerDescriptor { Joined = descriptor }
            : new ZLinkSpotActorInferredHandlerDescriptor { Left = descriptor };
    }

    private static ZLinkSpotActorLifecycleDescriptor? TryCreateDisconnected(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type? expectedActorType,
        MethodInfo method)
    {
        if (method.GetCustomAttribute<ZLinkSpotActorDisconnectedAttribute>() is null)
        {
            return null;
        }

        var parameters = ZLinkHandlerMethodShape.RequireParameterCount(handlerType, method, 3, "SPOT actor disconnected handler");
        var spotType = parameters[0].ParameterType;
        var actorType = parameters[1].ParameterType;
        ZLinkHandlerMethodShape.RequireCancellationToken(handlerType, method, parameters[2], "SPOT actor disconnected handler");
        ZLinkHandlerMethodShape.RequireNoReply(handlerType, method, "SPOT actor disconnected handler");
        ZLinkSpotActorDescriptorBuilder.ValidateSpotType(handlerType, expectedSpotType, spotType);
        ZLinkSpotActorDescriptorBuilder.ValidateActorType(handlerType, expectedActorType, actorType);
        return ZLinkSpotActorDescriptorBuilder.CreateLifecycle(
            surface,
            handlerType,
            spotType,
            actorType,
            ZLinkHandlerMethodInvokerFactory.Create(method));
    }

    private static IEnumerable<MethodInfo> EnumeratePacketMethods(Type handlerType)
    {
        return handlerType
            .GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .Where(static method =>
                method.GetCustomAttribute<ZLinkSpotActorSendAttribute>() is not null
                || method.GetCustomAttribute<ZLinkSpotActorRequestAttribute>() is not null);
    }

    private static IEnumerable<MethodInfo> EnumerateLifecycleMethods(Type handlerType)
    {
        return handlerType
            .GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .Where(static method =>
                method.GetCustomAttribute<ZLinkSpotPostActorJoinedAttribute>() is not null
                || method.GetCustomAttribute<ZLinkSpotActorLeftAttribute>() is not null);
    }

    private static IEnumerable<MethodInfo> EnumerateDisconnectedMethods(Type handlerType)
    {
        return handlerType
            .GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .Where(static method =>
                method.GetCustomAttribute<ZLinkSpotActorDisconnectedAttribute>() is not null);
    }
}
