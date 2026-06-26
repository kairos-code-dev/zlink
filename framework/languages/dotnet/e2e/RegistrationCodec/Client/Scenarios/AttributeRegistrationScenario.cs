using RegistrationCodec.Shared;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-A2: verifies attribute-based packet names for request and send handlers.
internal static class AttributeRegistrationScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var reply = (await server.Post("/scenario/rc-a2").SubmitAsync<EchoReply>()).Body;
        ScenarioAssert.That(reply.Value == "echo:rc-a2", "RC-A2 request reply mismatch.");

        // The server records the attribute variant only when dispatch selected the annotated handler.
        await ScenarioAssert.EventuallyAsync(() =>
        {
            var evidence = server.Get("/evidence").Fetch<string[]>();
            return Task.FromResult(evidence.Any(line =>
                line.Contains("echo-command|variant=attr|id=cmd-rc-a2", StringComparison.Ordinal)));
        }, "RC-A2 send evidence missing.");

        Console.WriteLine("scenario RC-A2 passed");
    }
}
