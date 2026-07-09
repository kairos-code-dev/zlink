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
        if (row is null) return null;

        if (!acceptObserved(row)) return null;

        return await leaseTracker.IsOwnerLiveAsync(ownerOf(row), cancellationToken).ConfigureAwait(false)
            ? row
            : null;
    }

    public async ValueTask<IReadOnlyList<TRow>> FilterAsync<TRow>(
        IReadOnlyList<TRow> rows,
        Func<TRow, string> ownerOf,
        CancellationToken cancellationToken)
    {
        var live = new List<TRow>(rows.Count);
        foreach (var row in rows)
        {
            if (await leaseTracker.IsOwnerLiveAsync(ownerOf(row), cancellationToken)
                    .ConfigureAwait(false))
            {
                live.Add(row);
            }
        }

        return live;
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
            if (!acceptObserved(row)) continue;

            if (await leaseTracker.IsOwnerLiveAsync(ownerOf(row), cancellationToken)
                    .ConfigureAwait(false))
            {
                live.Add(row);
            }
        }

        return live;
    }
}
