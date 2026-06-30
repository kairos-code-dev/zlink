namespace Zlink.Framework.Runtime.Spots;

internal sealed record ZLinkSpotPacketRegistration(Type HandlerType, string? PacketName);

internal sealed record ZLinkSpotSubscriptionRegistration(string Topic, Type HandlerType);