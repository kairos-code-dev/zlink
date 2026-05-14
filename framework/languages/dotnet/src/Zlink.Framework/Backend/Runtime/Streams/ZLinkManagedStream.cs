using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;
using System.Threading;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkManagedStream : IZLinkStream
{
    private readonly IZLinkBackendStreamSocket _socket;
    private readonly RoutingId _routingId;

    public ZLinkManagedStream(
        IZLinkBackendStreamSocket socket,
        RoutingId routingId)
    {
        _socket = socket;
        _routingId = routingId;
        SessionId = _routingId.ToHex();
    }

    public string SessionId { get; }

    public RoutingId? RoutingId => _routingId;

    public string? LocalAddr { get; private set; }

    public string? RemoteAddr { get; private set; }

    public bool Write(
        Message payload,
        SendFlags flags = SendFlags.None)
    {
        return _socket.Send(_routingId, payload, flags);
    }

    public bool Write(
        Message header,
        Message body,
        SendFlags flags = SendFlags.None)
    {
        return _socket.Send(_routingId, [header, body], flags);
    }

    public ValueTask CloseAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        _socket.DisconnectPeer(_routingId);
        return ValueTask.CompletedTask;
    }

    public void UpdateAddresses(string? localAddr, string? remoteAddr)
    {
        LocalAddr = localAddr;
        RemoteAddr = remoteAddr;
    }

    internal async ValueTask BindActorAsync(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        await _socket.BindActorAsync(_routingId, actor, timeout, cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask UnbindActorAsync(
        string actorId,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        await _socket.UnbindActorAsync(_routingId, actorId, timeout, cancellationToken)
            .ConfigureAwait(false);
    }

    internal bool SendBoundActor(
        string actorId,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        return _socket.SendBoundActor(_routingId, actorId, parts, flags);
    }
}
