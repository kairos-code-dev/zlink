using Zlink.HttpClient;
using RegistryMessaging.Client;
using RegistryMessaging.Shared;
using System.Security.Cryptography;
using System.Text;

namespace RegistryMessaging.Client.Scenarios;

// RM-C8 verifies payload size round trips by checking returned length and hash
// for several message sizes.
internal static class RmC8PayloadRoundTripScenario
{
    public static async Task RunAsync(ZLinkHttpClient singleConsumer, ZLinkHttpClient providerA)
    {
        var before = providerA.Get("/evidence").Fetch<string[]>();
        var markers = new List<string>();
        foreach (var size in new[] { 1, 4096, 256 * 1024, 1024 * 1024 })
        {
            var marker = $"rm-c8-{size}-{Guid.NewGuid():N}";
            markers.Add(marker);
            var payload = BuildPayload(size);
            var expectedHash = HashPayload(payload);
            var reply = (await singleConsumer.Post("/profile/payload")
                .Body(new PayloadRequest(marker, payload))
                .SubmitAsync<PayloadReply>()).Body;
            ScenarioAssert.That(reply.Marker == marker, "RM-C8 marker mismatch.");
            ScenarioAssert.That(reply.Length == payload.Length, "RM-C8 payload length mismatch.");
            ScenarioAssert.That(reply.Sha256 == expectedHash, "RM-C8 payload hash mismatch.");
        }

        var followUp = (await singleConsumer.Post("/profile/request")
            .Body(new ProfileRequest("rm-c8-after"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(followUp.Value == "profile:rm-c8-after", "RM-C8 follow-up request failed.");

        var after = providerA.Get("/evidence").Fetch<string[]>();
        ScenarioAssert.That(
            markers.All(marker => ScenarioAssert.CountNewEvidence(after, before, "payload-request|rid=api-a", marker) == 1),
            "RM-C8 payload evidence missing.");
        Console.WriteLine("scenario RM-C8 passed");
    }

    static string BuildPayload(int size)
    {
        var builder = new StringBuilder(size);
        for (var i = 0; i < size; i++)
        {
            builder.Append((char)('a' + (i % 26)));
        }

        return builder.ToString();
    }

    static string HashPayload(string payload)
    {
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(payload)));
    }
}
