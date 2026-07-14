using RegistrationCodec.Client.Support;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-B1 verifies JSON codec round trip.
internal static class RcB1JsonCodecScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var result = (await server.Post("/codec/roundtrip").Async<CodecScenarioRes>()).Body;
        ScenarioAssert.That(result.Json.Value == "echo:rc-b1", "RC-B1 JSON reply mismatch.");
        ScenarioAssert.That(result.Json.ContentType == "application/json", "RC-B1 JSON content type mismatch.");

        Console.WriteLine("scenario RC-B1 passed");
    }
}