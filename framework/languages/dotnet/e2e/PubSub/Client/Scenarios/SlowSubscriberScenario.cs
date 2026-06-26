using PubSub.Shared;
using Zlink.HttpClient;

namespace PubSub.Client.Scenarios;

// PS-B1: verifies that a slow subscriber handler does not block other subscribers.
internal static class SlowSubscriberScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient publisher,
        IReadOnlyList<ZLinkHttpClient> fastSubscribers,
        ZLinkHttpClient slowSubscriber)
    {
        var runId = Guid.NewGuid().ToString("N");

        // The first value triggers delay in the configured slow subscriber handler.
        for (var i = 1; i <= 8; i++)
        {
            var value = i == 1 ? "slow-first" : $"fast-after-slow-{i}";
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", i.ToString())
                .Query("value", value)
                .SubmitRawAsync();
        }

        // Fast subscribers should still reach the tail event while the slow handler is delayed.
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var fastSnapshots = fastSubscribers
                .Select(subscriber => subscriber.Get("/evidence").Fetch<string[]>())
                .ToArray();
            return Task.FromResult(fastSnapshots.All(lines => lines.Any(line =>
                Evidence.IsEvent(line, runId, PubSubNames.MainTopic)
                && line.Contains("seq=8", StringComparison.Ordinal))));
        }, "PS-B1 expected fast subscribers to keep receiving while another handler is slow.", timeout: TimeSpan.FromSeconds(2));

        // The slow subscriber evidence proves that this scenario actually exercised the delayed path.
        var slowEvidence = slowSubscriber.Get("/evidence").Fetch<string[]>();
        ScenarioAssert.That(
            slowEvidence.Any(line => line.Contains("delay-start|", StringComparison.Ordinal)
                && line.Contains($"run={runId}", StringComparison.Ordinal)),
            "PS-B1 expected slow subscriber delay evidence.");
        Console.WriteLine("scenario PS-B1 passed");
    }
}
