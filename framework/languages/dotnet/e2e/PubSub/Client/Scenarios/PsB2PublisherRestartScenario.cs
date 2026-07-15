// Verifies PS-B2 Publisher Restart behavior.
using System.Diagnostics;
using PubSub.Client.Support;
using PubSub.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace PubSub.Client.Scenarios;

// PS-B2: verifies that subscribers receive new events after the publisher process restarts.
internal static class PsB2PublisherRestartScenario
{
    public static async Task<Process> RunAsync(
        ZLinkHttpClient publisher,
        IReadOnlyList<ZLinkHttpClient> subscribers,
        ServerProcessLauncher processes)
    {
        var runId = Guid.NewGuid().ToString("N");

        // Establish a baseline delivery before stopping the publisher process.
        await publisher.Post("/publish/event")
            .Query("topic", PubSubNames.MainTopic)
            .Query("runId", runId)
            .Query("sequence", "1")
            .Query("value", "before-publisher-restart")
            .AsyncRaw();
        await WaitForSubscribersAsync(subscribers, new EvidenceWaitReq(
            ["event|", $"run={runId}", $"topic={PubSubNames.MainTopic}", "seq=1"],
            []));

        // Stop the HTTP publisher server and wait until the client observes the process gap.
        await publisher.Post("/shutdown").AsyncRaw();
        await StateObservation.WaitUntilAsync(
            async () =>
            {
                try
                {
                    return (await publisher.Get("/health").AsyncRaw()).Status != 200;
                }
                catch (Exception ex) when ((ex is HttpRequestException || ex.InnerException is HttpRequestException))
                {
                    return true;
                }
            },
            "PS-B2 expected publisher to stop before restart.");

        // A publish while the process is down should fail at the HTTP boundary.
        await ZlinkStreamAssert.ExpectFailureAsync(async cancellationToken =>
            _ = await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", "2")
                .Query("value", "during-publisher-down")
                .AsyncRaw(cancellationToken));

        // Restart the same publisher role and wait for the health endpoint before measuring recovery.
        var restartedPublisher = processes.StartPublisher();
        await StateObservation.WaitUntilAsync(
            async () =>
            {
                try
                {
                    return (await publisher.Get("/health").AsyncRaw()).Status == 200;
                }
                catch (Exception ex) when ((ex is HttpRequestException || ex.InnerException is HttpRequestException))
                {
                    return false;
                }
            },
            "PS-B2 expected restarted publisher to become healthy.");
        await Task.Delay(500);

        // Send enough post-restart events to give subscribers time to reconnect to the publisher.
        for (var i = 3; i <= 42; i++)
        {
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", i.ToString())
                .Query("value", $"after-publisher-restart-{i}")
                .AsyncRaw();
            await Task.Delay(100);
        }

        // Recovery is proven by post-restart delivery to every subscriber, not by replaying downtime data.
        await WaitForSubscribersAsync(subscribers, new EvidenceWaitReq(
            ["event|", $"run={runId}", $"topic={PubSubNames.MainTopic}"],
            [])
        {
            ContainsAnyLineGroups = Enumerable.Range(20, 23)
                .Select(seq => new[] { $"seq={seq}|", $"run={runId}", $"topic={PubSubNames.MainTopic}" })
                .ToArray()
        });
        Console.WriteLine("scenario PS-B2 passed");
        return restartedPublisher;
    }

    private static async Task WaitForSubscribersAsync(
        IReadOnlyList<ZLinkHttpClient> subscribers,
        EvidenceWaitReq request)
    {
        var waits = subscribers
            .Select(subscriber => subscriber.Post("/evidence/wait").Body(request).Async<string[]>().AsTask())
            .ToArray();
        await Task.WhenAll(waits);
    }
}
