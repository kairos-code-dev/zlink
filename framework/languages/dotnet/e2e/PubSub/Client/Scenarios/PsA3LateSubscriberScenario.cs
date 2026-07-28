// Verifies PS-A3 Late Subscriber behavior.
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
        IReadOnlyList<ZLinkHttpClient> existingSubscribers,
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

        // Keep the publisher transport blocked until the late subscriber's
        // monitor is active, then allow the first ConnectionReady transition.
        await using var connectionGate = new NetworkFaultProxy(
            new Uri(processes.PublisherEndpoint),
            initiallyBlocked: true);
        using var lateSubscriber = processes.StartSubscriber(
            "sub-late",
            lateSubscriberUrl,
            "sub-late.evidence.log",
            connectionGate.Endpoint.GetComponents(
                UriComponents.SchemeAndServer,
                UriFormat.Unescaped));
        try
        {
            await StateObservation.WaitUntilAsync(
                async () =>
                {
                    try
                    {
                        return (await lateSubscriberClient.Get("/health").AsyncRaw()).Status == 200;
                    }
                    catch (Exception ex) when ((ex is HttpRequestException || ex.InnerException is HttpRequestException))
                    {
                        return false;
                    }
                },
                "PS-A3 expected late subscriber to become healthy.");
            connectionGate.Unblock();
            await connectionGate.WaitForUpstreamConnectionAsync();
            var readyRun = $"ready-{Guid.NewGuid():N}";
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", readyRun)
                .Query("sequence", "0")
                .Query("value", "late-subscriber-ready")
                .AsyncRaw();
            await SubscriberObservation.WaitForEventAsync(lateSubscriberClient, readyRun, 0);

            var afterLateRun = Guid.NewGuid().ToString("N");

            // Probe delivery proves the late subscriber joined before this measured publish.
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", afterLateRun)
                .Query("sequence", "1")
                .Query("value", "after-late")
                .AsyncRaw();

            var measuredSubscribers = existingSubscribers.Append(lateSubscriberClient).ToArray();
            var measuredEvidence = await Task.WhenAll(measuredSubscribers.Select(
                subscriber => SubscriberObservation.WaitForEventAsync(subscriber, afterLateRun, 1)));
            var lateEvidence = measuredEvidence[^1];

            // Pub/Sub has no replay contract, so pre-join events must stay absent from late evidence.
            ZlinkStreamAssert.Ensure(
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
