using PubSub.Shared;
using Zlink.HttpClient;

namespace PubSub.Client.Scenarios;

// PS-A4: verifies that a restarted subscriber resumes delivery without replaying the disconnect gap.
internal static class SubscriberReconnectScenario
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
            name: "sub-reconnect",
            httpUrl: reconnectSubscriberUrl,
            evidenceFile: "sub-reconnect.evidence.log"))
        {
            await ScenarioAssert.EventuallyAsync(
                async () =>
                {
                    try
                    {
                        return (await reconnectSubscriberClient.Get("/health").SubmitRawAsync()).Status == 200;
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
                .SubmitRawAsync();
            await ScenarioAssert.EventuallyAsync(() =>
            {
                var evidence = reconnectSubscriberClient.Get("/evidence").Fetch<string[]>();
                return Task.FromResult(evidence.Any(line => Evidence.IsEvent(line, runId, PubSubNames.MainTopic)));
            }, "PS-A4 expected subscriber to receive before disconnect.");

            subscriber.Kill(entireProcessTree: true);
            await subscriber.WaitForExitAsync();
        }

        // Wait until the endpoint is gone before publishing the no-replay gap range.
        await ScenarioAssert.EventuallyAsync(
            async () =>
            {
                try
                {
                    return (await reconnectSubscriberClient.Get("/health").SubmitRawAsync()).Status != 200;
                }
                catch (Exception ex) when (ScenarioAssert.IsConnectionFailure(ex))
                {
                    return true;
                }
            },
            "PS-A4 expected reconnect subscriber to leave before gap publish.");

        // These events are intentionally published while the reconnecting subscriber is absent.
        for (var i = 2; i <= 4; i++)
        {
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", i.ToString())
                .Query("value", $"gap-{i}")
                .SubmitRawAsync();
        }

        // The two always-on subscribers must continue receiving while the extra subscriber is down.
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var fastSnapshots = fastSubscribers
                .Select(subscriber => subscriber.Get("/evidence").Fetch<string[]>())
                .ToArray();
            return Task.FromResult(fastSnapshots.All(lines =>
                lines.Any(line => Evidence.IsEvent(line, runId, PubSubNames.MainTopic))));
        }, "PS-A4 expected other subscribers to keep receiving while one subscriber is disconnected.");

        // Restart the same logical subscriber and verify it receives only the post-reconnect range.
        using var restartedSubscriber = processes.StartSubscriber(
            name: "sub-reconnect",
            httpUrl: reconnectSubscriberUrl,
            evidenceFile: "sub-reconnect.evidence.log");
        await ScenarioAssert.EventuallyAsync(
            async () =>
            {
                try
                {
                    return (await reconnectSubscriberClient.Get("/health").SubmitRawAsync()).Status == 200;
                }
                catch (Exception ex) when (ScenarioAssert.IsConnectionFailure(ex))
                {
                    return false;
                }
                },
            "PS-A4 expected reconnect subscriber to become healthy after reconnect.");

        for (var i = 5; i <= 8; i++)
        {
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", i.ToString())
                .Query("value", $"after-reconnect-{i}")
                .SubmitRawAsync();
        }

        await ScenarioAssert.EventuallyAsync(() =>
        {
            var evidence = reconnectSubscriberClient.Get("/evidence").Fetch<string[]>();
            return Task.FromResult(evidence.Any(line => Evidence.IsEvent(line, runId, PubSubNames.MainTopic)));
        }, "PS-A4 expected reconnected subscriber to receive post-reconnect events.");

        // Reconnect should not replay events that were published during the disconnect gap.
        var reconnectEvidence = reconnectSubscriberClient.Get("/evidence").Fetch<string[]>();
        ScenarioAssert.That(
            reconnectEvidence.All(line =>
                !line.Contains($"run={runId}", StringComparison.Ordinal)
                || !line.Contains("value=gap-", StringComparison.Ordinal)),
            "PS-A4 reconnected subscriber replayed disconnect-gap events.");
        Console.WriteLine("scenario PS-A4 passed");

        restartedSubscriber.Kill(entireProcessTree: true);
        await restartedSubscriber.WaitForExitAsync();
    }
}
