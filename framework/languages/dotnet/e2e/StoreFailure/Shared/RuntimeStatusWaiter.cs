namespace StoreFailure.Shared;

public static class RuntimeStatusWaiter
{
    public static async Task<RuntimeStatusRes?> WaitAsync(
        Func<CancellationToken, ValueTask<RuntimeStatusRes>> readStatus,
        RuntimeStatusWaitReq request,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow
                       + TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 60000));
        while (DateTimeOffset.UtcNow < deadline)
        {
            var status = await readStatus(cancellationToken);
            if (Matches(status, request)) return status;

            await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
        }

        return null;
    }

    private static bool Matches(RuntimeStatusRes status, RuntimeStatusWaitReq request)
    {
        return (request.StoreHealthy is null || status.StoreHealthy == request.StoreHealthy)
               && (request.OwnerLeaseHealthy is null
                   || status.OwnerLeaseHealthy == request.OwnerLeaseHealthy)
               && (!request.RequireLastError || status.LastError is not null)
               && (!request.RequireLastRefresh || status.LastRefreshAt is not null);
    }
}
