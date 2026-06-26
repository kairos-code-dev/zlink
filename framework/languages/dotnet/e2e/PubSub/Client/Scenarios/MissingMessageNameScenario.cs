using PubSub.Shared;
using Zlink.HttpClient;

namespace PubSub.Client.Scenarios;

// PS-C1: verifies that an unregistered publish message is dropped and normal delivery continues.
internal static class MissingMessageNameScenario
{
    public static async Task RunAsync(ZLinkHttpClient publisher, IReadOnlyList<ZLinkHttpClient> subscribers)
    {
        var runId = Guid.NewGuid().ToString("N");

        // Publish a packet name that subscribers did not register, so dispatch should drop it.
        await publisher.Post("/publish/missing")
            .Query("topic", PubSubNames.MainTopic)
            .Query("runId", runId)
            .Query("sequence", "1")
            .Query("value", "missing")
            .SubmitRawAsync();

        // The drop is observed on subscriber evidence because publisher submit only reaches transport.
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var snapshots = subscribers
                .Select(subscriber => subscriber.Get("/evidence").Fetch<string[]>())
                .ToArray();
            return Task.FromResult(snapshots.All(lines => lines.Any(line =>
                line.Contains("dispatch-error|", StringComparison.Ordinal)
                && line.Contains("packet=MissingEventNotify", StringComparison.Ordinal)
                && line.Contains($"topic={PubSubNames.MainTopic}", StringComparison.Ordinal))));
        }, "PS-C1 expected subscriber dispatch-error evidence for missing publish handler.");

        // A normal event after the missing handler case proves the channel continues to work.
        await publisher.Post("/publish/event")
            .Query("topic", PubSubNames.MainTopic)
            .Query("runId", runId)
            .Query("sequence", "2")
            .Query("value", "after-missing")
            .SubmitRawAsync();
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var snapshots = subscribers
                .Select(subscriber => subscriber.Get("/evidence").Fetch<string[]>())
                .ToArray();
            return Task.FromResult(snapshots.All(lines =>
                lines.Any(line => Evidence.IsEvent(line, runId, PubSubNames.MainTopic))));
        }, "PS-C1 expected normal publish after missing handler to keep working.");

        Console.WriteLine("scenario PS-C1 passed");
    }
}
