namespace Zlink.Framework.Runtime.Locations;

internal enum ZLinkActorClaimStatus
{
    Claimed = 1,
    AlreadyOwned = 2,
    Conflict = 3,
    StoreUnavailable = 4
}

internal readonly record struct ZLinkActorClaimResult(
    ZLinkActorClaimStatus Status,
    ZLinkActorLocation? Existing);

internal readonly record struct ZLinkActorClaimActivation<TActor>(
    TActor? Activated,
    ZLinkActorLocation? ExistingLocation)
    where TActor : class;

/// <summary>
/// Lifecycle write policy over <see cref="ZLinkLocationRuntime"/>: claims,
/// renews, and removes the spot/actor/route rows this owner hosts and keeps
/// the store-issued generation as the owner token for later writes. Tracks
/// canonical key to local handle so an ownership loss (a write coming back
/// IgnoredStale after another owner's Takeover) deactivates the local
/// instance and single-activation holds (draft section 9).
/// </summary>
internal sealed class ZLinkLocationLifecycle : IDisposable
{
    private static readonly AsyncLocal<bool> TakeoverScope = new();

    private readonly ZLinkLocationRuntime _runtime;
    private readonly ZLinkStoreLocationResolvers _actorResolver;
    private readonly object _gate = new();
    private readonly Dictionary<string, TrackedActor> _actors = new(StringComparer.Ordinal);
    private readonly Dictionary<string, TrackedSpot> _spots = new(StringComparer.Ordinal);
    private readonly Dictionary<string, long> _routes = new(StringComparer.Ordinal);

    internal ZLinkLocationLifecycle(
        ZLinkLocationRuntime runtime,
        ZLinkStoreLocationResolvers actorResolver)
    {
        _runtime = runtime;
        _actorResolver = actorResolver;
        _runtime.OwnershipLost += OnOwnershipLost;
    }

    public void Dispose()
    {
        _runtime.OwnershipLost -= OnOwnershipLost;
    }

    /// <summary>
    /// Marks the current async flow as a hosting handoff (remote actor
    /// join): a claim that hits a live row of another owner may proceed
    /// with Takeover because the previous owner deactivates its instance
    /// as part of the move protocol (draft section 9).
    /// </summary>
    internal static IDisposable EnterActorTakeoverScope()
    {
        TakeoverScope.Value = true;
        return new TakeoverScopeExit();
    }

    // ----- actors (draft 16.1, 17) -----

    /// <summary>
    /// Claim-then-activate (draft section 17): the actor location claim
    /// must succeed before <paramref name="activate"/> runs. A losing
    /// claimer never activates; it gets the winner's freshly resolved
    /// location instead. An activation failure rolls the claim back.
    /// </summary>
    internal async ValueTask<ZLinkActorClaimActivation<TActor>> ExecuteActorClaimThenActivateAsync<TActor>(
        string actorType,
        string actorId,
        RoutingId nodeRid,
        Func<CancellationToken, ValueTask>? deactivate,
        Func<CancellationToken, ValueTask<TActor>> activate,
        CancellationToken cancellationToken)
        where TActor : class
    {
        var claim = await ClaimActorAsync(actorType, actorId, nodeRid, deactivate, cancellationToken)
            .ConfigureAwait(false);
        switch (claim.Status)
        {
            case ZLinkActorClaimStatus.Conflict:
                return new ZLinkActorClaimActivation<TActor>(null, claim.Existing);

            case ZLinkActorClaimStatus.StoreUnavailable:
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorCreateFailed,
                    $"Actor '{actorId}' cannot be created because the location store is unavailable.");
        }

        try
        {
            var activated = await activate(cancellationToken).ConfigureAwait(false);
            return new ZLinkActorClaimActivation<TActor>(activated, null);
        }
        catch when (claim.Status == ZLinkActorClaimStatus.Claimed)
        {
            // The claim preceded the failed activation; without a rollback
            // the key would stay owned by an instance that never existed.
            await ReleaseActorAsync(actorType, actorId, CancellationToken.None).ConfigureAwait(false);
            throw;
        }
    }

    internal async ValueTask<ZLinkActorClaimResult> ClaimActorAsync(
        string actorType,
        string actorId,
        RoutingId nodeRid,
        Func<CancellationToken, ValueTask>? deactivate,
        CancellationToken cancellationToken)
    {
        var normalizedType = ZLinkLocationKeyCodec.NormalizeActorType(actorType);
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(
            new ZLinkActorLocationKey(normalizedType, actorId));
        lock (_gate)
        {
            if (_actors.ContainsKey(canonical))
            {
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.AlreadyOwned, null);
            }
        }

        var row = new ZLinkActorLocation(
            normalizedType,
            actorId,
            ActorRef: string.Empty,
            nodeRid,
            Generation: 0,
            LocationKind: ZLinkSpotKind.Entry,
            SpotRid: null,
            SpotKind: ZLinkSpotKind.Entry,
            OwnerId: string.Empty,
            UpdatedAt: default);
        var result = await _runtime.WriteActorAsync(row, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
            .ConfigureAwait(false);
        if (result.Status == ZLinkLocationWriteStatus.RejectedConflict && TakeoverScope.Value)
        {
            // Hosting handoff: the live row belongs to the previous host,
            // which deactivates its instance as part of the move. Takeover
            // is the fencing path that lets the new host claim anyway.
            result = await _runtime.WriteActorAsync(row, ZLinkLocationWriteIntent.Takeover, cancellationToken)
                .ConfigureAwait(false);
        }

        switch (result.Status)
        {
            case ZLinkLocationWriteStatus.Stored:
                lock (_gate)
                {
                    _actors[canonical] = new TrackedActor(
                        row with { Generation = result.Generation },
                        deactivate);
                }

                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.Claimed, null);

            case ZLinkLocationWriteStatus.RejectedConflict:
            {
                var existing = await ResolveExistingActorAsync(
                        normalizedType,
                        actorId,
                        cancellationToken)
                    .ConfigureAwait(false);
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.Conflict, existing);
            }

            default:
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.StoreUnavailable, null);
        }
    }

    internal ValueTask SetActorRefAsync(
        string actorType,
        string actorId,
        string actorRef,
        CancellationToken cancellationToken = default) =>
        RenewActorAsync(actorType, actorId, row => row with { ActorRef = actorRef }, cancellationToken);

    internal ValueTask NotifyActorJoinedSpotAsync(
        string actorType,
        string actorId,
        RoutingId spotRid,
        CancellationToken cancellationToken = default) =>
        RenewActorAsync(
            actorType,
            actorId,
            row => row with
            {
                LocationKind = ZLinkSpotKind.User,
                SpotRid = spotRid,
                SpotKind = ZLinkSpotKind.User
            },
            cancellationToken);

    internal ValueTask NotifyActorMovedToEntrySpotAsync(
        string actorType,
        string actorId,
        RoutingId targetNodeRid,
        CancellationToken cancellationToken = default) =>
        RenewActorAsync(
            actorType,
            actorId,
            row => row with
            {
                NodeRid = targetNodeRid,
                LocationKind = ZLinkSpotKind.Entry,
                SpotRid = null,
                SpotKind = ZLinkSpotKind.Entry
            },
            cancellationToken);

    internal ValueTask NotifyActorLeftSpotAsync(
        string actorType,
        string actorId,
        CancellationToken cancellationToken = default) =>
        RenewActorAsync(
            actorType,
            actorId,
            row => row with
            {
                LocationKind = ZLinkSpotKind.Entry,
                SpotRid = null,
                SpotKind = ZLinkSpotKind.Entry
            },
            cancellationToken);

    internal async ValueTask ReleaseActorAsync(
        string actorType,
        string actorId,
        CancellationToken cancellationToken = default)
    {
        var key = new ZLinkActorLocationKey(
            ZLinkLocationKeyCodec.NormalizeActorType(actorType), actorId);
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(key);
        TrackedActor? tracked;
        lock (_gate)
        {
            if (!_actors.Remove(canonical, out tracked))
            {
                return;
            }
        }

        // Untracked before the write: a stale remove after another owner's
        // Takeover is ignored by the store and must not fire deactivation.
        await _runtime.RemoveActorAsync(key, tracked.Row.Generation, cancellationToken)
            .ConfigureAwait(false);
    }

    internal bool OwnsActor(string actorType, string actorId)
    {
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(new ZLinkActorLocationKey(
            ZLinkLocationKeyCodec.NormalizeActorType(actorType), actorId));
        lock (_gate)
        {
            return _actors.ContainsKey(canonical);
        }
    }

    // ----- spots (draft 15.1) -----

    internal async ValueTask<ZLinkLocationWriteStatus> ClaimSpotAsync(
        string meshName,
        RoutingId spotRid,
        string? spotType,
        RoutingId nodeRid,
        ZLinkSpotKind spotKind,
        string? routeEndpoint,
        Func<CancellationToken, ValueTask>? deactivate,
        CancellationToken cancellationToken = default)
    {
        var row = new ZLinkSpotLocation(
            meshName,
            spotRid,
            spotType,
            nodeRid,
            spotKind,
            routeEndpoint,
            OwnerId: string.Empty,
            Generation: 0,
            UpdatedAt: default);
        var result = await _runtime.WriteSpotAsync(row, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
            .ConfigureAwait(false);
        if (result.Status == ZLinkLocationWriteStatus.Stored)
        {
            var canonical = ZLinkLocationKeyCodec.EncodeSpotKey(
                new ZLinkSpotLocationKey(meshName, spotRid));
            lock (_gate)
            {
                _spots[canonical] = new TrackedSpot(result.Generation, deactivate);
            }
        }

        return result.Status;
    }

    internal async ValueTask ReleaseSpotAsync(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        var key = new ZLinkSpotLocationKey(meshName, spotRid);
        var canonical = ZLinkLocationKeyCodec.EncodeSpotKey(key);
        TrackedSpot? tracked;
        lock (_gate)
        {
            if (!_spots.Remove(canonical, out tracked))
            {
                return;
            }
        }

        await _runtime.RemoveSpotAsync(key, tracked.Generation, cancellationToken).ConfigureAwait(false);
    }

    // ----- actor session routes (draft 6.4 route kinds) -----

    /// <summary>Fire-and-forget wrapper for the synchronous session bind
    /// path. Route rows are advisory; owner lease and polling remain the
    /// correctness path, so a lost write is only a temporary miss.</summary>
    internal void OnActorSessionBound(RoutingId sessionRid, string actorId, RoutingId ownerNodeRid)
    {
        _ = RunGuardedAsync(() => BindActorSessionRouteAsync(
            sessionRid, actorId, ownerNodeRid, CancellationToken.None));
    }

    internal void OnActorSessionUnbound(RoutingId sessionRid)
    {
        _ = RunGuardedAsync(() => RemoveActorSessionRouteAsync(sessionRid, CancellationToken.None));
    }

    internal async ValueTask BindActorSessionRouteAsync(
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
        var result = await _runtime.WriteRouteAsync(row, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
            .ConfigureAwait(false);
        if (result.Status == ZLinkLocationWriteStatus.RejectedConflict)
        {
            // A session rebind moves the route to this node; replacing the
            // previous owner's row is an owner change, hence Takeover.
            result = await _runtime.WriteRouteAsync(row, ZLinkLocationWriteIntent.Takeover, cancellationToken)
                .ConfigureAwait(false);
        }

        if (result.Status != ZLinkLocationWriteStatus.Stored)
        {
            return;
        }

        var canonical = ZLinkLocationKeyCodec.EncodeRouteKey(
            new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, routeKey));
        lock (_gate)
        {
            _routes[canonical] = result.Generation;
        }
    }

    internal async ValueTask RemoveActorSessionRouteAsync(
        RoutingId sessionRid,
        CancellationToken cancellationToken = default)
    {
        var key = new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, sessionRid.ToHex());
        var canonical = ZLinkLocationKeyCodec.EncodeRouteKey(key);
        long generation;
        lock (_gate)
        {
            if (!_routes.Remove(canonical, out generation))
            {
                return;
            }
        }

        await _runtime.RemoveRouteAsync(key, generation, cancellationToken).ConfigureAwait(false);
    }

    internal static string SerializeActorRef(RoutingId nodeRid, string actorId, ulong generation) =>
        $"{nodeRid.ToHex()}:{generation}:{actorId}";

    // ----- ownership loss (draft 9) -----

    private void OnOwnershipLost(ZLinkLocationKind kind, string canonicalKey)
    {
        Func<CancellationToken, ValueTask>? deactivate = null;
        lock (_gate)
        {
            switch (kind)
            {
                case ZLinkLocationKind.Actor when _actors.Remove(canonicalKey, out var actor):
                    deactivate = actor.Deactivate;
                    break;

                case ZLinkLocationKind.Spot when _spots.Remove(canonicalKey, out var spot):
                    deactivate = spot.Deactivate;
                    break;

                case ZLinkLocationKind.Route:
                    _routes.Remove(canonicalKey);
                    break;
            }
        }

        if (deactivate is not null)
        {
            _ = RunGuardedAsync(() => deactivate(CancellationToken.None));
        }
    }

    private async ValueTask<ZLinkActorLocation?> ResolveExistingActorAsync(
        string actorType,
        string actorId,
        CancellationToken cancellationToken)
    {
        try
        {
            return await _actorResolver.ResolveActorRowAsync(
                    new ZLinkActorLocationKey(actorType, actorId),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"actor conflict resolve failed for '{actorId}': {exception.Message}");
            return null;
        }
    }

    private ValueTask RenewActorAsync(
        string actorType,
        string actorId,
        Func<ZLinkActorLocation, ZLinkActorLocation> mutate,
        CancellationToken cancellationToken)
    {
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(new ZLinkActorLocationKey(
            ZLinkLocationKeyCodec.NormalizeActorType(actorType), actorId));
        TrackedActor? tracked;
        lock (_gate)
        {
            if (!_actors.TryGetValue(canonical, out tracked))
            {
                return ValueTask.CompletedTask;
            }

            tracked.Row = mutate(tracked.Row);
        }

        // Renew keeps the store-issued generation; an IgnoredStale answer
        // raises OwnershipLost through the runtime and deactivates locally.
        return new ValueTask(_runtime.WriteActorAsync(
            tracked.Row,
            ZLinkLocationWriteIntent.Renew,
            cancellationToken).AsTask());
    }

    private static async Task RunGuardedAsync(Func<ValueTask> operation)
    {
        try
        {
            await operation().ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery($"location lifecycle error: {exception.Message}");
        }
    }

    private sealed class TrackedActor(
        ZLinkActorLocation row,
        Func<CancellationToken, ValueTask>? deactivate)
    {
        public ZLinkActorLocation Row { get; set; } = row;

        public Func<CancellationToken, ValueTask>? Deactivate { get; } = deactivate;
    }

    private sealed record TrackedSpot(
        long Generation,
        Func<CancellationToken, ValueTask>? Deactivate);

    private sealed class TakeoverScopeExit : IDisposable
    {
        public void Dispose()
        {
            TakeoverScope.Value = false;
        }
    }
}
