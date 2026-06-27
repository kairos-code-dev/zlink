using RegistrationCodec.Shared;
using Zlink.HttpClient;

namespace RegistrationCodec.Client.Scenarios;

// RC-B1..RC-B4: verifies JSON, Protobuf, MessagePack, and their coexistence on one channel.
internal static class CodecRoundTripScenario
{
    public static async Task RunAsync(ZLinkHttpClient server)
    {
        var result = (await server.Post("/codec/roundtrip").SubmitAsync<CodecScenarioResult>()).Body;
        ScenarioAssert.That(result.Json.Value == "echo:rc-b1", "RC-B1 JSON reply mismatch.");
        ScenarioAssert.That(result.Json.ContentType == "application/json", "RC-B1 JSON content type mismatch.");
        ScenarioAssert.That(result.ProtobufValue.Contains("echo:rc-b2", StringComparison.Ordinal), "RC-B2 Protobuf reply mismatch.");
        ScenarioAssert.That(
            result.ProtobufValue.Contains("content:application/x-protobuf", StringComparison.Ordinal),
            "RC-B2 Protobuf content type mismatch.");
        ScenarioAssert.That(result.MessagePackValue.Contains("echo:rc-b3", StringComparison.Ordinal), "RC-B3 MessagePack reply mismatch.");
        ScenarioAssert.That(
            result.MessagePackValue.Contains("content:application/x-msgpack", StringComparison.Ordinal),
            "RC-B3 MessagePack content type mismatch.");
        await EvidenceWait.ForAsync(
            server,
            new EvidenceWaitRequest(["codec-request|codec=json", "codec-request|codec=protobuf", "codec-request|codec=msgpack"]),
            evidence => EvidenceText.HasCodec(evidence, "json", "application/json")
                && EvidenceText.HasCodec(evidence, "protobuf", "application/x-protobuf")
                && EvidenceText.HasCodec(evidence, "msgpack", "application/x-msgpack"),
            "RC-B4 expected all codec evidence.");

        Console.WriteLine("scenario RC-B1 passed");
        Console.WriteLine("scenario RC-B2 passed");
        Console.WriteLine("scenario RC-B3 passed");
        Console.WriteLine("scenario RC-B4 passed");
    }
}

internal sealed record CodecScenarioResult(EchoReply Json, string ProtobufValue, string MessagePackValue);
