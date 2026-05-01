namespace Zlink.Framework.Channels;

public interface IZLinkRoutedClient
{
    IZLinkRoutedSendCall SendTo<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRoutedRequestCall RequestTo<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request);
}

public interface IZLinkRoutedSendCall
{
    IZLinkRoutedSendCall WithPacketName(string packetName);

    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZLinkRoutedRequestCall
{
    IZLinkRoutedRequestCall WithPacketName(string packetName);

    IZLinkRoutedRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkRoutedSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkRoutedSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkRoutedRequestHandler<in TRequest, TReply>
{
    ValueTask<TReply> HandleAsync(
        TRequest request,
        ZLinkRoutedRequestContext context,
        CancellationToken cancellationToken);
}

public sealed class ZLinkRoutedSendContext : ZLinkHandlerContext
{
    public ZLinkRoutedSendContext(
        string routerChannelId,
        RoutingId sourceNodeRid,
        string? packetName,
        string? contentType,
        string? correlationId,
        IServiceProvider services,
        CancellationToken connectionAborted)
        : base(routerChannelId, packetName, contentType, correlationId, null, services, connectionAborted)
    {
        RouterChannelId = routerChannelId;
        SourceNodeRid = sourceNodeRid;
    }

    public string RouterChannelId { get; }

    public RoutingId SourceNodeRid { get; }
}

public sealed class ZLinkRoutedRequestContext : ZLinkHandlerContext
{
    public ZLinkRoutedRequestContext(
        string routerChannelId,
        RoutingId sourceNodeRid,
        string? packetName,
        string? contentType,
        string? correlationId,
        DateTimeOffset? deadline,
        IServiceProvider services,
        CancellationToken connectionAborted)
        : base(routerChannelId, packetName, contentType, correlationId, deadline, services, connectionAborted)
    {
        RouterChannelId = routerChannelId;
        SourceNodeRid = sourceNodeRid;
    }

    public string RouterChannelId { get; }

    public RoutingId SourceNodeRid { get; }
}
