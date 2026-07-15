using System.Diagnostics;
using StoreFailure.Client.Support;
using Zlink.HttpClient;

namespace StoreFailure.Client.Scenarios;

// SF-C1: a SIGKILLed provider leaves its row behind, but the owner lease
// expiring is enough to drop the row from live results and to make the
// consumer stop routing there.
internal static class SfC1CrashLeaseExpiryScenario
{
    public static async Task RunAsync(
        ClientOptions options,
        ZLinkHttpClient consumer,
        StoreFailureProcessManager processes,
        ManagedProcess providerB)
    {
        await providerB.KillAsync();

        await SfProbe.WaitPeersAsync(
            consumer,
            SfProbe.PeerRows(options.OwnerLeaseTtl * 2 + options.PollingInterval * 4,
                absent: ["api-b"]),
            "SF-C1: the crashed provider's row was not excluded after its lease expired.");

        // Give the consumer's reconcile a tick to drop the dead target,
        // then verify traffic lands on the survivor only — and fast (no
        // repeated timeouts against the dead endpoint).
        await Task.Delay(options.PollingInterval * 4);
        var stopwatch = Stopwatch.StartNew();
        for (var i = 0; i < 12; i++)
        {
            var reply = await SfProbe.RequestAsync(consumer, $"sf-c1-{i}");
            ZlinkStreamAssert.Ensure(
                reply.ProviderRid == "api-a",
                $"SF-C1: request {i} was served by '{reply.ProviderRid}' instead of the survivor.");
        }

        stopwatch.Stop();
        ZlinkStreamAssert.Ensure(
            stopwatch.Elapsed < TimeSpan.FromSeconds(12),
            "SF-C1: follow-up requests were slow, suggesting repeated timeouts against the dead endpoint.");

        Console.WriteLine("scenario SF-C1 passed");
    }
}
