using RegistrationCodec.Shared;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-A1: verifies assembly/module auto registration for request and send handlers.
internal static class AutoRegistrationScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var reply = (await server.Post("/registration/auto").SubmitAsync<EchoReply>()).Body;
        ScenarioAssert.That(reply.Value == "echo:rc-a1", "RC-A1 request reply mismatch.");

        await EvidenceWait.ForAllAsync(
            server,
            ["echo-command|variant=auto|id=cmd-rc-a1"],
            "RC-A1 send evidence missing.");

        Console.WriteLine("scenario RC-A1 passed");
    }
}
