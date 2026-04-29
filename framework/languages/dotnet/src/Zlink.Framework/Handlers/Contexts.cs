namespace Zlink.Framework.Handlers;

public interface IZLinkHandlerContext
{
    string? ChannelName { get; }

    string? MessageName { get; }

    string? ContentType { get; }

    string? CorrelationId { get; }

    DateTimeOffset? Deadline { get; }

    IServiceProvider Services { get; }

    CancellationToken ConnectionAborted { get; }
}

public abstract class ZLinkHandlerContext : IZLinkHandlerContext
{
    protected ZLinkHandlerContext(
        string? channelName,
        string? messageName,
        string? contentType,
        string? correlationId,
        DateTimeOffset? deadline,
        IServiceProvider services,
        CancellationToken connectionAborted)
    {
        ChannelName = channelName;
        MessageName = messageName;
        ContentType = contentType;
        CorrelationId = correlationId;
        Deadline = deadline;
        Services = services;
        ConnectionAborted = connectionAborted;
    }

    public string? ChannelName { get; }

    public string? MessageName { get; }

    public string? ContentType { get; }

    public string? CorrelationId { get; }

    public DateTimeOffset? Deadline { get; }

    public IServiceProvider Services { get; }

    public CancellationToken ConnectionAborted { get; }
}

public sealed class ZLinkRequestContext : ZLinkHandlerContext
{
    public ZLinkRequestContext(
        string? channelName,
        string? messageName,
        string? contentType,
        string? correlationId,
        DateTimeOffset? deadline,
        IServiceProvider services,
        CancellationToken connectionAborted)
        : base(channelName, messageName, contentType, correlationId, deadline, services, connectionAborted)
    {
    }
}

public sealed class ZLinkSendContext : ZLinkHandlerContext
{
    public ZLinkSendContext(
        string? channelName,
        string? messageName,
        string? contentType,
        string? correlationId,
        DateTimeOffset? deadline,
        IServiceProvider services,
        CancellationToken connectionAborted)
        : base(channelName, messageName, contentType, correlationId, deadline, services, connectionAborted)
    {
    }
}

public sealed class ZLinkEventContext : ZLinkHandlerContext
{
    public ZLinkEventContext(
        string? channelName,
        string? messageName,
        string? contentType,
        string? correlationId,
        DateTimeOffset? deadline,
        string topic,
        string? source,
        IServiceProvider services,
        CancellationToken connectionAborted)
        : base(channelName, messageName, contentType, correlationId, deadline, services, connectionAborted)
    {
        Topic = topic;
        Source = source;
    }

    public string Topic { get; }

    public string? Source { get; }
}
