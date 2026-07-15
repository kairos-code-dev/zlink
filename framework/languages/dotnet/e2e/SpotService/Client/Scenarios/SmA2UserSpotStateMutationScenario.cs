using SpotService.Client.Support;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmA2UserSpotStateMutationScenario
{
    public static async Task RunAsync(ZLinkHttpClient api, SpotLifecycleOrderContext context)
    {
        var expectedEvidence = new[]
            { $"spot-state-request|rid=play-a|spot={context.SpotRid}|value={context.CurrentValue}" };
        var evidence = (await api.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(expectedEvidence))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            expectedEvidence.All(expected => evidence.Any(line => line.Contains(expected, StringComparison.Ordinal))),
            "SM-A2 state mutation evidence mismatch.");
        ZlinkStreamAssert.Ensure(
            evidence.Any(line =>
                line.Contains($"spot-state-request|rid=play-a|spot={context.SpotRid}|value={context.CurrentValue}",
                    StringComparison.Ordinal)),
            "SM-A2 state mutation did not preserve order.");
        Console.WriteLine("operation SpotService.sm-a2 passed");
    }
}
