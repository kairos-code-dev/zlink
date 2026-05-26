namespace Zlink.Framework.Contracts.Handlers;

public interface IZLinkHandlerContext
{
    string? ChannelName { get; }

    string? PacketName { get; }

    string? ContentType { get; }

    CancellationToken ConnectionAborted { get; }
}

public abstract class ZLinkHandlerContext(
    string? channelName,
    string? packetName,
    string? contentType,
    CancellationToken connectionAborted)
    : IZLinkHandlerContext
{
    public string? ChannelName { get; } = channelName;

    public string? PacketName { get; } = packetName;

    public string? ContentType { get; } = contentType;

    public CancellationToken ConnectionAborted { get; } = connectionAborted;
}

public sealed class ZLinkRequestContext : ZLinkHandlerContext
{
    internal ZLinkRequestContext(
        string? channelName,
        string? packetName,
        string? contentType,
        CancellationToken connectionAborted)
        : base(channelName, packetName, contentType, connectionAborted)
    {
    }
}

public sealed class ZLinkSendContext : ZLinkHandlerContext
{
    internal ZLinkSendContext(
        string? channelName,
        string? packetName,
        string? contentType,
        CancellationToken connectionAborted)
        : base(channelName, packetName, contentType, connectionAborted)
    {
    }
}

public sealed class ZLinkPublishContext : ZLinkHandlerContext
{
    internal ZLinkPublishContext(
        string? channelName,
        string? packetName,
        string? contentType,
        string topic,
        string? source,
        CancellationToken connectionAborted)
        : base(channelName, packetName, contentType, connectionAborted)
    {
        Topic = topic;
        Source = source;
    }

    public string Topic { get; }

    public string? Source { get; }
}
