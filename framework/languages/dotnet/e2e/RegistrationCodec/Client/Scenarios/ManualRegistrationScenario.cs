using RegistrationCodec.Shared;
using Zlink.HttpClient;
using RegistrationCodec.Client.Support;

namespace RegistrationCodec.Client.Scenarios;

// RC-A3: verifies manually registered handlers for request and send messages.
internal static class ManualRegistrationScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var reply = (await server.Post("/registration/manual").SubmitAsync<EchoReply>()).Body;
        ScenarioAssert.That(reply.Value == "echo:rc-a3", "RC-A3 request reply mismatch.");

        var evidence = (await server.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(["echo-command|variant=manual|id=cmd-rc-a3"]))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("echo-command|variant=manual|id=cmd-rc-a3", StringComparison.Ordinal)),
            "RC-A3 send evidence missing.");

        Console.WriteLine("scenario RC-A3 passed");
    }
}
