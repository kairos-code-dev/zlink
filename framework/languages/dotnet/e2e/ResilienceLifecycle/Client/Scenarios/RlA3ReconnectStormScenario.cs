// Verifies RL-A3 Reconnect Storm behavior.
using ResilienceLifecycle.Client.Support;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-A3 verifies many reconnecting requests return to normal request flow.
internal static class RlA3ReconnectStormScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        foreach (var index in Enumerable.Range(0, 24))
        {
            var marker = $"rl-a3-{index}";
            var reply = (await consumer.Post("/profile/request/new-client")
                .Body(new ProfileReq("fast", marker))
                .Async<ProfileRes>()).Body;
            ZlinkStreamAssert.Ensure(
                reply.Value == "profile:fast" && reply.ProviderRid is "api-a" or "api-b",
                "RL-A3 storm request returned an unexpected reply.");
        }

        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            var waitA = providerA.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(["marker=rl-a3-"], []))
                .Async<string[]>(timeout.Token).AsTask();
            var waitB = providerB.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(["marker=rl-a3-"], []))
                .Async<string[]>(timeout.Token).AsTask();
            var completed = await Task.WhenAny(waitA, waitB);
            var evidence = (await completed).Body;
            timeout.Cancel();
            ZlinkStreamAssert.Ensure(
                evidence.Any(line => line.Contains("marker=rl-a3-", StringComparison.Ordinal)),
                "RL-A3 did not record expected provider evidence.");
        }

        Console.WriteLine("scenario RL-A3 passed");
    }
}