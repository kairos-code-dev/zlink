using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionTable(
    IServiceProvider services,
    IZLinkBackendStreamSocket socket,
    Type? headerSessionType,
    ZLinkDrainAdmissionGate drainAdmission,
    string transport,
    TimeProvider timeProvider,
    bool actorDispatchEnabled,
    ZLinkAsyncSubmitter sendSubmitter)
{
    private readonly object _gate = new();
    private readonly Queue<(string LocalAddr, string RemoteAddr)> _pendingConnectionMetadata = [];
    private readonly Queue<string> _unaddressedMonitorSessions = [];
    private readonly Dictionary<string, ZLinkStreamSessionRuntime> _sessions = [];
    private bool _rejectNewSessions;
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

    public int Count
    {
        get
        {
            lock (_gate) return _sessions.Count;
        }
    }

    public ZLinkStreamSessionRuntime[] Snapshot()
    {
        lock (_gate) return _sessions.Values.ToArray();
    }

    public ZLinkStreamSessionRuntime[] Stop()
    {
        lock (_gate)
        {
            _stopping = true;
            var sessions = _sessions.Values.ToArray();
            _sessions.Clear();
            _pendingConnectionMetadata.Clear();
            _unaddressedMonitorSessions.Clear();
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
        var reject = false;
        lock (_gate)
        {
            if (_stopping) return null;

            if (_sessions.TryGetValue(sessionId, out var existing))
                return existing;
            reject = _rejectNewSessions || drainAdmission.IsDraining;
        }
        if (reject)
        {
            RejectNewSession(routingId);
            return null;
        }

        var created = await ZLinkStreamSessionRuntime.CreateAsync(
                services,
                socket,
                routingId,
                headerSessionType,
                Remove,
                transport,
                timeProvider,
                actorDispatchEnabled,
                sendSubmitter)
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
            else if (_rejectNewSessions || drainAdmission.IsDraining)
            {
                duplicate = created;
                created = null!;
                reject = true;
            }
            else
            {
                _sessions.Add(sessionId, created);
            }
        }

        if (duplicate is not null) await duplicate.DisposeUncommittedAsync().ConfigureAwait(false);
        if (reject) RejectNewSession(routingId);
        return created;
    }

    public async ValueTask<bool> DrainSessionsAsync(CancellationToken cancellationToken)
    {
        ZLinkStreamSessionRuntime[] sessions;
        lock (_gate)
        {
            _rejectNewSessions = true;
            sessions = _sessions.Values.ToArray();
        }

        var allClosed = true;
        const int maximumConcurrency = 16;
        for (var offset = 0; offset < sessions.Length; offset += maximumConcurrency)
        {
            var count = Math.Min(maximumConcurrency, sessions.Length - offset);
            var closes = new Task<bool>[count];
            for (var index = 0; index < count; index++)
                closes[index] = sessions[offset + index]
                    .CloseForDrainAsync(cancellationToken)
                    .AsTask();
            var results = await Task.WhenAll(closes).ConfigureAwait(false);
            foreach (var closed in results) allClosed &= closed;
        }
        return allClosed;
    }

    public void ForceStopSessions()
    {
        ZLinkStreamSessionRuntime[] sessions;
        lock (_gate)
        {
            _rejectNewSessions = true;
            sessions = _sessions.Values.ToArray();
        }

        foreach (var session in sessions) session.RequestForceStopForDrain();
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
            {
                metadata = _pendingConnectionMetadata.Dequeue();
                _unaddressedMonitorSessions.Enqueue(session.Stream.SessionId);
            }
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

            if (_unaddressedMonitorSessions.Count > 0)
            {
                // Some STREAM transports do not attach a routing id to monitor events. Match
                // those disconnects to the same FIFO order used when their connection metadata
                // was assigned. A removed id is an intentional tombstone: consuming it without
                // falling through prevents a late disconnect from terminating a newer session.
                var sessionId = _unaddressedMonitorSessions.Dequeue();
                if (_sessions.TryGetValue(sessionId, out var existing))
                {
                    session = existing;
                    return true;
                }
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

    private void RejectNewSession(RoutingId routingId)
    {
        try
        {
            var payload = ZLinkStreamSessionClosingCodec.EncodeServerDrain();
            ZLinkStreamFrameWriter.Write(
                message => socket.Send(routingId, message, SendFlags.None),
                ZLinkStreamSessionClosingCodec.CreateHeader(),
                payload,
                "Could not submit the session-closing control packet.",
                transport);
        }
        catch
        {
            ZLinkRuntimeMetrics.RecordDrainForced("session");
        }
        finally
        {
            socket.DisconnectPeer(routingId);
        }
    }
}
