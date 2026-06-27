using SpotService.Shared;
using Zlink.HttpClient;
using SpotService.Client.Support;

namespace SpotService.Client.Scenarios;

internal static class SmA2Scenario
{
    public static async Task RunAsync(ZLinkHttpClient api, SpotLifecycleOrderContext context)
    {
        var expectedEvidence = new[] { $"spot-state-request|rid=play-a|spot={context.SpotRid}|value={context.CurrentValue}" };
        var evidence = (await api.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(expectedEvidence))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            expectedEvidence.All(expected => evidence.Any(line => line.Contains(expected, StringComparison.Ordinal))),
            "SM-A2 state mutation evidence mismatch.");
        ScenarioAssert.That(
            evidence.Any(line => line.Contains($"spot-state-request|rid=play-a|spot={context.SpotRid}|value={context.CurrentValue}", StringComparison.Ordinal)),
            "SM-A2 state mutation did not preserve order.");
        Console.WriteLine("operation SpotService.sm-a2 passed");
    }
}
