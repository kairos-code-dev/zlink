using RegistrationCodec.Shared;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-A1: verifies assembly/module auto registration for request and send handlers.
internal static class AutoRegistrationScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var reply = (await server.Post("/scenario/rc-a1").SubmitAsync<EchoReply>()).Body;
        ScenarioAssert.That(reply.Value == "echo:rc-a1", "RC-A1 request reply mismatch.");

        // Server evidence proves the fire-and-forget command reached the auto-registered handler.
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var evidence = server.Get("/evidence").Fetch<string[]>();
            return Task.FromResult(evidence.Any(line =>
                line.Contains("echo-command|variant=auto|id=cmd-rc-a1", StringComparison.Ordinal)));
        }, "RC-A1 send evidence missing.");

        Console.WriteLine("scenario RC-A1 passed");
    }
}
