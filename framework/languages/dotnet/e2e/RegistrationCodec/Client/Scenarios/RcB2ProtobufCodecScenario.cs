using RegistrationCodec.Client.Support;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-B2 verifies Protobuf codec round trip.
internal static class RcB2ProtobufCodecScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var result = (await server.Post("/codec/roundtrip").Async<CodecScenarioRes>()).Body;
        ScenarioAssert.That(result.ProtobufValue.Contains("echo:rc-b2", StringComparison.Ordinal),
            "RC-B2 Protobuf reply mismatch.");
        ScenarioAssert.That(
            result.ProtobufValue.Contains("content:application/x-protobuf", StringComparison.Ordinal),
            "RC-B2 Protobuf content type mismatch.");

        Console.WriteLine("scenario RC-B2 passed");
    }
}