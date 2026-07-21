// Verifies SF-D3 Status Transition behavior.
using StoreFailure.Client.Support;
using StoreFailure.Shared;
using Zlink.HttpClient;

namespace StoreFailure.Client.Scenarios;

// SF-D3: one outage cycle shows up in runtime status as an ordered
// healthy -> unhealthy(+last error, lease failure) -> healthy(+fresh
// refresh) transition.
internal static class SfD3StatusTransitionScenario
{
    public static async Task RunAsync(
        ClientOptions options,
        ZLinkHttpClient consumer,
        StoreFailureProcessManager processes)
    {
        var before = await SfProbe.WaitStatusAsync(
            consumer,
            SfProbe.Status(options.HeartbeatInterval * 6, storeHealthy: true, ownerLeaseHealthy: true),
            "SF-D3: the pre-outage status was not healthy.");

        await processes.PauseStoreAsync();
        RuntimeStatusRes during;
        try
        {
            during = await SfProbe.WaitStatusAsync(
                consumer,
                SfProbe.Status(options.HeartbeatInterval * 8,
                    storeHealthy: false, ownerLeaseHealthy: false, requireLastError: true),
                "SF-D3: the outage did not surface as unhealthy status with a last error.");
        }
        finally
        {
            await processes.UnpauseStoreAsync();
        }

        var after = await SfProbe.WaitStatusAsync(
            consumer,
            SfProbe.Status(options.HeartbeatInterval * 8,
                storeHealthy: true,
                ownerLeaseHealthy: true,
                requireLastRefresh: true,
                lastRefreshAfter: before.LastRefreshAt),
            "SF-D3: status did not return to healthy after recovery.");

        ZlinkStreamAssert.Ensure(
            before.LastRefreshAt is not null && after.LastRefreshAt > before.LastRefreshAt,
            $"SF-D3: the post-recovery refresh timestamp did not advance " +
            $"(before={before.LastRefreshAt:O}, during={during.LastRefreshAt:O}, " +
            $"after={after.LastRefreshAt:O}).");
        ZlinkStreamAssert.Ensure(
            !during.WatchEnabled && during.LastError is not null,
            "SF-D3: outage status fields (watch/polling, last error) were not observable.");

        Console.WriteLine("scenario SF-D3 passed");
    }
}
