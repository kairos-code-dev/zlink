using System.Reflection;

namespace Zlink.Framework.Runtime.Spots;

internal enum ZLinkScannedSpotHandlerKind
{
    Packet,
    Subscription,
    ActorSend,
    ActorRequest,
    Timer
}

internal sealed record ZLinkScannedSpotHandler(
    ZLinkScannedSpotHandlerKind Kind,
    Type HandlerType,
    Type SpotType,
    Type? ActorType = null,
    string? PacketName = null,
    string? Topic = null,
    string? TimerName = null,
    TimeSpan TimerPeriod = default);

internal static class ZLinkScannedSpotHandlerScanner
{
    public static IReadOnlyList<ZLinkScannedSpotHandler> Scan(Assembly assembly)
    {
        var handlers = new List<ZLinkScannedSpotHandler>();
        foreach (var type in assembly.GetTypes())
        {
            if (type.IsAbstract || type.IsInterface) continue;

            handlers.AddRange(ScanType(type));
        }

        return handlers;
    }

    private static IEnumerable<ZLinkScannedSpotHandler> ScanType(Type handlerType)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
            if (definition == typeof(IZLinkSpotPacketHandler<,>)
                || definition == typeof(IZLinkSpotRequestHandler<,,>))
            {
                yield return new ZLinkScannedSpotHandler(
                    ZLinkScannedSpotHandlerKind.Packet,
                    handlerType,
                    arguments[0],
                    PacketName: ResolvePacketName(handlerType));
            }
            else if (definition == typeof(IZLinkSpotSubscriptionHandler<,>))
            {
                if (handlerType.GetCustomAttribute<ZLinkSpotSubscriptionHandlerAttribute>() is { } subscription)
                    yield return new ZLinkScannedSpotHandler(
                        ZLinkScannedSpotHandlerKind.Subscription,
                        handlerType,
                        arguments[0],
                        Topic: subscription.Topic);
            }
            else if (definition == typeof(IZLinkSpotTimerHandler<>))
            {
                if (handlerType.GetCustomAttribute<ZLinkSpotTimerHandlerAttribute>() is { } timer)
                    yield return new ZLinkScannedSpotHandler(
                        ZLinkScannedSpotHandlerKind.Timer,
                        handlerType,
                        arguments[0],
                        TimerName: timer.Name,
                        TimerPeriod: TimeSpan.FromMilliseconds(timer.PeriodMilliseconds));
            }
            else if (definition == typeof(IZLinkEntrySpotActorSendHandler<,,>)
                     || definition == typeof(IZLinkSpotActorSendHandler<,,>))
            {
                if (handlerType.GetCustomAttribute<ZLinkSpotActorSendHandlerAttribute>() is { } actorSend)
                    yield return new ZLinkScannedSpotHandler(
                        ZLinkScannedSpotHandlerKind.ActorSend,
                        handlerType,
                        arguments[0],
                        arguments[1],
                        actorSend.PacketName);
            }
            else if (definition == typeof(IZLinkEntrySpotActorRequestHandler<,,,>)
                     || definition == typeof(IZLinkSpotActorRequestHandler<,,,>))
            {
                if (handlerType.GetCustomAttribute<ZLinkSpotActorRequestHandlerAttribute>() is { } actorRequest)
                    yield return new ZLinkScannedSpotHandler(
                        ZLinkScannedSpotHandlerKind.ActorRequest,
                        handlerType,
                        arguments[0],
                        arguments[1],
                        actorRequest.PacketName);
            }
    }

    private static string? ResolvePacketName(Type handlerType)
    {
        if (handlerType.GetCustomAttribute<ZLinkSpotPacketHandlerAttribute>() is { } packet) return packet.PacketName;

        if (handlerType.GetCustomAttribute<ZLinkSpotRequestHandlerAttribute>() is { } request)
            return request.PacketName;

        return null;
    }
}