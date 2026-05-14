namespace Zlink.Framework.Contracts.Handlers;

[AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]
public abstract class ZLinkHandlerGroupAttribute(string groupName) : Attribute
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

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotSubscriptionAttribute(
    string spotNodeName,
    string topic) : Attribute
{
    public string SpotNodeName { get; } = spotNodeName;

    public string Topic { get; } = topic;
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkStreamPacketAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkStreamRawAttribute : Attribute
{
}
