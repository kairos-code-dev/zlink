using RegistrationCodec.Client;
using RegistrationCodec.Shared;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-A4 verifies DI lifetimes for dispatch.
internal static class RcA4DiLifecycleScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var replies = (await server.Post("/registration/di-filter-order").SubmitAsync<EchoReply[]>()).Body;
        var first = replies[0];
        var second = replies[1];
        ScenarioAssert.That(
            first.Value == "echo:rc-a4-1" && second.Value == "echo:rc-a4-2",
            "RC-A4 DI reply mismatch.");

        await EvidenceWait.ForAsync(
            server,
            new EvidenceWaitRequest(["di|value=rc-a4-1", "di|value=rc-a4-2"]),
            evidence =>
            {
                var di = evidence.Where(line => line.Contains("di|", StringComparison.Ordinal)).ToArray();
                if (di.Length < 2)
                {
                    return false;
                }

                var singletonIds = di.Select(line => EvidenceText.ExtractValue(line, "singleton"))
                    .Distinct(StringComparer.Ordinal)
                    .Count();
                var scopedIds = di.Select(line => EvidenceText.ExtractValue(line, "scoped"))
                    .Distinct(StringComparer.Ordinal)
                    .Count();
                return singletonIds == 1 && scopedIds >= 2;
            },
            "RC-A4 expected stable singleton and per-dispatch scoped dependencies.");

        Console.WriteLine("scenario RC-A4 passed");
    }
}
