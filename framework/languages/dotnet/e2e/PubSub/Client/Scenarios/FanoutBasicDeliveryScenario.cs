using PubSub.Shared;
using Zlink.HttpClient;

namespace PubSub.Client.Scenarios;

// PS-A1: verifies that one publisher fanouts a shared event range to every subscriber.
internal static class FanoutBasicDeliveryScenario
{
    public static async Task RunAsync(ZLinkHttpClient publisher, IReadOnlyList<ZLinkHttpClient> subscribers)
    {
        var runId = Guid.NewGuid().ToString("N");
        var warmupStart = 1;

        // There is no public "subscribe ready" event, so the first observed delivery is the barrier.
        for (var i = warmupStart; i < warmupStart + 20; i++)
        {
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", i.ToString())
                .Query("value", $"warmup-{i}")
                .SubmitRawAsync();
        }

        // Wait until every subscriber has proven that it is attached to the fanout stream.
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var snapshots = subscribers
                .Select(subscriber => subscriber.Get("/evidence").Fetch<string[]>())
                .ToArray();
            return Task.FromResult(snapshots.All(lines =>
                lines.Any(line => Evidence.IsEvent(line, runId, PubSubNames.MainTopic))));
        }, "PS-A1 expected all initial subscribers to receive warm-up events.");

        var measureStart = 100;
        var measureCount = 12;

        // The measured range is separate from warm-up so the fanout oracle uses only post-barrier data.
        for (var i = measureStart; i < measureStart + measureCount; i++)
        {
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", i.ToString())
                .Query("value", $"measure-{i}")
                .SubmitRawAsync();
        }

        // Publish submit does not promise remote delivery, so the scenario checks a shared contiguous range.
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var snapshots = subscribers
                .Select(subscriber => subscriber.Get("/evidence").Fetch<string[]>())
                .ToArray();
            return Task.FromResult(Evidence.CommonContiguousSequence(
                snapshots,
                runId,
                PubSubNames.MainTopic,
                measureStart,
                measureStart + measureCount - 1).Count >= 3);
        }, "PS-A1 expected common contiguous sequence on all subscribers.");

        Console.WriteLine("scenario PS-A1 passed");
    }
}
