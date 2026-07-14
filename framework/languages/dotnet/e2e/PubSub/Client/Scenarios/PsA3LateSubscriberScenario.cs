using PubSub.Client.Support;
using PubSub.Shared;
using Zlink.HttpClient;

namespace PubSub.Client.Scenarios;

// PS-A3: verifies that a subscriber started after publishing receives only post-join events.
internal static class PsA3LateSubscriberScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient publisher,
        ZLinkHttpClient lateSubscriberClient,
        ServerProcessLauncher processes,
        string lateSubscriberUrl)
    {
        var beforeLateRun = Guid.NewGuid().ToString("N");

        // Publish before the late subscriber exists; this range must not be replayed later.
        for (var i = 1; i <= 5; i++)
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", beforeLateRun)
                .Query("sequence", i.ToString())
                .Query("value", $"before-late-{i}")
                .AsyncRaw();

        // Start the late subscriber through the same server executable used by the harness.
        using var lateSubscriber = processes.StartSubscriber(
            "sub-late",
            lateSubscriberUrl,
            "sub-late.evidence.log");
        try
        {
            await ScenarioAssert.EventuallyAsync(
                async () =>
                {
                    try
                    {
                        return (await lateSubscriberClient.Get("/health").AsyncRaw()).Status == 200;
                    }
                    catch (Exception ex) when (ScenarioAssert.IsConnectionFailure(ex))
                    {
                        return false;
                    }
                },
                "PS-A3 expected late subscriber to become healthy.");

            var afterLateRun = Guid.NewGuid().ToString("N");

            // After the late subscriber is healthy, new publishes should be visible to it.
            for (var i = 1; i <= 8; i++)
                await publisher.Post("/publish/event")
                    .Query("topic", PubSubNames.MainTopic)
                    .Query("runId", afterLateRun)
                    .Query("sequence", i.ToString())
                    .Query("value", $"after-late-{i}")
                    .AsyncRaw();

            // The positive check proves the late subscriber joined the fanout after process start.
            var lateEvidence = (await lateSubscriberClient.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(
                    ["event|", $"run={afterLateRun}", $"topic={PubSubNames.MainTopic}"],
                    []))
                .Async<string[]>()).Body;

            // Pub/Sub has no replay contract, so pre-join events must stay absent from late evidence.
            ScenarioAssert.That(
                lateEvidence.All(line => !line.Contains($"run={beforeLateRun}", StringComparison.Ordinal)),
                "PS-A3 late subscriber replayed pre-join events.");
            Console.WriteLine("scenario PS-A3 passed");
        }
        finally
        {
            if (!lateSubscriber.HasExited)
            {
                lateSubscriber.Kill(true);
                await lateSubscriber.WaitForExitAsync();
            }
        }
    }
}
