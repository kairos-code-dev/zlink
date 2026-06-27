using RegistrationCodec.Shared;
using Zlink.HttpClient;
using RegistrationCodec.Client.Support;

namespace RegistrationCodec.Client.Scenarios;

// RC-A1: verifies assembly/module auto registration for request and send handlers.
internal static class AutoRegistrationScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var reply = (await server.Post("/registration/auto").SubmitAsync<EchoReply>()).Body;
        ScenarioAssert.That(reply.Value == "echo:rc-a1", "RC-A1 request reply mismatch.");

        var evidence = (await server.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(["echo-command|variant=auto|id=cmd-rc-a1"]))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("echo-command|variant=auto|id=cmd-rc-a1", StringComparison.Ordinal)),
            "RC-A1 send evidence missing.");

        Console.WriteLine("scenario RC-A1 passed");
    }
}
