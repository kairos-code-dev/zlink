using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionTable(
    IServiceProvider services,
    IZLinkBackendStreamSocket socket,
    Type? headerSessionType,
    IZlinkStreamHeaderCodec headerCodec)
{
    private readonly Dictionary<string, ZLinkStreamSessionRuntime> _sessions = [];
    private readonly Queue<(string LocalAddr, string RemoteAddr)> _pendingConnectionMetadata = [];
    private readonly object _gate = new();
    private bool _stopping;

    public ZLinkStreamSessionRuntime[] Stop()
    {
        lock (_gate)
        {
            _stopping = true;
            var sessions = _sessions.Values.ToArray();
            _sessions.Clear();
            _pendingConnectionMetadata.Clear();
            return sessions;
        }
    }

    public bool IsStopping
    {
        get
        {
            lock (_gate)
            {
                return _stopping;
            }
        }
    }

    public bool TryGetOrCreate(
        RoutingId routingId,
        out ZLinkStreamSessionRuntime session)
    {
        var sessionId = routingId.ToHex();
        lock (_gate)
        {
            if (_stopping)
            {
                session = null!;
                return false;
            }

            if (_sessions.TryGetValue(sessionId, out var existing))
            {
                session = existing;
                return true;
            }

            var created = new ZLinkStreamSessionRuntime(
                services.CreateAsyncScope(),
                socket,
                routingId,
                headerSessionType,
                headerCodec,
                Remove);
            _sessions.Add(sessionId, created);
            session = created;
            return true;
        }
    }

    public void QueueConnectionMetadata(string localAddr, string remoteAddr)
    {
        lock (_gate)
        {
            if (!_stopping)
            {
                _pendingConnectionMetadata.Enqueue((localAddr, remoteAddr));
            }
        }
    }

    public void ApplyPendingConnectionMetadata(ZLinkStreamSessionRuntime session)
    {
        (string LocalAddr, string RemoteAddr)? metadata = null;

        lock (_gate)
        {
            if (session.Stream.LocalAddr is null
                && session.Stream.RemoteAddr is null
                && _pendingConnectionMetadata.Count > 0)
            {
                metadata = _pendingConnectionMetadata.Dequeue();
            }
        }

        if (metadata is { } value)
        {
            session.EnqueueConnected(value.LocalAddr, value.RemoteAddr);
        }
    }

    public bool TryResolveMonitorSession(
        RoutingId? routingId,
        out ZLinkStreamSessionRuntime session)
    {
        lock (_gate)
        {
            if (routingId is RoutingId streamRoutingId)
            {
                var sessionId = streamRoutingId.ToHex();
                if (_sessions.TryGetValue(sessionId, out var existing))
                {
                    session = existing;
                    return true;
                }
            }

            if (_sessions.Count == 1)
            {
                session = _sessions.Values.First();
                return true;
            }
        }

        session = null!;
        return false;
    }

    private void Remove(string sessionId)
    {
        lock (_gate)
        {
            _sessions.Remove(sessionId);
        }
    }
}
