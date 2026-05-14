namespace Zlink.Framework.Runtime.Spots;

internal sealed record ZLinkSpotPacketRegistration(Type HandlerType);

internal sealed record ZLinkSpotSubscriptionRegistration(string Topic, Type HandlerType);

internal sealed record ZLinkSpotActorJoinRegistration(
    Type HandlerType,
    Type ActorType,
    Type RequestType,
    Type ReplyType);
