using PubSub.Shared;
using Zlink.HttpClient;

namespace PubSub.Client.Support;

internal static class SubscriberObservation
{
    public static async Task<int> EvidenceCountAsync(ZLinkHttpClient subscriber)
    {
        return (await subscriber.Get("/evidence").Async<string[]>()).Body.Length;
    }

    public static async Task WaitForConnectionAsync(
        ZLinkHttpClient subscriber,
        int afterIndex = 0)
    {
        await StateObservation.WaitUntilAsync(
            async () =>
            {
                var entries = (await subscriber.Get("/evidence").Async<string[]>()).Body;
                return entries.Skip(afterIndex).Any(line =>
                    line.Contains("socket|", StringComparison.Ordinal)
                    && line.Contains(
                        $"source={PubSubNames.SubscriberSocketSource}",
                        StringComparison.Ordinal)
                    && (line.Contains("event=Connected", StringComparison.Ordinal)
                        || line.Contains("event=ConnectionReady", StringComparison.Ordinal)));
            },
            "Subscriber socket did not become ready within the local readiness bound.");
    }

    public static async Task WaitForSocketEventAsync(
        ZLinkHttpClient role,
        string source,
        string eventName,
        int afterIndex)
    {
        await StateObservation.WaitUntilAsync(
            async () =>
            {
                var entries = (await role.Get("/evidence").Async<string[]>()).Body;
                return entries.Skip(afterIndex).Any(line =>
                    line.Contains("socket|", StringComparison.Ordinal)
                    && line.Contains($"source={source}", StringComparison.Ordinal)
                    && line.Contains($"event={eventName}", StringComparison.Ordinal));
            },
            $"Socket event {eventName} did not arrive within the local readiness bound.");
    }

    public static async Task WaitForSocketEvidenceAsync(
        ZLinkHttpClient role,
        string source,
        string eventName,
        int afterIndex)
    {
        _ = await role.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([], [], TimeoutMilliseconds: 8000)
            {
                AfterIndex = afterIndex,
                ContainsAllLineGroups =
                [
                    ["socket|", $"source={source}", $"event={eventName}"]
                ]
            })
            .Timeout(TimeSpan.FromSeconds(9))
            .Async<string[]>();
    }

    public static async Task<string[]> WaitForEventAsync(
        ZLinkHttpClient subscriber,
        string runId,
        int sequence)
    {
        return (await subscriber.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([], [])
            {
                ContainsAllLineGroups =
                [
                    [
                        "event|",
                        $"run={runId}",
                        $"topic={PubSubNames.MainTopic}",
                        $"seq={sequence}|"
                    ]
                ]
            })
            .Async<string[]>()).Body;
    }
}
