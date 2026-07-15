using StoreFailure.Client.Support;
using Zlink.HttpClient;

namespace StoreFailure.Client.Scenarios;

// SF-A2: a store registration without the change-stamp surface still
// reflects peer add/remove within polling ticks — polling is the
// correctness path, the stamp only trims latency.
internal static class SfA2PollingFallbackScenario
{
    public static async Task RunAsync(
        ClientOptions options,
        StoreFailureProcessManager processes)
    {
        var consumerNw = await processes.StartConsumerNwAsync();
        using var observer = ZLinkHttpClient.Create(options.ConsumerNwUrl).Build();
        try
        {
            var status = await SfProbe.GetStatusAsync(observer);
            ZlinkStreamAssert.Ensure(
                !status.WatchEnabled,
                "SF-A2: the polling-only consumer unexpectedly reports watch enabled.");

            var providerC = await processes.StartProviderCAsync();
            await SfProbe.WaitPeersAsync(
                observer,
                SfProbe.PeerRows(options.PollingInterval * 8 + options.HeartbeatInterval,
                    present: ["api-c"]),
                "SF-A2: the added provider did not appear through pure polling.");

            await providerC.StopAsync();
            await SfProbe.WaitPeersAsync(
                observer,
                SfProbe.PeerRows(options.PollingInterval * 8 + options.HeartbeatInterval,
                    absent: ["api-c"]),
                "SF-A2: the removed provider did not disappear through pure polling.");
        }
        finally
        {
            await consumerNw.StopAsync();
        }

        Console.WriteLine("scenario SF-A2 passed");
    }
}
