namespace Zlink.Framework.Runtime.Locations;

internal enum ZLinkActorClaimStatus
{
    Claimed = 1,
    AlreadyOwned = 2,
    Conflict = 3,
    StoreFailure = 4
}

internal readonly record struct ZLinkActorClaimResult(
    ZLinkActorClaimStatus Status,
    ZLinkActorLocation? Existing);

internal readonly record struct ZLinkActorClaimActivation<TActor>(
    TActor? Activated,
    ZLinkActorLocation? ExistingLocation)
    where TActor : class;

internal sealed class ZLinkActorOwnershipCoordinator(
    ZLinkLocationRuntime runtime,
    ZLinkStoreLocationResolvers resolver) : IZLinkActorLocationLifecycle
{
    private readonly object _gate = new();
    private readonly Dictionary<string, TrackedActor> _actors = new(StringComparer.Ordinal);

    public async ValueTask<ZLinkActorClaimActivation<TActor>> ExecuteActorClaimThenActivateAsync<TActor>(
        string actorType,
        string actorId,
        RoutingId nodeRid,
        Func<CancellationToken, ValueTask>? deactivate,
        Func<CancellationToken, ValueTask<TActor>> activate,
        CancellationToken cancellationToken,
        ZLinkActorClaimMode claimMode = ZLinkActorClaimMode.NewOwner)
        where TActor : class
    {
        var claim = await ClaimActorAsync(actorType, actorId, nodeRid, deactivate, claimMode, cancellationToken)
            .ConfigureAwait(false);
        switch (claim.Status)
        {
            case ZLinkActorClaimStatus.AlreadyOwned:
                return new ZLinkActorClaimActivation<TActor>(null, null);

            case ZLinkActorClaimStatus.Conflict:
                return new ZLinkActorClaimActivation<TActor>(null, claim.Existing);

            case ZLinkActorClaimStatus.StoreFailure:
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

    public async ValueTask<ZLinkActorClaimResult> ClaimActorAsync(
        string actorType,
        string actorId,
        RoutingId nodeRid,
        Func<CancellationToken, ValueTask>? deactivate,
        CancellationToken cancellationToken)
    {
        return await ClaimActorAsync(
                actorType,
                actorId,
                nodeRid,
                deactivate,
                ZLinkActorClaimMode.NewOwner,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkActorClaimResult> ClaimActorAsync(
        string actorType,
        string actorId,
        RoutingId nodeRid,
        Func<CancellationToken, ValueTask>? deactivate,
        ZLinkActorClaimMode claimMode,
        CancellationToken cancellationToken)
    {
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(
            new ZLinkActorLocationKey(actorId));
        lock (_gate)
        {
            if (_actors.ContainsKey(canonical))
            {
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.AlreadyOwned, null);
            }
        }

        var row = new ZLinkActorLocation(
            actorId,
            ActorType: actorType,
            ActorRef: null,
            nodeRid,
            LocationKind: ZLinkSpotKind.Entry,
            SpotMeshName: string.Empty,
            SpotRid: null,
            OwnerId: string.Empty,
            Generation: 0,
            UpdatedAt: default);
        ZLinkLocationWriteResult result;
        try
        {
            result = await runtime.WriteActorAsync(row, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            return new ZLinkActorClaimResult(ZLinkActorClaimStatus.StoreFailure, null);
        }
        if (result.Status == ZLinkLocationWriteStatus.RejectedConflict
            && claimMode == ZLinkActorClaimMode.TakeoverExistingOwner)
        {
            // Hosting handoff: the live row belongs to the previous host,
            // which deactivates its instance as part of the move. Takeover
            // is the fencing path that lets the new host claim anyway.
            try
            {
                result = await runtime.WriteActorAsync(row, ZLinkLocationWriteIntent.Takeover, cancellationToken)
                    .ConfigureAwait(false);
            }
            catch
            {
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.StoreFailure, null);
            }
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
                var existing = await ResolveExistingActorAsync(actorId, cancellationToken)
                    .ConfigureAwait(false);
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.Conflict, existing);
            }

            default:
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.StoreFailure, null);
        }
    }

    public async ValueTask<ZLinkLocationWriteResult> PublishActorRefAsync(
        string actorType,
        string actorId,
        ActorRef actorRef,
        CancellationToken cancellationToken = default)
    {
        var result = await RenewActorAsync(
            actorType,
            actorId,
            row => row with { ActorRef = actorRef },
            cancellationToken).ConfigureAwait(false);
        return result ?? ZLinkLocationWriteResult.IgnoredStale;
    }

    internal async ValueTask NotifyActorJoinedSpotAsync(
        string actorType,
        string actorId,
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        await RenewActorAsync(
                actorType,
                actorId,
                row => row with
                {
                    LocationKind = ZLinkSpotKind.User,
                    SpotRid = spotRid
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyActorMovedToEntrySpotAsync(
        string actorType,
        string actorId,
        RoutingId targetNodeRid,
        CancellationToken cancellationToken = default)
    {
        await RenewActorAsync(
                actorType,
                actorId,
                row => row with
                {
                    NodeRid = targetNodeRid,
                    LocationKind = ZLinkSpotKind.Entry,
                    SpotRid = null
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyActorLeftSpotAsync(
        string actorType,
        string actorId,
        CancellationToken cancellationToken = default)
    {
        await RenewActorAsync(
                actorType,
                actorId,
                row => row with
                {
                    LocationKind = ZLinkSpotKind.Entry,
                    SpotRid = null
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask ReleaseActorAsync(
        string actorType,
        string actorId,
        CancellationToken cancellationToken = default)
    {
        var key = new ZLinkActorLocationKey(actorId);
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
        await runtime.RemoveActorAsync(key, tracked.Row.Generation, cancellationToken)
            .ConfigureAwait(false);
    }

    internal bool OwnsActor(string actorType, string actorId)
    {
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(new ZLinkActorLocationKey(
            actorId));
        lock (_gate)
        {
            return _actors.ContainsKey(canonical);
        }
    }

    internal Func<CancellationToken, ValueTask>? TakeOwnershipLostDeactivation(string canonicalKey)
    {
        lock (_gate)
        {
            return _actors.Remove(canonicalKey, out var actor) ? actor.Deactivate : null;
        }
    }

    private async ValueTask<ZLinkActorLocation?> ResolveExistingActorAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        try
        {
            return await resolver.ResolveActorRowAsync(
                    new ZLinkActorLocationKey(actorId),
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

    private async ValueTask<ZLinkLocationWriteResult?> RenewActorAsync(
        string actorType,
        string actorId,
        Func<ZLinkActorLocation, ZLinkActorLocation> mutate,
        CancellationToken cancellationToken)
    {
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(new ZLinkActorLocationKey(
            actorId));
        TrackedActor? tracked;
        lock (_gate)
        {
            if (!_actors.TryGetValue(canonical, out tracked))
            {
                return null;
            }

            tracked.Row = mutate(tracked.Row);
        }

        // Renew keeps the store-issued generation; an IgnoredStale answer
        // raises OwnershipLost through the runtime and deactivates locally.
        return await runtime.WriteActorAsync(
                tracked.Row,
                ZLinkLocationWriteIntent.Renew,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private sealed class TrackedActor(
        ZLinkActorLocation row,
        Func<CancellationToken, ValueTask>? deactivate)
    {
        public ZLinkActorLocation Row { get; set; } = row;

        public Func<CancellationToken, ValueTask>? Deactivate { get; } = deactivate;
    }
}
