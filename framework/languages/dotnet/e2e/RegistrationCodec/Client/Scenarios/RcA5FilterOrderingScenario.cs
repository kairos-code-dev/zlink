using RegistrationCodec.Client.Support;
using RegistrationCodec.Shared;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-A5 verifies before/after filter ordering for dispatch.
internal static class RcA5FilterOrderingScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        await server.Post("/registration/di-filter-order").Async<EchoRes[]>();
        var lines = (await server.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(["filter|", "packet=EchoDi"]))
            .Async<string[]>()).Body;
        ScenarioAssert.That(
            lines.Count(line => line.Contains("filter|", StringComparison.Ordinal)
                                && line.Contains("packet=EchoDi", StringComparison.Ordinal)) >= 4,
            "RC-A5 filter evidence missing.");

        var filter = lines
            .Where(line => line.Contains("filter|", StringComparison.Ordinal)
                           && line.Contains("packet=EchoDi", StringComparison.Ordinal))
            .Take(4)
            .ToArray();
        ScenarioAssert.That(
            filter[0].Contains("name=first|phase=before", StringComparison.Ordinal)
            && filter[1].Contains("name=second|phase=before", StringComparison.Ordinal)
            && filter[2].Contains("name=second|phase=after", StringComparison.Ordinal)
            && filter[3].Contains("name=first|phase=after", StringComparison.Ordinal),
            "RC-A5 filter ordering mismatch.");

        Console.WriteLine("scenario RC-A5 passed");
    }
}