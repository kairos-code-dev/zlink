using RegistrationCodec.Client.Support;
using RegistrationCodec.Shared;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-A2: verifies attribute-based packet names for request and send handlers.
internal static class AttributeRegistrationScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var reply = (await server.Post("/registration/attribute").SubmitAsync<EchoReply>()).Body;
        ScenarioAssert.That(reply.Value == "echo:rc-a2", "RC-A2 request reply mismatch.");

        var evidence = (await server.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(["echo-command|variant=attr|id=cmd-rc-a2"]))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("echo-command|variant=attr|id=cmd-rc-a2", StringComparison.Ordinal)),
            "RC-A2 send evidence missing.");

        Console.WriteLine("scenario RC-A2 passed");
    }
}