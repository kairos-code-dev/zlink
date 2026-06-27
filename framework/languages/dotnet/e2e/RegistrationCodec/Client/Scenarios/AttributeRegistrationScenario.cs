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

        await EvidenceWait.ForAllAsync(
            server,
            ["echo-command|variant=attr|id=cmd-rc-a2"],
            "RC-A2 send evidence missing.");

        Console.WriteLine("scenario RC-A2 passed");
    }
}
