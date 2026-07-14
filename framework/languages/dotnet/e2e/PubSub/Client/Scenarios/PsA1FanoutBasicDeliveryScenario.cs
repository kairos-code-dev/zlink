using PubSub.Client.Support;
using PubSub.Shared;
using Zlink.HttpClient;

namespace PubSub.Client.Scenarios;

// PS-A1: verifies that one publisher fanouts a shared event range to every subscriber.
internal static class PsA1FanoutBasicDeliveryScenario
{
    public static async Task RunAsync(ZLinkHttpClient publisher, IReadOnlyList<ZLinkHttpClient> subscribers)
    {
        var runId = Guid.NewGuid().ToString("N");
        var warmupStart = 1;

        // There is no public "subscribe ready" event, so the first observed delivery is the barrier.
        for (var i = warmupStart; i < warmupStart + 20; i++)
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", i.ToString())
                .Query("value", $"warmup-{i}")
                .AsyncRaw();

        // Wait until every subscriber has proven that it is attached to the fanout stream.
        await WaitForAllSubscribersAsync(
            subscribers,
            new EvidenceWaitReq(
                ["event|", $"run={runId}", $"topic={PubSubNames.MainTopic}"],
                []));

        var measureStart = 100;
        var measureCount = 12;

        // The measured range is separate from warm-up so the fanout oracle uses only post-barrier data.
        for (var i = measureStart; i < measureStart + measureCount; i++)
            await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", i.ToString())
                .Query("value", $"measure-{i}")
                .AsyncRaw();

        // Publish submit does not promise remote delivery, so the scenario checks a shared contiguous range.
        var measuredSnapshots = await WaitForAllSubscribersAsync(
            subscribers,
            new EvidenceWaitReq(
                ["event|", $"run={runId}", $"topic={PubSubNames.MainTopic}"],
                [
                    [$"seq={measureStart}|"],
                    [$"seq={measureStart + 1}|"],
                    [$"seq={measureStart + 2}|"]
                ]));
        ScenarioAssert.That(
            Evidence.CommonContiguousSequence(
                measuredSnapshots,
                runId,
                PubSubNames.MainTopic,
                measureStart,
                measureStart + measureCount - 1).Count >= 3,
            "PS-A1 expected common contiguous sequence on all subscribers.");

        Console.WriteLine("scenario PS-A1 passed");
    }

    private static async Task<string[][]> WaitForAllSubscribersAsync(
        IReadOnlyList<ZLinkHttpClient> subscribers,
        EvidenceWaitReq request)
    {
        var waits = subscribers
            .Select(subscriber => subscriber.Post("/evidence/wait").Body(request).Async<string[]>().AsTask())
            .ToArray();
        var responses = await Task.WhenAll(waits);
        return responses.Select(response => response.Body).ToArray();
    }
}
