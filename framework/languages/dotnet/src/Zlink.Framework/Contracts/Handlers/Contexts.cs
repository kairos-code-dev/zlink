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

public abstract class ZLinkHandlerContext : IZLinkHandlerContext
{
    protected ZLinkHandlerContext(
        string? channelName,
        string? packetName,
        string? contentType,
        string? correlationId,
        DateTimeOffset? deadline,
        IServiceProvider services,
        CancellationToken connectionAborted)
    {
        ChannelName = channelName;
        PacketName = packetName;
        ContentType = contentType;
        CorrelationId = correlationId;
        Deadline = deadline;
        Services = services;
        ConnectionAborted = connectionAborted;
    }

    public string? ChannelName { get; }

    public string? PacketName { get; }

    public string? ContentType { get; }

    public string? CorrelationId { get; }

    public DateTimeOffset? Deadline { get; }

    internal IServiceProvider Services { get; }

    public CancellationToken ConnectionAborted { get; }
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
