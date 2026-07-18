namespace Zlink.Framework.Contracts.Handlers;

using Zlink.Framework.Contracts.Streams;

public interface IZLinkHandlerContext
{
    string? ChannelName { get; }

    string? PacketName { get; }

    string? ContentType { get; }

    /// <summary>
    ///     The immutable application-metadata snapshot the sender attached;
    ///     <see cref="ZLinkMessageMetadata.Empty" /> when none was sent.
    /// </summary>
    ZLinkMessageMetadata Metadata { get; }

    CancellationToken ConnectionAborted { get; }
}

public abstract class ZLinkHandlerContext(
    string? channelName,
    string? packetName,
    string? contentType,
    CancellationToken connectionAborted,
    ZLinkMessageMetadata? metadata = null)
    : IZLinkHandlerContext
{
    public string? ChannelName { get; } = channelName;

    public string? PacketName { get; } = packetName;

    public string? ContentType { get; } = contentType;

    public ZLinkMessageMetadata Metadata { get; } = metadata ?? ZLinkMessageMetadata.Empty;

    public CancellationToken ConnectionAborted { get; } = connectionAborted;
}

public sealed class ZLinkRequestContext : ZLinkHandlerContext
{
    internal ZLinkRequestContext(
        string? channelName,
        string? packetName,
        string? contentType,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
        : base(channelName, packetName, contentType, connectionAborted, metadata)
    {
    }
}

public sealed class ZLinkSendContext : ZLinkHandlerContext
{
    internal ZLinkSendContext(
        string? channelName,
        string? packetName,
        string? contentType,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
        : base(channelName, packetName, contentType, connectionAborted, metadata)
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
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
        : base(channelName, packetName, contentType, connectionAborted, metadata)
    {
        Topic = topic;
        Source = source;
    }

    public string Topic { get; }

    public string? Source { get; }
}
