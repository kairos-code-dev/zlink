using PubSub.Client.Support;
using PubSub.Shared;
using Zlink.HttpClient;

namespace PubSub.Client.Scenarios;

// PS-A4: verifies that a restarted subscriber resumes delivery without replaying the disconnect gap.
internal static class PsA4SubscriberReconnectScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient publisher,
        ZLinkHttpClient reconnectSubscriberClient,
        IReadOnlyList<ZLinkHttpClient> fastSubscribers,
        ServerProcessLauncher processes,
        string reconnectSubscriberUrl)
    {
        var runId = Guid.NewGuid().ToString("N");

        // First prove the extra subscriber can receive before it is disconnected.
        using (var subscriber = processes.StartSubscriber(
                   "sub-reconnect",
                   reconnectSubscriberUrl,
                   "sub-reconnect.evidence.log"))
        {
            await ScenarioAssert.EventuallyAsync(
                async () =>
                {
                    try
                    {
                        return (await reconnectSubscriberClient.Get("/health").AsyncRaw()).Status == 200;
                    }
                    catch (Exception ex) when (ScenarioAssert.IsConnectionFailure(ex))
                    {
                        return false;
                    }
                },
                "PS-A4 expected reconnect subscriber to become healthy before disconnect.");
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", "1")
                .Query("value", "before-disconnect")
                .AsyncRaw();
            await WaitForSubscriberAsync(reconnectSubscriberClient, runId);

            subscriber.Kill(true);
            await subscriber.WaitForExitAsync();
        }

        // Wait until the endpoint is gone before publishing the no-replay gap range.
        await ScenarioAssert.EventuallyAsync(
            async () =>
            {
                try
                {
                    return (await reconnectSubscriberClient.Get("/health").AsyncRaw()).Status != 200;
                }
                catch (Exception ex) when (ScenarioAssert.IsConnectionFailure(ex))
                {
                    return true;
                }
            },
            "PS-A4 expected reconnect subscriber to leave before gap publish.");

        // These events are intentionally published while the reconnecting subscriber is absent.
        for (var i = 2; i <= 4; i++)
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", i.ToString())
                .Query("value", $"gap-{i}")
                .AsyncRaw();

        // The two always-on subscribers must continue receiving while the extra subscriber is down.
        await WaitForSubscribersAsync(fastSubscribers, runId);

        // Restart the same logical subscriber and verify it receives only the post-reconnect range.
        using var restartedSubscriber = processes.StartSubscriber(
            "sub-reconnect",
            reconnectSubscriberUrl,
            "sub-reconnect.evidence.log");
        await ScenarioAssert.EventuallyAsync(
            async () =>
            {
                try
                {
                    return (await reconnectSubscriberClient.Get("/health").AsyncRaw()).Status == 200;
                }
                catch (Exception ex) when (ScenarioAssert.IsConnectionFailure(ex))
                {
                    return false;
                }
            },
            "PS-A4 expected reconnect subscriber to become healthy after reconnect.");

        for (var i = 5; i <= 8; i++)
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", i.ToString())
                .Query("value", $"after-reconnect-{i}")
                .AsyncRaw();

        var reconnectEvidence = await WaitForSubscriberAsync(reconnectSubscriberClient, runId);

        // Reconnect should not replay events that were published during the disconnect gap.
        ZlinkStreamAssert.Ensure(
            reconnectEvidence.All(line =>
                !line.Contains($"run={runId}", StringComparison.Ordinal)
                || !line.Contains("value=gap-", StringComparison.Ordinal)),
            "PS-A4 reconnected subscriber replayed disconnect-gap events.");
        Console.WriteLine("scenario PS-A4 passed");

        restartedSubscriber.Kill(true);
        await restartedSubscriber.WaitForExitAsync();
    }

    private static async Task<string[]> WaitForSubscriberAsync(ZLinkHttpClient subscriber, string runId)
    {
        return (await subscriber.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                ["event|", $"run={runId}", $"topic={PubSubNames.MainTopic}"],
                []))
            .Async<string[]>()).Body;
    }

    private static async Task WaitForSubscribersAsync(IReadOnlyList<ZLinkHttpClient> subscribers, string runId)
    {
        var waits = subscribers.Select(subscriber => WaitForSubscriberAsync(subscriber, runId)).ToArray();
        await Task.WhenAll(waits);
    }
}
