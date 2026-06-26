using PubSub.Shared;
using System.Diagnostics;
using Zlink.HttpClient;

namespace PubSub.Client.Scenarios;

// PS-B2: verifies that subscribers receive new events after the publisher process restarts.
internal static class PublisherRestartScenario
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
            .SubmitRawAsync();
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var snapshots = subscribers
                .Select(subscriber => subscriber.Get("/evidence").Fetch<string[]>())
                .ToArray();
            return Task.FromResult(snapshots.All(lines =>
                lines.Any(line => Evidence.IsEvent(line, runId, PubSubNames.MainTopic))));
        }, "PS-B2 expected baseline publish before publisher restart.");

        // Stop the HTTP publisher server and wait until the client observes the process gap.
        await publisher.Post("/shutdown").SubmitRawAsync();
        await ScenarioAssert.EventuallyAsync(
            async () =>
            {
                try
                {
                    return (await publisher.Get("/health").SubmitRawAsync()).Status != 200;
                }
                catch (Exception ex) when (ScenarioAssert.IsConnectionFailure(ex))
                {
                    return true;
                }
            },
            "PS-B2 expected publisher to stop before restart.");

        // A publish while the process is down should fail at the HTTP boundary.
        try
        {
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", "2")
                .Query("value", "during-publisher-down")
                .SubmitRawAsync();
            throw new InvalidOperationException("PS-B2 expected publish attempt to fail while publisher is down.");
        }
        catch (Exception ex) when (ScenarioAssert.IsConnectionFailure(ex))
        {
        }

        // Restart the same publisher role and wait for the health endpoint before measuring recovery.
        var restartedPublisher = processes.StartPublisher();
        await ScenarioAssert.EventuallyAsync(
            async () =>
            {
                try
                {
                    return (await publisher.Get("/health").SubmitRawAsync()).Status == 200;
                }
                catch (Exception ex) when (ScenarioAssert.IsConnectionFailure(ex))
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
                .SubmitRawAsync();
            await Task.Delay(100);
        }

        // Recovery is proven by post-restart delivery to every subscriber, not by replaying downtime data.
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var snapshots = subscribers
                .Select(subscriber => subscriber.Get("/evidence").Fetch<string[]>())
                .ToArray();
            return Task.FromResult(snapshots.All(lines => lines.Any(line =>
                Evidence.IsEvent(line, runId, PubSubNames.MainTopic)
                && Evidence.ExtractInt(line, "seq") >= 20)));
        }, "PS-B2 expected publish delivery after publisher restart.");
        Console.WriteLine("scenario PS-B2 passed");
        return restartedPublisher;
    }
}
