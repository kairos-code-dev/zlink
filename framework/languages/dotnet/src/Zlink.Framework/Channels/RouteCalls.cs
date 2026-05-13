namespace Zlink.Framework.Channels;

public interface IZLinkRouteClient
{
    IZLinkRouteSendCall SendTo<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRouteRequestCall RequestTo<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request);
}

public interface IZLinkRouteSendCall
{
    IZLinkRouteSendCall WithPacketName(string packetName);

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkRouteRequestCall
{
    IZLinkRouteRequestCall WithPacketName(string packetName);

    IZLinkRouteRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Submit<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkRouteSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkRouteSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkRouteRequestHandler<in TRequest, TReply>
{
    ValueTask<TReply> HandleAsync(
        TRequest request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken);
}

public sealed class ZLinkRouteSendContext : ZLinkHandlerContext
{
    internal ZLinkRouteSendContext(
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

public sealed class ZLinkRouteRequestContext : ZLinkHandlerContext
{
    internal ZLinkRouteRequestContext(
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
