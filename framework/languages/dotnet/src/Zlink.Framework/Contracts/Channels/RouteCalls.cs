namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkRouteClient
{
    IZLinkRouteSendCall Send<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRouteRequestCall Request<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request);
}

public interface IZLinkRouteSendCall
{
    IZLinkRouteSendCall PacketName(string packetName);

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkRouteRequestCall
{
    IZLinkRouteRequestCall PacketName(string packetName);

    IZLinkRouteRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default);
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
        CancellationToken connectionAborted)
        : base(routerChannelId, packetName, contentType, connectionAborted)
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
        CancellationToken connectionAborted)
        : base(routerChannelId, packetName, contentType, connectionAborted)
    {
        RouterChannelId = routerChannelId;
        SourceNodeRid = sourceNodeRid;
    }

    public string RouterChannelId { get; }

    public RoutingId SourceNodeRid { get; }
}
