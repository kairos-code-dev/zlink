namespace Zlink.Framework.Runtime.Spots;

/// <summary>
/// Keeps opaque source reply capabilities in the source process while an
/// accepted request is relocated. The capability never enters application
/// metadata or the relocation root; only its process-local id does.
/// </summary>
internal sealed class ZLinkSpotRelocationReplyRoutes
{
    private const int MaxRoutes = 4_096;
    private readonly object _gate = new();
    private readonly Dictionary<ulong, Route> _routes = [];
    private readonly TimeProvider _time;
    private readonly TimeSpan _retention;
    private ulong _nextRouteId;

    internal ZLinkSpotRelocationReplyRoutes(
        TimeSpan retention,
        TimeProvider? timeProvider = null)
    {
        if (retention < TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(retention));
        _retention = retention;
        _time = timeProvider ?? TimeProvider.System;
    }

    internal ulong Register(
        ZLinkBackendRouteReceived received,
        string spotId,
        ulong objectGeneration)
    {
        ArgumentNullException.ThrowIfNull(received);
        ArgumentException.ThrowIfNullOrWhiteSpace(spotId);
        var reply = received.CaptureReplyRoute();
        if (reply is null)
            return 0;

        lock (_gate)
        {
            RemoveExpiredUnderLock(_time.GetUtcNow());
            if (_routes.Count >= MaxRoutes)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RequestRejected,
                    "The SPOT relocation reply-route registry is full.");
            var routeId = NextRouteIdUnderLock();
            _routes.Add(
                routeId,
                new Route(
                    spotId,
                    objectGeneration,
                    _time.GetUtcNow() + _retention,
                    reply));
            return routeId;
        }
    }

    internal void CompleteLocal(ulong routeId)
    {
        if (routeId == 0) return;
        lock (_gate) _routes.Remove(routeId);
    }

    internal void BindCommitted(
        ulong routeId,
        RoutingId targetNodeRid,
        ulong targetAuthorityOwnerGeneration)
    {
        if (routeId == 0) return;
        lock (_gate)
        {
            if (_routes.TryGetValue(routeId, out var route))
                _routes[routeId] = route with
                {
                    TargetNodeRid = targetNodeRid,
                    TargetAuthorityOwnerGeneration =
                        targetAuthorityOwnerGeneration
                };
        }
    }

    internal SubmitResult TryRelay(
        ulong routeId,
        string spotId,
        ulong objectGeneration,
        RoutingId targetNodeRid,
        ulong targetAuthorityOwnerGeneration,
        int hopCount,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        Route? route;
        lock (_gate)
        {
            RemoveExpiredUnderLock(_time.GetUtcNow());
            if (!_routes.TryGetValue(routeId, out route)
                || route.SpotId != spotId
                || route.ObjectGeneration != objectGeneration
                || route.TargetNodeRid != targetNodeRid
                || route.TargetAuthorityOwnerGeneration
                   != targetAuthorityOwnerGeneration
                || hopCount is < 0 or > 8)
                return SubmitResult.NotFound;
            _routes.Remove(routeId);
        }

        return route.Reply(parts, flags);
    }

    internal void Clear()
    {
        lock (_gate) _routes.Clear();
    }

    private ulong NextRouteIdUnderLock()
    {
        do
        {
            _nextRouteId = _nextRouteId == ulong.MaxValue
                ? 1
                : _nextRouteId + 1;
        } while (_routes.ContainsKey(_nextRouteId));
        return _nextRouteId;
    }

    private void RemoveExpiredUnderLock(DateTimeOffset now)
    {
        if (_routes.Count == 0) return;
        foreach (var routeId in _routes
                     .Where(entry => entry.Value.ExpiresAt <= now)
                     .Select(static entry => entry.Key)
                     .ToArray())
            _routes.Remove(routeId);
    }

    private sealed record Route(
        string SpotId,
        ulong ObjectGeneration,
        DateTimeOffset ExpiresAt,
        Func<IReadOnlyList<Message>, SendFlags, SubmitResult> Reply)
    {
        public RoutingId? TargetNodeRid { get; init; }

        public ulong TargetAuthorityOwnerGeneration { get; init; }
    }
}
