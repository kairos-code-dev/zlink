using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend;
using System.Reflection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotDescriptor
{
    public required Type HandlerType { get; init; }

    public required Type SpotType { get; init; }

    public required Type MessageType { get; init; }

    public required MethodInfo HandleMethod { get; init; }

    public required string PacketName { get; init; }

    public Type? ReplyType { get; init; }

    public bool IsRequest => ReplyType is not null;
}

internal sealed class ZLinkSpotSubscriptionDescriptor
{
    public required string Topic { get; init; }

    public required Type HandlerType { get; init; }

    public required Type SpotType { get; init; }

    public required Type MessageType { get; init; }

    public required MethodInfo HandleMethod { get; init; }

    public required string PacketName { get; init; }
}

internal sealed class ZLinkSpotTimerDescriptor
{
    public required string Name { get; init; }

    public required TimeSpan Period { get; init; }

    public required Type HandlerType { get; init; }

    public required Type SpotType { get; init; }

    public required MethodInfo HandleMethod { get; init; }
}

internal sealed class ZLinkSpotActorJoinDescriptor
{
    public required Type HandlerType { get; init; }

    public required Type SpotType { get; init; }

    public required Type ActorType { get; init; }

    public required Type RequestType { get; init; }

    public required Type ReplyType { get; init; }

    public required MethodInfo HandleMethod { get; init; }

    public required string PacketName { get; init; }
}
