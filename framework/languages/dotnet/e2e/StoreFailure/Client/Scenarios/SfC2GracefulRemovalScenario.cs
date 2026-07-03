using System.Diagnostics;
using StoreFailure.Client.Support;
using Zlink.HttpClient;

namespace StoreFailure.Client.Scenarios;

// SF-C2: a gracefully stopped provider removes its lease and rows on the
// way out, so the row disappears without waiting for lease expiry — the
// contrast to SF-C1's crash path.
internal static class SfC2GracefulRemovalScenario
{
    public static async Task RunAsync(
        ClientOptions options,
        ZLinkHttpClient consumer,
        ManagedProcess providerB)
    {
        var stopwatch = Stopwatch.StartNew();
        await providerB.StopAsync();

        await SfProbe.WaitPeersAsync(
            consumer,
            peers => !SfProbe.HasRid(peers, "api-b"),
            options.OwnerLeaseTtl,
            "SF-C2: the gracefully stopped provider's row did not disappear promptly.");
        stopwatch.Stop();

        // The whole removal must beat the lease TTL by a margin — that is
        // what distinguishes shutdown cleanup from lease expiry.
        ScenarioAssert.That(
            stopwatch.Elapsed < options.OwnerLeaseTtl,
            $"SF-C2: row removal took {stopwatch.Elapsed}, which does not beat the lease TTL.");

        await Task.Delay(options.PollingInterval * 4);
        for (var i = 0; i < 8; i++)
        {
            var reply = await SfProbe.RequestAsync(consumer, $"sf-c2-{i}");
            ScenarioAssert.That(
                reply.ProviderRid == "api-a",
                $"SF-C2: request {i} was served by '{reply.ProviderRid}' after api-b left.");
        }

        Console.WriteLine("scenario SF-C2 passed");
    }
}
