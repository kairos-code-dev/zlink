namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkRouteClient
{
    IZLinkSendCall SendToNode<TMessage>(
        string meshName,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRequestCall RequestToNode<TRequest>(
        string meshName,
        RoutingId targetNodeRid,
        TRequest request);

    /// <summary>
    /// Sends to one ready positive-weight member of the ChannelName on the
    /// named mesh. Selection and submit are one atomic operation; the selected
    /// RID is not returned and the send is not re-submitted to another member.
    /// </summary>
    IZLinkSendCall SendToChannel<TMessage>(
        string channelName,
        TMessage message);

    /// <summary>
    /// Requests one ready positive-weight member of the ChannelName on the
    /// named mesh. Selection and submit are one atomic operation; a timeout or
    /// disconnect after submit is not retried on another member because the
    /// request may already have executed.
    /// </summary>
    IZLinkRequestCall RequestToChannel<TRequest>(
        string channelName,
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

public sealed class ZLinkRouteSendContext : IZLinkHandlerContext
{
    internal ZLinkRouteSendContext(
        string meshName,
        string? channelName,
        RoutingId sourceNodeRid,
        string packetName,
        string? contentType,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
    {
        MeshName = meshName;
        ChannelName = channelName;
        SourceNodeRid = sourceNodeRid;
        PacketName = packetName;
        ContentType = contentType;
        ConnectionAborted = connectionAborted;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
    }

    public string MeshName { get; }

    public string? ChannelName { get; }

    public string PacketName { get; }

    public string? ContentType { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public CancellationToken ConnectionAborted { get; }

    public RoutingId SourceNodeRid { get; }
}

public sealed class ZLinkRouteRequestContext : IZLinkHandlerContext
{
    internal ZLinkRouteRequestContext(
        string meshName,
        string? channelName,
        RoutingId sourceNodeRid,
        string packetName,
        string? contentType,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
    {
        MeshName = meshName;
        ChannelName = channelName;
        SourceNodeRid = sourceNodeRid;
        PacketName = packetName;
        ContentType = contentType;
        ConnectionAborted = connectionAborted;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
    }

    public string MeshName { get; }

    public string? ChannelName { get; }

    public string PacketName { get; }

    public string? ContentType { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public CancellationToken ConnectionAborted { get; }

    public RoutingId SourceNodeRid { get; }
}
