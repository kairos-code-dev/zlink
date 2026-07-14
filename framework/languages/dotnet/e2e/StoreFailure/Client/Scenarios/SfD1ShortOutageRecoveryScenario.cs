using StoreFailure.Client.Support;
using Zlink.HttpClient;

namespace StoreFailure.Client.Scenarios;

// SF-D1: an outage shorter than the lease TTL passes without any harm —
// the first successful lookup resumes reconcile, no connection is torn
// down, and requests never miss a beat.
internal static class SfD1ShortOutageRecoveryScenario
{
    public static async Task RunAsync(
        ClientOptions options,
        ZLinkHttpClient consumer,
        StoreFailureProcessManager processes)
    {
        var traffic = Task.Run(() => SfProbe.DriveRequestsAsync(
            consumer,
            "sf-d1",
            options.OwnerLeaseTtl * 2,
            "SF-D1"));

        await processes.PauseStoreAsync();
        try
        {
            await Task.Delay(options.OwnerLeaseTtl * 0.5);
        }
        finally
        {
            await processes.UnpauseStoreAsync();
        }

        // Every request across pause, outage, and recovery must succeed.
        await traffic;

        await SfProbe.WaitStatusAsync(
            consumer,
            SfProbe.Status(options.HeartbeatInterval * 8, storeHealthy: true, ownerLeaseHealthy: true),
            "SF-D1: runtime status did not return to healthy after the short outage.");
        await SfProbe.WaitPeersAsync(
            consumer,
            SfProbe.PeerRows(options.OwnerLeaseTtl + options.HeartbeatInterval * 4,
                present: ["api-a", "api-b"]),
            "SF-D1: provider rows were not all live after the short outage.");

        Console.WriteLine("scenario SF-D1 passed");
    }
}
