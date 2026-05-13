using System.Reflection;
using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Spots;

internal enum ZLinkSpotActorHandlerSurface
{
    EntrySpot,
    UserSpot
}

internal sealed class ZLinkSpotActorPacketDescriptor
{
    public required Type HandlerType { get; init; }

    public Type? SpotType { get; init; }

    public required Type ActorType { get; init; }

    public required Type MessageType { get; init; }

    public Type? ReplyType { get; init; }

    public required ZLinkMessageKind Kind { get; init; }

    public required MethodInfo HandleMethod { get; init; }

    public required string MessageName { get; init; }

    public required ZLinkSpotActorHandlerSurface Surface { get; init; }
}

internal sealed class ZLinkSpotActorLifecycleDescriptor
{
    public required Type HandlerType { get; init; }

    public Type? SpotType { get; init; }

    public required Type ActorType { get; init; }

    public required MethodInfo HandleMethod { get; init; }

    public required ZLinkSpotActorHandlerSurface Surface { get; init; }
}

internal sealed class ZLinkSpotActorHandlerRegistry
{
    private readonly ZLinkSpotActorHandlerSurface _surface;
    private readonly Type? _expectedSpotType;
    private readonly Dictionary<(ZLinkMessageKind Kind, Type ActorType, string Name), ZLinkSpotActorPacketDescriptor> _packets = [];
    private readonly Dictionary<Type, ZLinkSpotActorLifecycleDescriptor> _joined = [];
    private readonly Dictionary<Type, ZLinkSpotActorLifecycleDescriptor> _left = [];
    private bool _bound;

    public ZLinkSpotActorHandlerRegistry(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType = null)
    {
        _surface = surface;
        _expectedSpotType = expectedSpotType;
    }

    public void AddPacket(Type handlerType, Type actorType, string? packetName)
    {
        EnsureNotBound();
        var descriptor = ZLinkSpotActorHandlerDescriptorFactory.CreatePacket(
            _surface,
            _expectedSpotType,
            handlerType,
            actorType,
            packetName);
        var key = (descriptor.Kind, descriptor.ActorType, descriptor.MessageName);
        if (_packets.TryGetValue(key, out var existing)
            && existing.HandlerType == descriptor.HandlerType
            && existing.MessageType == descriptor.MessageType)
        {
            return;
        }

        if (existing is not null)
        {
            throw new InvalidOperationException(
                $"Actor packet '{descriptor.MessageName}' for '{descriptor.ActorType}' is already registered.");
        }

        _packets.Add(key, descriptor);
    }

    public void AddJoined(Type handlerType, Type actorType)
    {
        AddLifecycle(_joined, handlerType, actorType, joined: true);
    }

    public void AddLeft(Type handlerType, Type actorType)
    {
        AddLifecycle(_left, handlerType, actorType, joined: false);
    }

    public void Bind()
    {
        _bound = true;
    }

    public bool TryResolve(
        Type actorType,
        ZlinkStreamHeader header,
        out ZLinkSpotActorPacketDescriptor? descriptor)
    {
        var kind = header.Kind == ZlinkStreamMessageKind.Request
            ? ZLinkMessageKind.Request
            : ZLinkMessageKind.Command;

        if (_packets.TryGetValue((kind, actorType, header.Name), out descriptor))
        {
            return true;
        }

        if (kind == ZLinkMessageKind.Request
            && _packets.TryGetValue((ZLinkMessageKind.Command, actorType, header.Name), out descriptor))
        {
            return true;
        }

        foreach (var candidate in _packets)
        {
            if (candidate.Key.Kind == kind
                && candidate.Key.Name == header.Name
                && candidate.Key.ActorType.IsAssignableFrom(actorType))
            {
                descriptor = candidate.Value;
                return true;
            }
        }

        descriptor = null;
        return false;
    }

    public bool TryResolveJoined(Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        return TryResolveLifecycle(_joined, actorType, out descriptor);
    }

    public bool TryResolveLeft(Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        return TryResolveLifecycle(_left, actorType, out descriptor);
    }

    private void AddLifecycle(
        Dictionary<Type, ZLinkSpotActorLifecycleDescriptor> target,
        Type handlerType,
        Type actorType,
        bool joined)
    {
        EnsureNotBound();
        var descriptor = ZLinkSpotActorHandlerDescriptorFactory.CreateLifecycle(
            _surface,
            _expectedSpotType,
            handlerType,
            actorType,
            joined);
        if (target.TryGetValue(descriptor.ActorType, out var existing)
            && existing.HandlerType == descriptor.HandlerType)
        {
            return;
        }

        if (existing is not null)
        {
            throw new InvalidOperationException(
                $"Actor lifecycle handler for '{descriptor.ActorType}' is already registered.");
        }

        target.Add(descriptor.ActorType, descriptor);
    }

    private static bool TryResolveLifecycle(
        Dictionary<Type, ZLinkSpotActorLifecycleDescriptor> source,
        Type actorType,
        out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        if (source.TryGetValue(actorType, out descriptor))
        {
            return true;
        }

        foreach (var candidate in source)
        {
            if (candidate.Key.IsAssignableFrom(actorType))
            {
                descriptor = candidate.Value;
                return true;
            }
        }

        descriptor = null;
        return false;
    }

    private void EnsureNotBound()
    {
        if (_bound)
        {
            throw new InvalidOperationException(
                "Actor handler registration is only allowed while Configure is running.");
        }
    }
}

internal static class ZLinkSpotActorHandlerDescriptorFactory
{
    public static ZLinkSpotActorPacketDescriptor CreatePacket(
        ZLinkSpotActorHandlerSurface surface,
        Type? expectedSpotType,
        Type handlerType,
        Type expectedActorType,
        string? packetName)
    {
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType)
            {
                continue;
            }

            var definition = implemented.GetGenericTypeDefinition();
            var arguments = implemented.GetGenericArguments();
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
        var expectedDefinition = surface switch
        {
            ZLinkSpotActorHandlerSurface.EntrySpot when joined => typeof(IZLinkEntrySpotActorJoinedHandler<>),
            ZLinkSpotActorHandlerSurface.EntrySpot => typeof(IZLinkEntrySpotActorLeftHandler<>),
            ZLinkSpotActorHandlerSurface.UserSpot when joined => typeof(IZLinkSpotActorJoinedHandler<,>),
            _ => typeof(IZLinkSpotActorLeftHandler<,>)
        };

        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != expectedDefinition)
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            if (surface == ZLinkSpotActorHandlerSurface.EntrySpot)
            {
                ValidateActorType(handlerType, expectedActorType, arguments[0]);
                return new ZLinkSpotActorLifecycleDescriptor
                {
                    HandlerType = handlerType,
                    ActorType = arguments[0],
                    HandleMethod = FindHandleMethod(handlerType),
                    Surface = surface
                };
            }

            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            ValidateActorType(handlerType, expectedActorType, arguments[1]);
            return new ZLinkSpotActorLifecycleDescriptor
            {
                HandlerType = handlerType,
                SpotType = arguments[0],
                ActorType = arguments[1],
                HandleMethod = FindHandleMethod(handlerType),
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
            HandleMethod = FindHandleMethod(handlerType),
            MessageName = packetName ?? ZLinkMessageNameResolver.ResolveFromType(messageType),
            Surface = surface
        };
    }

    private static MethodInfo FindHandleMethod(Type handlerType)
    {
        return handlerType.GetMethod("HandleAsync", BindingFlags.Instance | BindingFlags.Public)
            ?? throw new InvalidOperationException($"Handler '{handlerType}' does not expose HandleAsync.");
    }

    private static void ValidateSpotType(Type handlerType, Type? expectedSpotType, Type actualSpotType)
    {
        if (expectedSpotType is null || actualSpotType != expectedSpotType)
        {
            throw new InvalidOperationException(
                $"SPOT actor handler '{handlerType}' targets SPOT '{actualSpotType}', but registration expects '{expectedSpotType}'.");
        }
    }

    private static void ValidateActorType(Type handlerType, Type expectedActorType, Type actualActorType)
    {
        if (!actualActorType.IsAssignableFrom(expectedActorType))
        {
            throw new InvalidOperationException(
                $"SPOT actor handler '{handlerType}' targets actor '{actualActorType}', but registration expects '{expectedActorType}'.");
        }
    }
}
