using RegistrationCodec.Shared;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-A3: verifies manually registered handlers for request and send messages.
internal static class ManualRegistrationScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var reply = (await server.Post("/registration/manual").SubmitAsync<EchoReply>()).Body;
        ScenarioAssert.That(reply.Value == "echo:rc-a3", "RC-A3 request reply mismatch.");

        await EvidenceWait.ForAllAsync(
            server,
            ["echo-command|variant=manual|id=cmd-rc-a3"],
            "RC-A3 send evidence missing.");

        Console.WriteLine("scenario RC-A3 passed");
    }
}
