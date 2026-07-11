namespace Zlink.Framework.Runtime.Spots;

internal sealed record ZLinkSpotPacketRegistration(
    Type HandlerType,
    string? PacketName,
    System.Reflection.MethodInfo? Method = null);

internal sealed record ZLinkSpotSubscriptionRegistration(
    string Topic,
    Type HandlerType,
    System.Reflection.MethodInfo? Method = null);
