namespace Zlink.Framework.Runtime.Spots;

internal enum ZLinkRelocationReplyAckState : byte
{
    NotAcknowledged = 0,
    TerminalReceived = 1,
    AlreadyTerminal = 2
}

internal sealed record ZLinkRelocationReplyBinding(
    ZLinkAuthorityKey AuthorityKey,
    ZLinkServiceWireCodec.RelocationWireId RelocationId,
    ulong TargetAttemptGeneration,
    string CoordinatorOwnerId,
    ulong CoordinatorLeaseGeneration,
    RoutingId CoordinatorNodeRid,
    ulong CoordinatorNodeGeneration,
    ulong ParticipantId,
    ulong Sequence,
    ZLinkServiceWireCodec.RequestSourceFence RequestSource);

/// <summary>
/// Keeps opaque source reply capabilities in the source process while an
/// accepted request is relocated. The capability never enters application
/// metadata or the relocation root; only its process-local id does.
/// </summary>
internal sealed class ZLinkSpotRelocationReplyRoutes
{
    private const int MaxRoutes = 4_096;
    internal static readonly TimeSpan LateCompletionRetention =
        TimeSpan.FromHours(24);
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
                    received.OperationId,
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
        ulong targetAuthorityOwnerGeneration,
        ZLinkRelocationReplyBinding binding)
    {
        if (routeId == 0) return;
        lock (_gate)
        {
            if (_routes.TryGetValue(routeId, out var route))
            {
                route.TargetNodeRid = targetNodeRid;
                route.TargetAuthorityOwnerGeneration =
                    targetAuthorityOwnerGeneration;
                route.RelayBinding = binding;
            }
        }
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
            {
                route.TargetNodeRid = targetNodeRid;
                route.TargetAuthorityOwnerGeneration =
                    targetAuthorityOwnerGeneration;
            }
        }
    }

    internal bool TryGetRelayBinding(
        ZLinkServiceWireCodec.ReplyRelayRecord relay,
        RoutingId targetNodeRid,
        out ZLinkRelocationReplyBinding binding)
    {
        lock (_gate)
        {
            RemoveExpiredUnderLock(_time.GetUtcNow());
            var route = _routes.GetValueOrDefault(relay.ReplyRouteId);
            if (route?.RelayBinding is not { } candidate
                || route.OperationId != relay.OperationId
                || route.TargetNodeRid != targetNodeRid
                || candidate.RelocationId != relay.RelocationId
                || candidate.TargetAttemptGeneration
                   != relay.TargetAttemptGeneration
                || candidate.CoordinatorOwnerId
                   != relay.Coordinator.OwnerId
                || candidate.CoordinatorLeaseGeneration
                   != relay.Coordinator.LeaseGeneration
                || candidate.CoordinatorNodeRid
                   != relay.Coordinator.NodeRid
                || candidate.CoordinatorNodeGeneration
                   != relay.Coordinator.NodeGeneration
                || candidate.ParticipantId != relay.ParticipantId
                || candidate.Sequence != relay.Sequence)
            {
                binding = null!;
                return false;
            }
            binding = candidate;
            return true;
        }
    }

    internal SubmitResult TryRelay(
        ZLinkServiceWireCodec.ReplyRelayRecord relay,
        RoutingId targetNodeRid,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        out bool consumed,
        out bool alreadyTerminal)
    {
        string spotId;
        ulong objectGeneration;
        ulong targetAuthorityOwnerGeneration;
        lock (_gate)
        {
            if (!_routes.TryGetValue(relay.ReplyRouteId, out var route))
            {
                consumed = false;
                alreadyTerminal = false;
                return SubmitResult.NotFound;
            }
            spotId = route.SpotId;
            objectGeneration = route.ObjectGeneration;
            targetAuthorityOwnerGeneration =
                route.TargetAuthorityOwnerGeneration;
        }
        if (!TryGetRelayBinding(relay, targetNodeRid, out _))
        {
            consumed = false;
            alreadyTerminal = false;
            return SubmitResult.NotFound;
        }
        return TryRelay(
            relay.ReplyRouteId,
            relay.OperationId,
            spotId,
            objectGeneration,
            targetNodeRid,
            targetAuthorityOwnerGeneration,
            hopCount: 1,
            parts,
            flags,
            out consumed,
            out alreadyTerminal);
    }

    internal SubmitResult TryRelay(
        ulong routeId,
        MeshOperationId operationId,
        string spotId,
        ulong objectGeneration,
        RoutingId targetNodeRid,
        ulong targetAuthorityOwnerGeneration,
        int hopCount,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        out bool consumed,
        out bool alreadyTerminal)
    {
        consumed = false;
        alreadyTerminal = false;
        Route? route;
        lock (_gate)
        {
            RemoveExpiredUnderLock(_time.GetUtcNow());
            route = routeId == 0
                ? _routes.Values.SingleOrDefault(candidate =>
                    candidate.OperationId == operationId)
                : _routes.GetValueOrDefault(routeId);
            if (route is null
                || route.OperationId != operationId
                || route.SpotId != spotId
                || route.ObjectGeneration != objectGeneration
                || route.TargetNodeRid != targetNodeRid
                || route.TargetAuthorityOwnerGeneration
                   != targetAuthorityOwnerGeneration
                || hopCount is < 0 or > 8)
                return SubmitResult.NotFound;
            if (route.Delivered)
            {
                alreadyTerminal = true;
                return SubmitResult.Ok;
            }
            if (route.RelayInProgress)
                return SubmitResult.Backpressured;
            route.RelayInProgress = true;
        }
        SubmitResult result;
        try
        {
            result = route.Reply(parts, flags);
            consumed = result == SubmitResult.Ok;
        }
        catch
        {
            lock (_gate) route.RelayInProgress = false;
            throw;
        }
        lock (_gate)
        {
            route.RelayInProgress = false;
            if (result == SubmitResult.Ok) route.Delivered = true;
        }
        return result;
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

    private sealed class Route(
        string SpotId,
        ulong ObjectGeneration,
        MeshOperationId OperationId,
        DateTimeOffset ExpiresAt,
        Func<IReadOnlyList<Message>, SendFlags, SubmitResult> Reply)
    {
        public string SpotId { get; } = SpotId;
        public ulong ObjectGeneration { get; } = ObjectGeneration;
        public MeshOperationId OperationId { get; } = OperationId;
        public DateTimeOffset ExpiresAt { get; } = ExpiresAt;
        public Func<IReadOnlyList<Message>, SendFlags, SubmitResult> Reply { get; } = Reply;
        public RoutingId? TargetNodeRid { get; set; }

        public ulong TargetAuthorityOwnerGeneration { get; set; }
        public ZLinkRelocationReplyBinding? RelayBinding { get; set; }
        public bool RelayInProgress { get; set; }
        public bool Delivered { get; set; }
    }
}
