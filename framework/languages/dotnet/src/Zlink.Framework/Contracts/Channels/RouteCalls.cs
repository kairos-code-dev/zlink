namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkRouteClient
{
    IZLinkSendCall Send<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRouteRequestCall Request<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request);

    /// <summary>
    /// Sends to a spot full address over the channel's spot route plane.
    /// The caller resolved the address once and holds it; best-effort like
    /// every spot send (spot-address messaging draft §6, §7).
    /// </summary>
    IZLinkSendCall SendToSpot<TMessage>(
        string routerChannelId,
        ZLinkSpotAddress address,
        TMessage message);

    /// <summary>
    /// Requests against a spot full address over the channel's spot route
    /// plane. A stale address fails with SpotRouteNotFound; re-resolve and
    /// retry per the caller's policy.
    /// </summary>
    IZLinkRouteRequestCall RequestToSpot<TRequest>(
        string routerChannelId,
        ZLinkSpotAddress address,
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