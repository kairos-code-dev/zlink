using System.Security.Cryptography;
using System.Text;
using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-C8 verifies payload size round trips by checking returned length and hash
// for several message sizes.
internal static class RmC8PayloadRoundTripScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient directConsumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var beforeA = providerA.Get("/evidence").Async<string[]>().AsTask().GetAwaiter().GetResult().Body;
        var beforeB = providerB.Get("/evidence").Async<string[]>().AsTask().GetAwaiter().GetResult().Body;
        var markers = new List<string>();
        foreach (var size in new[] { 1, 4096, 256 * 1024, 1024 * 1024 })
        {
            var marker = $"rm-c8-{size}-{Guid.NewGuid():N}";
            markers.Add(marker);
            var payload = BuildPayload(size);
            var expectedHash = HashPayload(payload);
            var reply = (await directConsumer.Post("/profile/payload")
                .Body(new PayloadReq(marker, payload))
                .Async<PayloadRes>()).Body;
            ZlinkStreamAssert.Ensure(reply.Marker == marker, "RM-C8 marker mismatch.");
            ZlinkStreamAssert.Ensure(reply.Length == payload.Length, "RM-C8 payload length mismatch.");
            ZlinkStreamAssert.Ensure(reply.Sha256 == expectedHash, "RM-C8 payload hash mismatch.");
        }

        var oversizedMarker = $"rm-c8-over-limit-{Guid.NewGuid():N}";
        var oversized = (await directConsumer.Post("/profile/payload-over-limit")
            .Body(new PayloadReq(oversizedMarker, BuildPayload(3 * 1024 * 1024)))
            .Async<RequestFailureRes>()).Body;
        ZlinkStreamAssert.Ensure(oversized.Failed, "RM-C8 oversized payload should fail.");

        var followUp = (await directConsumer.Post("/profile/request")
            .Body(new ProfileReq("rm-c8-after"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(followUp.Value == "profile:rm-c8-after", "RM-C8 follow-up request failed.");

        var afterA = providerA.Get("/evidence").Async<string[]>().AsTask().GetAwaiter().GetResult().Body;
        var afterB = providerB.Get("/evidence").Async<string[]>().AsTask().GetAwaiter().GetResult().Body;
        ZlinkStreamAssert.Ensure(
            markers.All(marker =>
                ScenarioAssert.CountNewEvidence(afterA, beforeA, "payload-request|rid=api-a", marker)
                + ScenarioAssert.CountNewEvidence(afterB, beforeB, "payload-request|rid=api-b", marker) == 1),
            "RM-C8 payload evidence missing.");
    }

    private static string BuildPayload(int size)
    {
        var builder = new StringBuilder(size);
        for (var i = 0; i < size; i++) builder.Append((char)('a' + i % 26));

        return builder.ToString();
    }

    private static string HashPayload(string payload)
    {
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(payload)));
    }
}
