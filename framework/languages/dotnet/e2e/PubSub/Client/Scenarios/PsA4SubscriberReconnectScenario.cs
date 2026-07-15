// Verifies PS-A4 Subscriber Reconnect behavior.
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
        var activationRunId = Guid.NewGuid().ToString("N");

        // Activate the lazily-created publisher socket before waiting for a newly dialed subscriber.
        await publisher.Post("/publish/event")
            .Query("topic", PubSubNames.MainTopic)
            .Query("runId", activationRunId)
            .Query("sequence", "1")
            .Query("value", "activate-publisher")
            .AsyncRaw();
        await WaitForSubscribersAsync(fastSubscribers, activationRunId, 1);

        // First prove the extra subscriber can receive before it is disconnected.
        using (var subscriber = processes.StartSubscriber(
                   "sub-reconnect",
                   reconnectSubscriberUrl,
                   "sub-reconnect.evidence.log"))
        {
            await StateObservation.WaitUntilAsync(
                async () =>
                {
                    try
                    {
                        return (await reconnectSubscriberClient.Get("/health").AsyncRaw()).Status == 200;
                    }
                    catch (Exception ex) when ((ex is HttpRequestException || ex.InnerException is HttpRequestException))
                    {
                        return false;
                    }
                },
                "PS-A4 expected reconnect subscriber to become healthy before disconnect.");
            await SubscriberObservation.WaitForConnectionAsync(reconnectSubscriberClient);
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", "1")
                .Query("value", "before-disconnect")
                .AsyncRaw();
            await WaitForSubscribersAsync(
                fastSubscribers.Append(reconnectSubscriberClient).ToArray(),
                runId,
                1);

            var publisherEvidenceOffset = await SubscriberObservation.EvidenceCountAsync(publisher);
            subscriber.Kill(true);
            await subscriber.WaitForExitAsync();
            await SubscriberObservation.WaitForSocketEventAsync(
                publisher,
                PubSubNames.PublisherSocketSource,
                "Disconnected",
                publisherEvidenceOffset);
        }

        // Wait until the endpoint is gone before publishing the no-replay gap range.
        await StateObservation.WaitUntilAsync(
            async () =>
            {
                try
                {
                    return (await reconnectSubscriberClient.Get("/health").AsyncRaw()).Status != 200;
                }
                catch (Exception ex) when ((ex is HttpRequestException || ex.InnerException is HttpRequestException))
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

        // Every always-on subscriber must receive the last event published during the gap.
        await WaitForSubscribersAsync(fastSubscribers, runId, 4);

        // Restart the same logical subscriber and verify it receives only the post-reconnect range.
        using var restartedSubscriber = processes.StartSubscriber(
            "sub-reconnect",
            reconnectSubscriberUrl,
            "sub-reconnect.evidence.log");
        try
        {
            await StateObservation.WaitUntilAsync(
                async () =>
                {
                    try
                    {
                        return (await reconnectSubscriberClient.Get("/health").AsyncRaw()).Status == 200;
                    }
                    catch (Exception ex) when ((ex is HttpRequestException
                                                || ex.InnerException is HttpRequestException))
                    {
                        return false;
                    }
                },
                "PS-A4 expected reconnect subscriber to become healthy after reconnect.");
            await SubscriberObservation.WaitForConnectionAsync(reconnectSubscriberClient);

            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", "5")
                .Query("value", "after-reconnect")
                .AsyncRaw();

            await WaitForSubscribersAsync(
                fastSubscribers.Append(reconnectSubscriberClient).ToArray(),
                runId,
                5);
            var reconnectEvidence = (await reconnectSubscriberClient.Get("/evidence")
                .Async<string[]>()).Body;

            // Reconnect should not replay events that were published during the disconnect gap.
            ZlinkStreamAssert.Ensure(
                reconnectEvidence.All(line =>
                    !line.Contains($"run={runId}", StringComparison.Ordinal)
                    || !line.Contains("value=gap-", StringComparison.Ordinal)),
                "PS-A4 reconnected subscriber replayed disconnect-gap events.");
            Console.WriteLine("scenario PS-A4 passed");
        }
        finally
        {
            if (!restartedSubscriber.HasExited)
            {
                restartedSubscriber.Kill(true);
                await restartedSubscriber.WaitForExitAsync();
            }
        }
    }

    private static async Task WaitForSubscribersAsync(
        IReadOnlyList<ZLinkHttpClient> subscribers,
        string runId,
        int sequence)
    {
        var waits = subscribers
            .Select(subscriber => SubscriberObservation.WaitForEventAsync(subscriber, runId, sequence))
            .ToArray();
        await Task.WhenAll(waits);
    }
}
