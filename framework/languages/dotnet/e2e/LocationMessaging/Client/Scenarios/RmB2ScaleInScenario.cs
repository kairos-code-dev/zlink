using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-B2 verifies that after a graceful scale-in the stopped provider's peer
// location row is removed on the shutdown path (no owner lease expiry wait)
// and traffic continues through the remaining provider only.
internal static class RmB2ScaleInScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        await using var cluster = await DynamicClusterLauncher.StartAsync(options, "rm-b2");
        var providerA = await cluster.StartProviderAsync("api-a", "api-a");
        var providerB = await cluster.StartProviderAsync("api-b", "api-b");
        using var requester = ZLinkHttpClient.Create(providerA.HttpUrl)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();
        using var providerBClient = ZLinkHttpClient.Create(providerB.HttpUrl)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();

        var beforeA = await ReadEvidenceAsync(requester);
        var beforeB = await ReadEvidenceAsync(providerBClient);

        // Warm up until both providers actually serve: the requester's
        // reconcile needs a poll tick to dial the second row, and the
        // counted batch below must start from a two-way distribution.
        var warmSeen = new HashSet<string>(StringComparer.Ordinal);
        for (var attempt = 0; attempt < 100 && warmSeen.Count < 2; attempt++)
        {
            var warm = (await requester.Post("/profile/request")
                .Body(new ProfileReq($"rm-b2-warm-{attempt}"))
                .Async<ProfileRes>()).Body;
            warmSeen.Add(warm.ProviderRid);
            if (warmSeen.Count < 2) await Task.Delay(150);
        }

        ScenarioAssert.That(warmSeen.Count == 2, "RM-B2 warm-up never reached both providers.");
        var markerBefore = $"rm-b2-before-{Guid.NewGuid():N}";
        var valuesBefore = Enumerable.Range(0, 40)
            .Select(index => $"{markerBefore}-{index}")
            .ToArray();
        var repliesBefore = new List<ProfileRes>(valuesBefore.Length);
        foreach (var value in valuesBefore)
        {
            var reply = (await requester.Post("/profile/request")
                .Body(new ProfileReq(value))
                .Async<ProfileRes>()).Body;
            repliesBefore.Add(reply);
        }

        ScenarioAssert.That(repliesBefore.Count == valuesBefore.Length, "RM-B2 pre-scale reply count mismatch.");
        ScenarioAssert.That(
            repliesBefore.All(reply => reply.ProviderRid is "api-a" or "api-b"),
            "RM-B2 reply provider mismatch before scale-in.");

        var apiABeforeValues = repliesBefore
            .Where(reply => reply.ProviderRid == "api-a")
            .Select(reply => reply.Value)
            .ToArray();
        var apiBBeforeValues = repliesBefore
            .Where(reply => reply.ProviderRid == "api-b")
            .Select(reply => reply.Value)
            .ToArray();
        ScenarioAssert.That(apiABeforeValues.Length > 0 && apiBBeforeValues.Length > 0,
            "RM-B2 expected both providers before scale-in.");
        var scaleOutA = await WaitEvidenceAsync(requester, apiABeforeValues[^1]);
        var scaleOutB = await WaitEvidenceAsync(providerBClient, apiBBeforeValues[^1]);
        var preA = ScenarioAssert.CountNewEvidence(scaleOutA, beforeA, "profile-request|rid=api-a", markerBefore);
        var preB = ScenarioAssert.CountNewEvidence(scaleOutB, beforeB, "profile-request|rid=api-b", markerBefore);
        ScenarioAssert.That(preA == apiABeforeValues.Length && preB == apiBBeforeValues.Length
                                                            && preA + preB == valuesBefore.Length,
            "RM-B2 expected both providers before scale-in.");

        await cluster.StopAsync(providerB);

        // Graceful shutdown must remove api-b's peer row from the runtime
        // query peer list without waiting for owner lease expiry (doc RM-B2).
        await WaitForPeerRowGoneAsync(requester, "api-b");

        // The reconcile drop of the stopped provider races the next
        // requests: ride out the window until traffic flows cleanly again.
        var settled = 0;
        for (var attempt = 0; attempt < 100 && settled < 3; attempt++)
        {
            try
            {
                var settleReply = (await requester.Post("/profile/request")
                    .Body(new ProfileReq($"rm-b2-settle-{attempt}"))
                    .Async<ProfileRes>()).Body;
                settled = settleReply.ProviderRid == "api-a" ? settled + 1 : 0;
            }
            catch
            {
                settled = 0;
                await Task.Delay(200);
            }
        }

        ScenarioAssert.That(settled >= 3, "RM-B2 traffic did not settle on the surviving provider.");

        beforeA = await ReadEvidenceAsync(requester);
        beforeB = await ReadEvidenceIgnoringStoppedAsync(providerBClient);
        var markerAfter = $"rm-b2-after-{Guid.NewGuid():N}";
        var valuesAfter = Enumerable.Range(0, 20)
            .Select(index => $"{markerAfter}-{index}")
            .ToArray();
        var repliesAfter = new List<ProfileRes>(valuesAfter.Length);
        foreach (var value in valuesAfter)
        {
            var reply = (await requester.Post("/profile/request")
                .Body(new ProfileReq(value))
                .Async<ProfileRes>()).Body;
            repliesAfter.Add(reply);
        }

        ScenarioAssert.That(repliesAfter.Count == valuesAfter.Length, "RM-B2 post-scale reply count mismatch.");
        ScenarioAssert.That(
            repliesAfter.All(reply => reply.ProviderRid == "api-a"),
            "RM-B2 after scale-in should reach api-a only.");

        var afterA = await WaitEvidenceAsync(requester, valuesAfter[^1]);
        var afterB = await ReadEvidenceIgnoringStoppedAsync(providerBClient);
        var a = ScenarioAssert.CountNewEvidence(afterA, beforeA, "profile-request|rid=api-a", markerAfter);
        var b = ScenarioAssert.CountNewEvidence(afterB, beforeB, "profile-request|rid=api-b", markerAfter);
        ScenarioAssert.That(a == valuesAfter.Length && b == 0, "RM-B2 expected only api-a after scale-in.");
    }

    private static async Task WaitForPeerRowGoneAsync(ZLinkHttpClient http, string rid)
    {
        await http.Post("/locations/peers/wait")
            .Body(new PeerLocationWaitReq("profile", "Router", rid, Present: false))
            .Async<PeerLocationRow[]>();
    }

    private static async Task<string[]> ReadEvidenceAsync(ZLinkHttpClient http)
    {
        return (await http.Get("/evidence").Async<string[]>()).Body;
    }

    private static async Task<string[]> ReadEvidenceIgnoringStoppedAsync(ZLinkHttpClient http)
    {
        try
        {
            return await ReadEvidenceAsync(http);
        }
        catch
        {
            return [];
        }
    }

    private static async Task<string[]> WaitEvidenceAsync(ZLinkHttpClient http, string contains)
    {
        return (await http.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(contains))
            .Async<string[]>()).Body;
    }
}
