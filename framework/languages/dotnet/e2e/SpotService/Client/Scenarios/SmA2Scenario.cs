using SpotService.Client;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmA2Scenario
{
    public static async Task RunAsync(ZLinkHttpClient api, SpotLifecycleOrderContext context)
    {
        var evidence = await EvidenceWait.ForAllAsync(
            api,
            [
                $"spot-state-request|rid=play-a|spot={context.SpotRid}|value={context.CurrentValue}",
            ],
            "SM-A2 state mutation evidence mismatch.");
        ScenarioAssert.That(
            evidence.Any(line => line.Contains($"spot-state-request|rid=play-a|spot={context.SpotRid}|value={context.CurrentValue}", StringComparison.Ordinal)),
            "SM-A2 state mutation did not preserve order.");
        Console.WriteLine("operation SpotService.sm-a2 passed");
    }
}
