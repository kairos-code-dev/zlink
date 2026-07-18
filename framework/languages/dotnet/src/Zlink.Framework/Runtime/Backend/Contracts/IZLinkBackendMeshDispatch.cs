namespace Zlink.Framework.Runtime.Backend.Contracts;

// Framework-owned dispatch records (RouteMesh 10.0.0 Option B).
//
// In 9.x the framework drove the route/subscribe/reply plane with the bindings'
// Received/TopicMessage raw-socket receive types. 10.0.0 delivers pull dispatch
// via MeshReceiveRecord and does not expose a way to populate a Received from a
// record. Per the framework spec the framework itself drains claims to typed
// handlers and holds the reply token, route envelope and Core claim. These
// framework-owned records carry the retained message parts and a reply callback
// bound to the record's held reply token, so consumers reply without the binding
// Received type.

/// <summary>
///     A framework-owned inbound route/spot/channel message. Mirrors the subset
///     of the binding <c>Received</c> surface the route dispatch plane consumes
///     (parts, spot rid, request seq) and carries a reply delegate bound to the
///     originating pull-dispatch reply token.
/// </summary>
internal sealed class ZLinkBackendRouteReceived : IDisposable
{
    private readonly Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? _reply;
    private bool _disposed;

    public ZLinkBackendRouteReceived(
        IReadOnlyList<Message> parts,
        RoutingId? sourceNodeRid,
        RoutingId? spotRid,
        ulong? requestSeq,
        Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? reply)
    {
        Parts = parts;
        SourceNodeRid = sourceNodeRid;
        SpotRid = spotRid;
        RequestSeq = requestSeq;
        _reply = reply;
    }

    public IReadOnlyList<Message> Parts { get; }

    public RoutingId? SourceNodeRid { get; }

    public RoutingId? SpotRid { get; }

    public ulong? RequestSeq { get; }

    public bool CanReply => _reply is not null;

    public SubmitResult Reply(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        if (_reply is null)
            throw new InvalidOperationException(
                "This route record does not carry a reply token.");
        return _reply(parts, flags);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        ZLinkMessageParts.DisposeAll(Parts);
    }
}

/// <summary>
///     A framework-owned inbound subscription (logical multicast) message.
///     Mirrors the subset of the binding <c>TopicMessage</c> surface the
///     subscription drain consumes (topic, parts).
/// </summary>
internal sealed class ZLinkBackendSubscribeMessage : IDisposable
{
    private bool _disposed;

    public ZLinkBackendSubscribeMessage(string topic, IReadOnlyList<Message> parts)
    {
        Topic = topic;
        Parts = parts;
    }

    public string Topic { get; }

    public IReadOnlyList<Message> Parts { get; }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        ZLinkMessageParts.DisposeAll(Parts);
    }
}
