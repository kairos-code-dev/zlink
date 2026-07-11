namespace Zlink.Framework.Runtime.Locations;

internal sealed class ZLinkActorSessionRouteLifecycle(
    ZLinkLocationRuntime runtime,
    Func<Func<CancellationToken, ValueTask>, bool> schedule)
{
    private readonly object _gate = new();
    private readonly Queue<RouteChange> _pending = new();
    private readonly Dictionary<string, long> _routes = new(StringComparer.Ordinal);
    private bool _draining;

    internal void OnActorSessionBound(
        RoutingId sessionRid,
        string actorId,
        RoutingId ownerNodeRid)
    {
        Enqueue(new RouteChange(sessionRid, actorId, ownerNodeRid));
    }

    internal void OnActorSessionUnbound(RoutingId sessionRid)
    {
        Enqueue(new RouteChange(sessionRid, null, default));
    }

    internal async ValueTask BindAsync(
        RoutingId sessionRid,
        string actorId,
        RoutingId ownerNodeRid,
        CancellationToken cancellationToken = default)
    {
        var routeKey = sessionRid.ToHex();
        var row = new ZLinkRouteLocation(
            ZLinkRouteKind.ActorSession,
            routeKey,
            ownerNodeRid,
            OwnerId: string.Empty,
            Generation: 0,
            Value: System.Text.Encoding.UTF8.GetBytes(actorId),
            UpdatedAt: default);
        var result = await runtime.WriteRouteAsync(row, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
            .ConfigureAwait(false);
        if (result.Status == ZLinkLocationWriteStatus.RejectedConflict)
        {
            // A session rebind moves the route to this node; replacing the
            // previous owner's row is an owner change, hence Takeover.
            result = await runtime.WriteRouteAsync(row, ZLinkLocationWriteIntent.Takeover, cancellationToken)
                .ConfigureAwait(false);
        }

        if (result.Status != ZLinkLocationWriteStatus.Stored)
            throw new InvalidOperationException(
                $"Actor session route '{sessionRid}' write was rejected with '{result.Status}'.");

        var canonical = ZLinkLocationKeyCodec.EncodeRouteKey(
            new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, routeKey));
        lock (_gate)
        {
            _routes[canonical] = result.Generation;
        }
    }

    internal async ValueTask RemoveAsync(
        RoutingId sessionRid,
        CancellationToken cancellationToken = default)
    {
        var key = new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, sessionRid.ToHex());
        var canonical = ZLinkLocationKeyCodec.EncodeRouteKey(key);
        long generation;
        lock (_gate)
        {
            if (!_routes.TryGetValue(canonical, out generation))
            {
                return;
            }
        }

        var result = await runtime.RemoveRouteAsync(key, generation, cancellationToken).ConfigureAwait(false);
        if (result.Status is not (ZLinkLocationWriteStatus.Stored or ZLinkLocationWriteStatus.IgnoredStale))
            throw new InvalidOperationException(
                $"Actor session route '{sessionRid}' removal was rejected with '{result.Status}'.");

        lock (_gate)
        {
            if (_routes.TryGetValue(canonical, out var current) && current == generation)
                _routes.Remove(canonical);
        }
    }

    internal void OnOwnershipLost(string canonicalKey)
    {
        lock (_gate)
        {
            _routes.Remove(canonicalKey);
        }
    }

    internal void ResetGeneration()
    {
        lock (_gate)
        {
            _pending.Clear();
            _routes.Clear();
            _draining = false;
        }
    }

    private void Enqueue(RouteChange change)
    {
        var startDrain = false;
        lock (_gate)
        {
            _pending.Enqueue(change);
            if (!_draining)
            {
                _draining = true;
                startDrain = true;
            }
        }

        if (startDrain && !schedule(DrainAsync))
            ResetGeneration();
    }

    private async ValueTask DrainAsync(CancellationToken cancellationToken)
    {
        try
        {
            while (true)
            {
                RouteChange change;
                lock (_gate)
                {
                    if (!_pending.TryDequeue(out change)) return;
                }

                while (true)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    try
                    {
                        if (change.ActorId is { } actorId)
                            await BindAsync(
                                    change.SessionRid,
                                    actorId,
                                    change.OwnerNodeRid,
                                    cancellationToken)
                                .ConfigureAwait(false);
                        else
                            await RemoveAsync(change.SessionRid, cancellationToken).ConfigureAwait(false);
                        break;
                    }
                    catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                    {
                        throw;
                    }
                    catch (Exception exception)
                    {
                        ZLinkFrameworkDebugLog.SpotDiscovery(
                            $"actor session route reconciliation retry: {exception.Message}");
                        await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken)
                            .ConfigureAwait(false);
                    }
                }
            }
        }
        finally
        {
            var restart = false;
            lock (_gate)
            {
                _draining = false;
                if (_pending.Count != 0 && !cancellationToken.IsCancellationRequested)
                {
                    _draining = true;
                    restart = true;
                }
            }

            if (restart && !schedule(DrainAsync)) ResetGeneration();
        }
    }

    private readonly record struct RouteChange(
        RoutingId SessionRid,
        string? ActorId,
        RoutingId OwnerNodeRid);
}
