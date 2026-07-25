namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Public messaging lookup surfaces: mesh-scoped spot rid / actor id to a
/// full spot address. The actor lookup derives the address from the actor
/// row's spot kind — the
/// entry spot address is the node itself. The returned opaque handle keeps
/// its logical lookup key and receives location-event updates without
/// exposing address refresh policy to callers.
/// </summary>
internal sealed class ZLinkLocationAddressResolvers :
    IZLinkSpotHandleResolver,
    IZLinkActorSpotHandleResolver
{
    private readonly ZLinkStoreLocationResolvers _rows;
    private readonly ZLinkSpotHandleRegistry _handles;

    internal ZLinkLocationAddressResolvers(
        ZLinkStoreLocationResolvers rows,
        ZLinkSpotHandleRegistry handles)
    {
        _rows = rows;
        _handles = handles;
    }

    public async ValueTask<SpotHandle?> ResolveSpotHandleAsync(
        string meshName,
        string spotId,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(meshName);
        var key = new ZLinkSpotLocationKey(spotId);
        var row = await _rows.ResolveSpotRowAsync(key, cancellationToken).ConfigureAwait(false);
        if (row is null) return null;
        var handle = new ZLinkResolvedSpotHandle(
            ToSnapshot(row),
            row.SpotGeneration,
            ct => RefreshSpotAsync(key, ct));
        _handles.RegisterSpot(key, handle);
        return handle;
    }

    public async ValueTask<SpotHandle?> ResolveActorSpotHandleAsync(
        string meshName,
        string actorId,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(meshName);
        ArgumentException.ThrowIfNullOrWhiteSpace(actorId);
        var key = new ZLinkActorLocationKey(actorId);
        var row = await _rows.ResolveActorRowAsync(key, cancellationToken)
            .ConfigureAwait(false);
        if (row is null)
        {
            return null;
        }

        // Entry actors use their node's Entry Spot snapshot; actors in a user
        // Spot use that Spot's current snapshot. Membership epoch orders the
        // handle updates because the addressed spot generation resets when an
        // actor moves back to the entry spot.
        var handle = new ZLinkResolvedSpotHandle(
            ToSnapshot(row),
            row.MembershipEpoch,
            ct => RefreshActorAsync(key, ct));
        _handles.RegisterActor(key, handle);
        return handle;
    }

    private async ValueTask<(ZLinkSpotHandleSnapshot Snapshot, ulong Version)?> RefreshSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken)
    {
        var row = await _rows.ResolveSpotRowAsync(key, cancellationToken).ConfigureAwait(false);
        return row is null ? null : (ToSnapshot(row), row.SpotGeneration);
    }

    private async ValueTask<(ZLinkSpotHandleSnapshot Snapshot, ulong Version)?> RefreshActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken)
    {
        var row = await _rows.ResolveActorRowAsync(key, cancellationToken)
            .ConfigureAwait(false);
        return row is null ? null : (ToSnapshot(row), row.MembershipEpoch);
    }

    private ZLinkSpotHandleSnapshot ToSnapshot(ZLinkSpotLocation row)
        => new(
            row.MeshName,
            row.OwnerNodeRid,
            row.SpotId,
            row.SpotGeneration,
            row.SpotKind,
            row.AuthorityOwnerGeneration);

    internal ZLinkSpotHandleSnapshot ToSnapshot(ZLinkActorLocation row)
        => row.SpotKind == ZLinkSpotKind.Entry || string.IsNullOrEmpty(row.SpotId)
            ? new ZLinkSpotHandleSnapshot(
                row.MeshName,
                row.OwnerNodeRid,
                row.SpotId,
                row.SpotGeneration,
                ZLinkSpotKind.Entry,
                row.AuthorityOwnerGeneration)
            : new ZLinkSpotHandleSnapshot(
                row.MeshName,
                row.OwnerNodeRid,
                row.SpotId,
                row.SpotGeneration,
                ZLinkSpotKind.User,
                row.AuthorityOwnerGeneration);
}

internal readonly record struct ZLinkSpotHandleSnapshot(
    string RouterChannelId,
    RoutingId NodeRid,
    string SpotId,
    ulong Generation,
    ZLinkSpotKind SpotKind = ZLinkSpotKind.User,
    ulong AuthorityOwnerGeneration = 0);

internal sealed class ZLinkResolvedSpotHandle : SpotHandle
{
    private readonly Func<CancellationToken, ValueTask<(ZLinkSpotHandleSnapshot Snapshot, ulong Version)?>> _refresh;
    private readonly object _gate = new();
    private ZLinkHandleAvailability _availability = ZLinkHandleAvailability.Available;
    private ulong _version;
    private ZLinkSpotHandleSnapshot _snapshot;

    internal ZLinkResolvedSpotHandle(
        ZLinkSpotHandleSnapshot initialSnapshot,
        ulong version,
        Func<CancellationToken, ValueTask<(ZLinkSpotHandleSnapshot Snapshot, ulong Version)?>> refresh)
    {
        _refresh = refresh;
        _snapshot = initialSnapshot;
        _version = version;
    }

    internal ZLinkSpotHandleSnapshot Snapshot
    {
        get
        {
            lock (_gate)
            {
                if (_availability != ZLinkHandleAvailability.Available)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.SpotRouteNotFound,
                        "The resolved spot handle is no longer available.");
                return _snapshot;
            }
        }
    }

    public override string MeshName { get { lock (_gate) return _snapshot.RouterChannelId; } }

    public override string SpotId { get { lock (_gate) return _snapshot.SpotId; } }

    internal void Update(ZLinkSpotHandleSnapshot snapshot, ulong version)
    {
        lock (_gate)
        {
            if (version < _version
                || (version == _version
                    && _availability == ZLinkHandleAvailability.Removed)) return;
            _snapshot = snapshot;
            _version = version;
            _availability = ZLinkHandleAvailability.Available;
        }
    }

    internal void Invalidate(ulong version)
    {
        lock (_gate)
        {
            if (version < _version) return;
            _version = version;
            _availability = ZLinkHandleAvailability.Removed;
        }
    }

    /// <summary>Invalidates at the current version: a later update with a
    /// strictly newer version resurrects the handle, a stale replay of the
    /// same version does not.</summary>
    internal void InvalidateCurrent()
    {
        lock (_gate)
        {
            _availability = ZLinkHandleAvailability.Removed;
        }
    }

    internal async ValueTask<bool> RefreshAsync(CancellationToken cancellationToken)
    {
        var refreshed = await _refresh(cancellationToken).ConfigureAwait(false);
        if (refreshed is not { } current) return false;
        Update(current.Snapshot, current.Version);
        return true;
    }
}

internal enum ZLinkHandleAvailability
{
    Available,
    Removed
}

internal static class ZLinkSpotHandleRequestExecution
{
    internal static async ValueTask<T> ExecuteAsync<T>(
        ZLinkResolvedSpotHandle handle,
        Func<ZLinkSpotHandleSnapshot, ValueTask<T>> operation,
        CancellationToken cancellationToken)
    {
        try
        {
            return await operation(handle.Snapshot).ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException error) when (IsRefreshCandidate(error))
        {
            if (!await handle.RefreshAsync(cancellationToken).ConfigureAwait(false)) throw;
            return await operation(handle.Snapshot).ConfigureAwait(false);
        }
    }

    private static bool IsRefreshCandidate(ZLinkFrameworkException error)
        => error.Kind is ZLinkFrameworkErrorKind.SpotRouteNotFound
            or ZLinkFrameworkErrorKind.RequestTargetNotFound;
}
