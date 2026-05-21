namespace Zlink.Framework.Contracts.Handlers;

public interface IZLinkHandlerContext
{
    string? ChannelName { get; }

    string? PacketName { get; }

    string? ContentType { get; }

    string? CorrelationId { get; }

    DateTimeOffset? Deadline { get; }

    CancellationToken ConnectionAborted { get; }
}

public abstract class ZLinkHandlerContext(
    string? channelName,
    string? packetName,
    string? contentType,
    string? correlationId,
    DateTimeOffset? deadline,
    IServiceProvider services,
    CancellationToken connectionAborted)
    : IZLinkHandlerContext
{
    public string? ChannelName { get; } = channelName;

    public string? PacketName { get; } = packetName;

    public string? ContentType { get; } = contentType;

    public string? CorrelationId { get; } = correlationId;

    public DateTimeOffset? Deadline { get; } = deadline;

    internal IServiceProvider Services { get; } = services;

    public CancellationToken ConnectionAborted { get; } = connectionAborted;
}

public sealed class ZLinkRequestContext : ZLinkHandlerContext
{
    internal ZLinkRequestContext(
        string? channelName,
        string? packetName,
        string? contentType,
        string? correlationId,
        DateTimeOffset? deadline,
        IServiceProvider services,
        CancellationToken connectionAborted)
        : base(channelName, packetName, contentType, correlationId, deadline, services, connectionAborted)
    {
    }
}

public sealed class ZLinkSendContext : ZLinkHandlerContext
{
    internal ZLinkSendContext(
        string? channelName,
        string? packetName,
        string? contentType,
        string? correlationId,
        DateTimeOffset? deadline,
        IServiceProvider services,
        CancellationToken connectionAborted)
        : base(channelName, packetName, contentType, correlationId, deadline, services, connectionAborted)
    {
    }
}

public sealed class ZLinkPublishContext : ZLinkHandlerContext
{
    internal ZLinkPublishContext(
        string? channelName,
        string? packetName,
        string? contentType,
        string? correlationId,
        DateTimeOffset? deadline,
        string topic,
        string? source,
        IServiceProvider services,
        CancellationToken connectionAborted)
        : base(channelName, packetName, contentType, correlationId, deadline, services, connectionAborted)
    {
        Topic = topic;
        Source = source;
    }

    public string Topic { get; }

    public string? Source { get; }
}
