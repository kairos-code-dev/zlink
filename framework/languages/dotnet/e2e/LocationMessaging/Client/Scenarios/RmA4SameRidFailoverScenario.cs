// Verifies RM-A4 Same Rid Failover behavior.
using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-A4 verifies that the location store keeps the same logical provider rid
// after the backing provider process is replaced with a new endpoint: the
// runtime query peer list must show a single live api-a row at the new
// endpoint before traffic is resumed (peer handover, stale endpoint avoided).
internal static class RmA4SameRidFailoverScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        await using var cluster = await DynamicClusterLauncher.StartAsync(options, "rm-a4");
        var providerV1 = await cluster.StartProviderAsync("api-a-v1", "api-a");
        var consumer = await cluster.StartConsumerAsync("consumer");
        using var providerV1Client = ZLinkHttpClient.Create(providerV1.HttpUrl)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();
        using var observer = ZLinkHttpClient.Create(consumer.HttpUrl)
            .Timeout(TimeSpan.FromSeconds(40))
            .Build();

        await WaitForPeerAsync(observer, "api-a", present: true, providerV1.ChannelEndpoint);

        var first = (await providerV1Client.Post("/profile/request")
            .Body(new ProfileReq("rm-a4-v1"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(
            first.ProviderRid == "api-a",
            "RM-A4 initial request should reach api-a.");

        await WaitForEvidenceAsync(providerV1Client, "value=rm-a4-v1");

        var drained = await cluster.StopAsync(providerV1);
        ZlinkStreamAssert.Ensure(
            drained is { Result: "Drained", Reason: null },
            $"RM-A4 v1 did not reach terminal Drained: {drained.Result}/{drained.Reason}.");
        await WaitForPeerAsync(observer, "api-a", present: false);

        var providerV2 = await cluster.StartProviderAsync("api-a-v2", "api-a");
        using var providerV2Client = ZLinkHttpClient.Create(providerV2.HttpUrl)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();

        // Wait until the runtime query's peer list shows the api-a rid served
        // by exactly one live row whose endpoint is v2's (doc RM-A4).
        await WaitForSingleLiveRowAsync(providerV2Client, providerV2.ChannelEndpoint);

        var beforeV1 = await ReadEvidenceIgnoringStoppedAsync(providerV1Client);
        var beforeV2 = await ReadEvidenceAsync(providerV2Client);
        var marker = $"rm-a4-{Guid.NewGuid():N}";
        for (var i = 0; i < 20; i++)
        {
            var reply = (await providerV2Client.Post("/profile/request")
                .Body(new ProfileReq($"{marker}-{i}"))
                .Async<ProfileRes>()).Body;
            ZlinkStreamAssert.Ensure(
                reply.ProviderRid == "api-a",
                "RM-A4 replacement request should reach api-a.");
        }

        var afterV2 = await WaitForEvidenceAsync(providerV2Client, $"{marker}-19");
        var afterV1 = await ReadEvidenceIgnoringStoppedAsync(providerV1Client);
        var v1Count = EvidenceDelta.CountMatching(
            afterV1,
            beforeV1,
            "profile-request|rid=api-a",
            marker);
        var v2Count = EvidenceDelta.CountMatching(
            afterV2,
            beforeV2,
            "profile-request|rid=api-a",
            marker);
        ZlinkStreamAssert.Ensure(v1Count == 0 && v2Count == 20, "RM-A4 replacement provider evidence did not match.");
    }

    private static async Task WaitForSingleLiveRowAsync(ZLinkHttpClient client, string expectedEndpoint)
    {
        await client.Post("/locations/peers/wait")
            .Body(new PeerLocationWaitReq(
                "profile",
                "Router",
                "api-a",
                Present: true,
                Endpoint: expectedEndpoint))
            .Async<PeerLocationRow[]>();
    }

    private static Task WaitForPeerAsync(
        ZLinkHttpClient client,
        string rid,
        bool present,
        string? endpoint = null) =>
        client.Post("/locations/peers/wait")
            .Body(new PeerLocationWaitReq(
                "profile",
                "Router",
                rid,
                present,
                Endpoint: endpoint))
            .Async<PeerLocationRow[]>()
            .AsTask();

    private static async Task<string[]> ReadEvidenceAsync(ZLinkHttpClient client)
    {
        return (await client.Get("/evidence").Async<string[]>()).Body;
    }

    private static async Task<string[]> ReadEvidenceIgnoringStoppedAsync(ZLinkHttpClient client)
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

    private static async Task<string[]> WaitForEvidenceAsync(ZLinkHttpClient client, string contains)
    {
        return (await client.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(contains))
            .Async<string[]>()).Body;
    }
}
