using Zlink.Framework.Runtime.Backend.DotNet.Mappings;

namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

// RouteMesh 10.0.0 STREAM seam. Raw framed I/O stays on IStreamSocket; the
// bound-actor plane (bind/unbind/relay) moved onto IStreamSessionService, created
// from the owning MeshNode. Actor identity is resolved to a full ActorRef via the
// session bindings table so the framework seam can keep its actor-id-keyed shape.
internal sealed class ZLinkBackendStreamSocketWrapper : IZLinkBackendStreamSocket
{
    private readonly IStreamSocket _socket;
    private readonly IMeshNode _node;
    private readonly object _sendGate = new();
    private readonly object _sessionGate = new();
    private IStreamSessionService? _session;
    private bool _sessionStarted;

    public ZLinkBackendStreamSocketWrapper(IStreamSocket socket, IMeshNode node)
    {
        _socket = socket;
        _node = node;
    }

    internal IStreamSocket NativeSocket => _socket;

    private IStreamSessionService Session()
    {
        if (_session is { } existing) return existing;
        lock (_sessionGate)
        {
            if (_session is null)
            {
                _session = _node.CreateStreamSessionService(_socket);
                _session.Start();
                _sessionStarted = true;
            }

            return _session;
        }
    }

    public void Bind(string endpoint)
    {
        _socket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        _socket.SetChannelName(channelName);
    }

    public void SetTlsServer(string certPath, string keyPath, bool requireClientCert)
    {
        _socket.SetTlsServer(certPath, keyPath, requireClientCert);
    }

    public void OnFramedPacket(Action<RoutingId, Message, Message> handler)
    {
        _socket.OnPacket((routingId, header, body) => handler(routingId, header, body));
    }

    public bool Send(RoutingId routingId, Message payload, SendFlags flags)
    {
        lock (_sendGate)
            return _socket.Send(routingId).Message(payload).Flags(flags).Submit();
    }

    public bool Send(RoutingId routingId, IReadOnlyList<Message> parts, SendFlags flags)
    {
        lock (_sendGate)
            return _socket.Send(routingId).Messages(parts).Flags(flags).Submit();
    }

    public void DisconnectPeer(RoutingId routingId)
    {
        _socket.DisconnectRid(routingId);
    }

    public ValueTask BindActorAsync(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        Session().BindActor(sessionRid, actor.ToNative(), out _, timeout);
        return ValueTask.CompletedTask;
    }

    public ValueTask UnbindActorAsync(
        RoutingId sessionRid,
        string actorId,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var session = Session();
        foreach (var binding in session.Bindings(sessionRid))
            if (string.Equals(binding.Actor.ActorId, actorId, StringComparison.Ordinal))
            {
                session.UnbindActor(
                    sessionRid, binding.Actor, binding.BindingGeneration, out _, timeout);
                break;
            }

        return ValueTask.CompletedTask;
    }

    public bool SendBoundActor(
        RoutingId sessionRid,
        string actorId,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        var session = Session();
        foreach (var binding in session.Bindings(sessionRid))
            if (string.Equals(binding.Actor.ActorId, actorId, StringComparison.Ordinal))
                lock (_sendGate)
                    return session.SendToActor(sessionRid, binding.Actor, parts, flags)
                        == SubmitResult.Ok;

        return false;
    }

    public async ValueTask DisposeAsync()
    {
        IStreamSessionService? session;
        lock (_sessionGate)
        {
            session = _sessionStarted ? _session : null;
            _session = null;
        }

        if (session is not null)
            await session.DisposeAsync().ConfigureAwait(false);
        await _socket.DisposeAsync().ConfigureAwait(false);
        await _node.DisposeAsync().ConfigureAwait(false);
    }
}
