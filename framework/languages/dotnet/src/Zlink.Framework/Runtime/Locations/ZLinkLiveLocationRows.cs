namespace Zlink.Framework.Runtime.Locations;

internal sealed class ZLinkLiveLocationRows(ZLinkOwnerLeaseTracker leaseTracker)
{
    public async ValueTask<TRow?> ResolveAsync<TRow>(
        TRow? row,
        Func<TRow, string> ownerOf,
        Func<TRow, bool> acceptObserved,
        CancellationToken cancellationToken)
        where TRow : class
    {
        var (resolved, _) = await ResolveWithPresenceAsync(
                row,
                ownerOf,
                acceptObserved,
                cancellationToken)
            .ConfigureAwait(false);
        return resolved;
    }

    public async ValueTask<(TRow? Row, bool LiveRowPresent)> ResolveWithPresenceAsync<TRow>(
        TRow? row,
        Func<TRow, string> ownerOf,
        Func<TRow, bool> acceptObserved,
        CancellationToken cancellationToken)
        where TRow : class
    {
        if (row is null) return (null, false);

        // Liveness gates the observation: a dead-owner row must never record a
        // generation floor, or the successor incarnation's fresh row (whose
        // axes legitimately restart) would be rejected as a lagging replica.
        if (!await leaseTracker.IsOwnerLiveAsync(ownerOf(row), cancellationToken).ConfigureAwait(false))
            return (null, false);

        return (acceptObserved(row) ? row : null, true);
    }

    public async ValueTask<IReadOnlyList<TRow>> FilterAsync<TRow>(
        IReadOnlyList<TRow> rows,
        Func<TRow, string> ownerOf,
        Func<TRow, bool> acceptObserved,
        CancellationToken cancellationToken)
    {
        var live = new List<TRow>(rows.Count);
        foreach (var row in rows)
        {
            if (!await leaseTracker.IsOwnerLiveAsync(ownerOf(row), cancellationToken)
                    .ConfigureAwait(false))
                continue;

            if (acceptObserved(row)) live.Add(row);
        }

        return live;
    }
}
