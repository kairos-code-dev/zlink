namespace Zlink.Framework.Contracts.Handlers;

[AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]
public sealed class ZLinkHandlerGroupAttribute(string groupName) : Attribute
{
    public string GroupName { get; } = groupName;
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkRequestAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSendAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkPublishAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct, AllowMultiple = false)]
public sealed class ZLinkPacketAttribute(string packetName) : Attribute
{
    public string PacketName { get; } = packetName;
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotRequestAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false)]
public sealed class ZLinkSpotPacketHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; } = packetName;
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false)]
public sealed class ZLinkSpotRequestHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; } = packetName;
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotSubscriptionAttribute(
    string spotNodeName,
    string topic) : Attribute
{
    public string SpotNodeName { get; } = spotNodeName;

    public string Topic { get; } = topic;
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false)]
public sealed class ZLinkSpotSubscriptionHandlerAttribute(string topic) : Attribute
{
    public string Topic { get; } = topic;
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotActorSendAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false)]
public sealed class ZLinkSpotActorSendHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; } = packetName;
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotActorRequestAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false)]
public sealed class ZLinkSpotActorRequestHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; } = packetName;
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false)]
public sealed class ZLinkSpotTimerHandlerAttribute(string name, double periodMilliseconds) : Attribute
{
    public string Name { get; } = name;

    public double PeriodMilliseconds { get; } = periodMilliseconds;
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkStreamPacketAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkStreamRawAttribute : Attribute
{
}
