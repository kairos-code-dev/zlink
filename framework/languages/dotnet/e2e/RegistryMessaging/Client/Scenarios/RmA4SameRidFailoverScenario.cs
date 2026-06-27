using Zlink.HttpClient;
using RegistryMessaging.Shared;
using RegistryMessaging.Client.Support;

namespace RegistryMessaging.Client.Scenarios;

// RM-A4 verifies that discovery keeps using the same logical provider rid
// after the backing provider process is replaced with a new endpoint.
internal static class RmA4SameRidFailoverScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        await using var cluster = await DynamicClusterLauncher.StartAsync(options, "rm-a4");
        var providerV1 = await cluster.StartProviderAsync("api-a-v1", "api-a");
        using var providerV1Client = ZLinkHttpClient.Create(providerV1.HttpUrl)
            .Json()
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();

        var first = (await providerV1Client.Post("/profile/request")
            .Body(new ProfileRequest("rm-a4-v1"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(
            first.ProviderRid == "api-a",
            "RM-A4 initial request should reach api-a.");

        await WaitForEvidenceAsync(providerV1Client, "value=rm-a4-v1");

        await cluster.StopAsync(providerV1);

        var providerV2 = await cluster.StartProviderAsync("api-a-v2", "api-a");
        using var providerV2Client = ZLinkHttpClient.Create(providerV2.HttpUrl)
            .Json()
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();

        var beforeV1 = await ReadEvidenceIgnoringStoppedAsync(providerV1Client);
        var beforeV2 = await ReadEvidenceAsync(providerV2Client);
        var marker = $"rm-a4-{Guid.NewGuid():N}";
        for (var i = 0; i < 20; i++)
        {
            var reply = (await providerV2Client.Post("/profile/request")
                .Body(new ProfileRequest($"{marker}-{i}"))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(
                reply.ProviderRid == "api-a",
                "RM-A4 replacement request should reach api-a.");
        }

        var afterV2 = await WaitForEvidenceAsync(providerV2Client, $"{marker}-19");
        var afterV1 = await ReadEvidenceIgnoringStoppedAsync(providerV1Client);
        var v1Count = ScenarioAssert.CountNewEvidence(
            afterV1,
            beforeV1,
            "profile-request|rid=api-a",
            marker);
        var v2Count = ScenarioAssert.CountNewEvidence(
            afterV2,
            beforeV2,
            "profile-request|rid=api-a",
            marker);
        ScenarioAssert.That(v1Count == 0 && v2Count == 20, "RM-A4 replacement provider evidence did not match.");

        Console.WriteLine("scenario RM-A4 passed");
    }

    static async Task<string[]> ReadEvidenceAsync(ZLinkHttpClient client) =>
        (await client.Get("/evidence").SubmitAsync<string[]>()).Body;

    static async Task<string[]> ReadEvidenceIgnoringStoppedAsync(ZLinkHttpClient client)
    {
        try
        {
            return await ReadEvidenceAsync(client);
        }
        catch
        {
            return [];
        }
    }

    static async Task<string[]> WaitForEvidenceAsync(ZLinkHttpClient client, string contains) =>
        (await client.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(contains))
            .SubmitAsync<string[]>()).Body;
}
