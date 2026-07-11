namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkRouteClient
{
    IZLinkSendCall SendToNode<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRequestCall RequestToNode<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request);

    /// <summary>
    /// Sends through the current address held by the spot handle. The framework
    /// refreshes the handle from location updates, but does not retry a one-way
    /// send because delivery may already have occurred.
    /// </summary>
    IZLinkSendCall SendToSpot<TMessage>(
        SpotHandle target,
        TMessage message);

    /// <summary>
    /// Requests through the current address held by the spot handle. If the
    /// address is invalidated during the request, the framework refreshes the
    /// handle and retries once when doing so cannot duplicate a completed call.
    /// </summary>
    IZLinkRequestCall RequestToSpot<TRequest>(
        SpotHandle target,
        TRequest request);
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

    /// <summary>
    /// The route channel id that delivered the message. This is the same value
    /// exposed through <see cref="IZLinkHandlerContext.ChannelName" />.
    /// </summary>
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

    /// <summary>
    /// The route channel id that delivered the request. This is the same value
    /// exposed through <see cref="IZLinkHandlerContext.ChannelName" />.
    /// </summary>
    public string RouterChannelId { get; }

    public RoutingId SourceNodeRid { get; }
}
