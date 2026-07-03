using StoreFailure.Client.Support;
using Zlink.HttpClient;

namespace StoreFailure.Client.Scenarios;

// SF-B1: a store outage must not translate into disconnects — the last
// desired set stays, established messaging keeps working, and the outage
// is visible only through runtime status.
internal static class SfB1FailStaticScenario
{
    public static async Task RunAsync(
        ClientOptions options,
        ZLinkHttpClient consumer,
        StoreFailureProcessManager processes)
    {
        await processes.PauseStoreAsync();
        try
        {
            // Observe for less than the lease TTL: rows cannot have
            // expired yet, so any request failure would be a fail-static
            // violation, not lease bookkeeping.
            await SfProbe.DriveRequestsAsync(
                consumer,
                "sf-b1-outage",
                options.OwnerLeaseTtl * 0.7,
                "SF-B1");

            await SfProbe.WaitStatusAsync(
                consumer,
                status => status is { StoreHealthy: false, LastError: not null },
                options.HeartbeatInterval * 6,
                "SF-B1: the consumer's runtime status did not record the store outage.");
            await SfProbe.WaitStatusAsync(
                consumer,
                status => !status.OwnerLeaseHealthy,
                options.HeartbeatInterval * 6,
                "SF-B1: the consumer's owner lease heartbeat failure did not surface in status.");
        }
        finally
        {
            await processes.UnpauseStoreAsync();
        }

        await SfProbe.WaitStatusAsync(
            consumer,
            status => status is { StoreHealthy: true, OwnerLeaseHealthy: true },
            options.HeartbeatInterval * 8,
            "SF-B1: the consumer's runtime status did not recover after the outage.");

        Console.WriteLine("scenario SF-B1 passed");
    }
}
