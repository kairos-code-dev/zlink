namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Reads exact owner tokens for descriptor admission. The provider does not
/// expose a global lease list; each descriptor supplies the owner identity
/// that must be fenced.
/// </summary>
internal sealed class ZLinkOwnerLeaseTracker
{
    private readonly IZLinkLocationRepository _store;
    private readonly ZLinkLocationOptions _options;
    private readonly TimeProvider _time;
    private readonly ZLinkLocationStoreHealth? _health;
    private readonly object _cacheGate = new();
    private readonly Dictionary<string, Snapshot> _cache =
        new(StringComparer.Ordinal);
    private long _liveSetVersion;

    internal TimeProvider TimeProvider => _time;

    internal ZLinkOwnerLeaseTracker(
        IZLinkLocationRepository store,
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
        var snapshot = await GetSnapshotAsync(ownerId, cancellationToken)
            .ConfigureAwait(false);
        return snapshot.Token is not null
               && IsUnexpired(snapshot);
    }

    internal async ValueTask<bool> IsOwnerTokenLiveAsync(
        ZLinkLocationOwnerToken token,
        CancellationToken cancellationToken = default)
    {
        var snapshot = await GetSnapshotAsync(token.OwnerId, cancellationToken)
            .ConfigureAwait(false);
        return snapshot.Token == token && IsUnexpired(snapshot);
    }

    /// <summary>
    /// Returns the conservative time left before the owner must stop accepting
    /// new work. The lease can remain present after this point, but the
    /// fencing margin is reserved for the owner to seal admission before the
    /// provider expiry boundary.
    /// </summary>
    internal async ValueTask<TimeSpan?> GetOwnerTokenRemainingAdmissionLifetimeAsync(
        ZLinkLocationOwnerToken token,
        CancellationToken cancellationToken = default)
    {
        var snapshot = await GetSnapshotAsync(token.OwnerId, cancellationToken)
            .ConfigureAwait(false);
        if (snapshot.Token != token) return null;
        var remaining = snapshot.LeaseExpiresAt - snapshot.StoreNow
                        - _time.GetElapsedTime(snapshot.FetchedAt)
                        - _options.OwnerLeaseFencingMargin;
        return remaining > TimeSpan.Zero ? remaining : null;
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
        cancellationToken.ThrowIfCancellationRequested();
        await Task.CompletedTask.ConfigureAwait(false);
        return Interlocked.Increment(ref _liveSetVersion);
    }

    private async ValueTask<Snapshot> GetSnapshotAsync(
        string ownerId,
        CancellationToken cancellationToken)
    {
        Snapshot? current;
        lock (_cacheGate)
            _cache.TryGetValue(ownerId, out current);
        if (current is not null
            && _time.GetElapsedTime(current.FetchedAt) < _options.PollingInterval)
        {
            return current;
        }

        var fetchedAt = _time.GetTimestamp();
        var read = await ZLinkLocationStoreRead.ExecuteAsync(
                _health,
                "owner-lease-read",
                cancellationToken,
                storeToken => _store.ReadOwnerLeaseAsync(ownerId, storeToken))
            .ConfigureAwait(false);
        var refreshed = read switch
        {
            ZLinkOwnerLeaseReadResult.Found found => new Snapshot(
                found.Token,
                found.LeaseExpiresAt,
                found.StoreNow,
                fetchedAt),
            ZLinkOwnerLeaseReadResult.Missing => new Snapshot(
                null,
                DateTimeOffset.MinValue,
                DateTimeOffset.MinValue,
                fetchedAt),
            _ => throw new ArgumentOutOfRangeException(nameof(read))
        };
        lock (_cacheGate)
            _cache[ownerId] = refreshed;
        return refreshed;
    }

    private bool IsUnexpired(Snapshot snapshot) =>
        snapshot.LeaseExpiresAt - snapshot.StoreNow
        - _time.GetElapsedTime(snapshot.FetchedAt) > TimeSpan.Zero;

    private sealed record Snapshot(
        ZLinkLocationOwnerToken? Token,
        DateTimeOffset LeaseExpiresAt,
        DateTimeOffset StoreNow,
        long FetchedAt);
}
