using SpotService.Client;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB7Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-b7-order-{Guid.NewGuid():N}";
        var replies = await SpotActorRequestSupport.RunBoundActorRequestsWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq(actorId, "order", "play-a"),
            [new ActorPingReq("order-1"), new ActorPingReq("order-2")],
            "SM-B7 ordered actor requests did not become routable.");
        ScenarioAssert.That(
            replies[0].Value == "order-1" && replies[0].Seen == 1
                && replies[1].Value == "order-2" && replies[1].Seen == 2,
            "SM-B7 stream replies did not preserve actor packet order.");
        await EvidenceWait.ForAsync(
            playA,
            new EvidenceWaitRequest([$"actor-ping|rid=play-a|actor={actorId}", "value=order-2|seen=2"]),
            evidence => evidence.Any(line =>
                line.Contains($"actor-ping|rid=play-a|actor={actorId}", StringComparison.Ordinal)
                && line.Contains("value=order-2|seen=2", StringComparison.Ordinal)),
            "SM-B7 evidence did not include ordered second request.");
        Console.WriteLine("operation SpotService.sm-b7 passed");
    }
}
