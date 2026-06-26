using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-B5: verifies a codec mismatch failure through the server app endpoint, then JSON recovery.
internal static class CodecMismatchScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        await server.Post("/scenario/rc-b5")
            .Timeout(TimeSpan.FromSeconds(15))
            .SubmitRawAsync();
        Console.WriteLine("scenario RC-B5 passed");
    }
}
