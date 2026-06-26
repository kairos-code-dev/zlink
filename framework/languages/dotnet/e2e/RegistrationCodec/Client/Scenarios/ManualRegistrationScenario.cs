using RegistrationCodec.Shared;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-A3: verifies manually registered handlers for request and send messages.
internal static class ManualRegistrationScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var reply = (await server.Post("/scenario/rc-a3").SubmitAsync<EchoReply>()).Body;
        ScenarioAssert.That(reply.Value == "echo:rc-a3", "RC-A3 request reply mismatch.");

        // Manual send registration is verified through command evidence on the server.
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var evidence = server.Get("/evidence").Fetch<string[]>();
            return Task.FromResult(evidence.Any(line =>
                line.Contains("echo-command|variant=manual|id=cmd-rc-a3", StringComparison.Ordinal)));
        }, "RC-A3 send evidence missing.");

        Console.WriteLine("scenario RC-A3 passed");
    }
}
