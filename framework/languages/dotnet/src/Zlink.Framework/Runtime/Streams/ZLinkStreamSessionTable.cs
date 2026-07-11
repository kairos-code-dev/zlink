using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionTable(
    IServiceProvider services,
    IZLinkBackendStreamSocket socket,
    Type? headerSessionType)
{
    private readonly object _gate = new();
    private readonly Queue<(string LocalAddr, string RemoteAddr)> _pendingConnectionMetadata = [];
    private readonly Dictionary<string, ZLinkStreamSessionRuntime> _sessions = [];
    private bool _stopping;

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

    public void RequestStop()
    {
        ZLinkStreamSessionRuntime[] sessions;
        lock (_gate) sessions = _sessions.Values.ToArray();
        foreach (var session in sessions) session.RequestStop();
    }

    public async ValueTask<ZLinkStreamSessionRuntime?> GetOrCreateAsync(RoutingId routingId)
    {
        var sessionId = routingId.ToHex();
        lock (_gate)
        {
            if (_stopping) return null;

            if (_sessions.TryGetValue(sessionId, out var existing))
                return existing;
        }

        var created = await ZLinkStreamSessionRuntime.CreateAsync(
                services,
                socket,
                routingId,
                headerSessionType,
                Remove)
            .ConfigureAwait(false);
        ZLinkStreamSessionRuntime? duplicate = null;
        lock (_gate)
        {
            if (_stopping)
            {
                duplicate = created;
                created = null!;
            }
            else if (_sessions.TryGetValue(sessionId, out var existing))
            {
                duplicate = created;
                created = existing;
            }
            else
            {
                _sessions.Add(sessionId, created);
            }
        }

        if (duplicate is not null) await duplicate.DisposeUncommittedAsync().ConfigureAwait(false);
        return created;
    }

    public void QueueConnectionMetadata(string localAddr, string remoteAddr)
    {
        lock (_gate)
        {
            if (!_stopping) _pendingConnectionMetadata.Enqueue((localAddr, remoteAddr));
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
                metadata = _pendingConnectionMetadata.Dequeue();
        }

        if (metadata is { } value) session.EnqueueConnected(value.LocalAddr, value.RemoteAddr);
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
