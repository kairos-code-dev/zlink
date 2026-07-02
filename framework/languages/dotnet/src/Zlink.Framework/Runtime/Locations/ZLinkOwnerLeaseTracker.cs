namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Caches the owner lease snapshot and answers "is this owner alive" for
/// row validity joins. Expiry is computed from the store's own time plus
/// locally measured monotonic elapsed time, never from the application wall
/// clock. The snapshot refreshes at most once per polling interval, which
/// bounds its staleness.
/// </summary>
internal sealed class ZLinkOwnerLeaseTracker
{
    private readonly IZLinkOwnerLeaseStore _store;
    private readonly ZLinkLocationOptions _options;
    private readonly TimeProvider _time;
    private readonly SemaphoreSlim _refreshGate = new(1, 1);
    private volatile Snapshot? _snapshot;

    internal ZLinkOwnerLeaseTracker(
        IZLinkOwnerLeaseStore store,
        ZLinkLocationOptions options,
        TimeProvider? timeProvider = null)
    {
        _store = store;
        _options = options;
        _time = timeProvider ?? TimeProvider.System;
    }

    internal async ValueTask<bool> IsOwnerLiveAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        var snapshot = await GetSnapshotAsync(cancellationToken).ConfigureAwait(false);
        if (!snapshot.Leases.TryGetValue(ownerId, out var lease))
        {
            return false;
        }

        var elapsedSinceFetch = _time.GetElapsedTime(snapshot.FetchedAt);
        return lease.LeaseExpiresAt - snapshot.StoreNow - elapsedSinceFetch > TimeSpan.Zero;
    }

    private async ValueTask<Snapshot> GetSnapshotAsync(CancellationToken cancellationToken)
    {
        var current = _snapshot;
        if (current is not null && _time.GetElapsedTime(current.FetchedAt) < _options.PollingInterval)
        {
            return current;
        }

        await _refreshGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            current = _snapshot;
            if (current is not null && _time.GetElapsedTime(current.FetchedAt) < _options.PollingInterval)
            {
                return current;
            }

            var fetchedAt = _time.GetTimestamp();
            var listed = await _store.ListOwnerLeasesAsync(cancellationToken).ConfigureAwait(false);
            var byOwner = listed.Leases.ToDictionary(lease => lease.OwnerId, StringComparer.Ordinal);
            var refreshed = new Snapshot(byOwner, listed.StoreNow, fetchedAt);
            _snapshot = refreshed;
            return refreshed;
        }
        finally
        {
            _refreshGate.Release();
        }
    }

    private sealed record Snapshot(
        IReadOnlyDictionary<string, ZLinkOwnerLease> Leases,
        DateTimeOffset StoreNow,
        long FetchedAt);
}
