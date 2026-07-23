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
    private readonly ZLinkLocationStoreHealth? _health;
    private readonly SemaphoreSlim _refreshGate = new(1, 1);
    private readonly object _liveSetGate = new();
    private volatile Snapshot? _snapshot;
    private string? _liveSetFingerprint;
    private long _liveSetVersion;

    internal ZLinkOwnerLeaseTracker(
        IZLinkOwnerLeaseStore store,
        ZLinkLocationOptions options,
        TimeProvider? timeProvider = null,
        ZLinkLocationStoreHealth? health = null)
    {
        _store = store;
        _options = options;
        _time = timeProvider ?? TimeProvider.System;
        _health = health;
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

    internal async ValueTask<bool> IsOwnerTokenLiveAsync(
        ZLinkLocationOwnerToken token,
        CancellationToken cancellationToken = default)
    {
        var snapshot = await GetSnapshotAsync(cancellationToken)
            .ConfigureAwait(false);
        if (!snapshot.Leases.TryGetValue(token.OwnerId, out var lease)
            || lease.LeaseGeneration != token.LeaseGeneration)
            return false;
        var elapsedSinceFetch = _time.GetElapsedTime(snapshot.FetchedAt);
        return lease.LeaseExpiresAt - snapshot.StoreNow - elapsedSinceFetch
               > TimeSpan.Zero;
    }

    /// <summary>
    /// Version of the set of currently live owners. The desired target set
    /// is a join of rows and leases, so a reconcile tick must not be
    /// skipped just because no row changed: an owner appearing (its rows
    /// become visible) or expiring (its rows must drop) changes the join
    /// without any row write. The version bumps only on membership changes,
    /// never on plain renewals, so a healthy steady state keeps the change
    /// stamp skip effective.
    /// </summary>
    internal async ValueTask<long> GetLiveOwnerSetVersionAsync(
        CancellationToken cancellationToken = default)
    {
        var snapshot = await GetSnapshotAsync(cancellationToken).ConfigureAwait(false);
        var elapsedSinceFetch = _time.GetElapsedTime(snapshot.FetchedAt);
        var live = snapshot.Leases.Values
            .Where(lease => lease.LeaseExpiresAt - snapshot.StoreNow - elapsedSinceFetch > TimeSpan.Zero)
            .Select(static lease => lease.OwnerId)
            .Order(StringComparer.Ordinal);
        var fingerprint = string.Join('\n', live);

        lock (_liveSetGate)
        {
            if (!string.Equals(fingerprint, _liveSetFingerprint, StringComparison.Ordinal))
            {
                _liveSetFingerprint = fingerprint;
                _liveSetVersion++;
            }

            return _liveSetVersion;
        }
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
            var listed = await ZLinkLocationStoreRead.ExecuteAsync(
                    _health,
                    "owner-lease-read",
                    cancellationToken,
                    storeToken => _store.ListOwnerLeasesAsync(storeToken))
                .ConfigureAwait(false);
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
