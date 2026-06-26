using RegistrationCodec.Shared;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-A4/RC-A5: verifies DI lifetimes and before/after filter ordering for dispatch.
internal static class DiLifecycleAndFilterOrderingScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var replies = (await server.Post("/scenario/rc-a4-a5").SubmitAsync<EchoReply[]>()).Body;
        var first = replies[0];
        var second = replies[1];
        ScenarioAssert.That(
            first.Value == "echo:rc-a4-1" && second.Value == "echo:rc-a4-2",
            "RC-A4 DI reply mismatch.");

        // Two dispatches should reuse singleton dependencies and create distinct scoped instances.
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var evidence = server.Get("/evidence").Fetch<string[]>();
            var di = evidence.Where(line => line.Contains("di|", StringComparison.Ordinal)).ToArray();
            if (di.Length < 2)
            {
                return Task.FromResult(false);
            }

            var singletonIds = di.Select(line => EvidenceText.ExtractValue(line, "singleton"))
                .Distinct(StringComparer.Ordinal)
                .Count();
            var scopedIds = di.Select(line => EvidenceText.ExtractValue(line, "scoped"))
                .Distinct(StringComparer.Ordinal)
                .Count();
            return Task.FromResult(singletonIds == 1 && scopedIds >= 2);
        }, "RC-A4 expected stable singleton and per-dispatch scoped dependencies.");

        // Filter evidence is ordered by nested before/after execution around the handler.
        var lines = server.Get("/evidence").Fetch<string[]>();
        var filter = lines
            .Where(line => line.Contains("filter|", StringComparison.Ordinal)
                && line.Contains("packet=EchoDi", StringComparison.Ordinal))
            .Take(4)
            .ToArray();
        ScenarioAssert.That(filter.Length == 4, "RC-A5 filter evidence missing.");
        ScenarioAssert.That(
            filter[0].Contains("name=first|phase=before", StringComparison.Ordinal)
            && filter[1].Contains("name=second|phase=before", StringComparison.Ordinal)
            && filter[2].Contains("name=second|phase=after", StringComparison.Ordinal)
            && filter[3].Contains("name=first|phase=after", StringComparison.Ordinal),
            "RC-A5 filter ordering mismatch.");

        Console.WriteLine("scenario RC-A4 passed");
        Console.WriteLine("scenario RC-A5 passed");
    }
}
