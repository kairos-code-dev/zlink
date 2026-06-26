using PubSub.Shared;
using Zlink.HttpClient;

namespace PubSub.Client.Scenarios;

// PS-A2: verifies that subscribers accept the target topic and ignore non-interest topics.
internal static class TopicFilterScenario
{
    public static async Task RunAsync(ZLinkHttpClient publisher, IReadOnlyList<ZLinkHttpClient> subscribers)
    {
        var runId = Guid.NewGuid().ToString("N");

        // .NET exposes topic filtering at handler level here: one accepted topic and one ignored topic.
        await publisher.Post("/publish/event")
            .Query("topic", PubSubNames.MainTopic)
            .Query("runId", runId)
            .Query("sequence", "1")
            .Query("value", "accepted")
            .SubmitRawAsync();
        await publisher.Post("/publish/event")
            .Query("topic", PubSubNames.OtherTopic)
            .Query("runId", runId)
            .Query("sequence", "2")
            .Query("value", "ignored")
            .SubmitRawAsync();

        // Each subscriber records both the accepted event and the ignored marker for the other topic.
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var snapshots = subscribers
                .Select(subscriber => subscriber.Get("/evidence").Fetch<string[]>())
                .ToArray();
            return Task.FromResult(snapshots.All(lines =>
                    lines.Any(line => Evidence.IsEvent(line, runId, PubSubNames.MainTopic)))
                && snapshots.All(lines =>
                    lines.Any(line => Evidence.IsIgnored(line, runId, PubSubNames.OtherTopic))));
        }, "PS-A2 expected topic-based application filtering evidence.");

        // The non-interest topic must not be recorded as an accepted business event.
        var snapshots = subscribers
            .Select(subscriber => subscriber.Get("/evidence").Fetch<string[]>())
            .ToArray();
        ScenarioAssert.That(
            snapshots.All(lines => lines.Any(line => Evidence.IsEvent(line, runId, PubSubNames.MainTopic))),
            "PS-A2 accepted topic was not recorded.");
        ScenarioAssert.That(
            snapshots.All(lines => lines.All(line =>
                !line.Contains("event|", StringComparison.Ordinal)
                || !line.Contains($"run={runId}", StringComparison.Ordinal)
                || !line.Contains($"topic={PubSubNames.OtherTopic}", StringComparison.Ordinal))),
            "PS-A2 non-interest topic was recorded as accepted.");

        Console.WriteLine("scenario PS-A2 passed");
    }
}
