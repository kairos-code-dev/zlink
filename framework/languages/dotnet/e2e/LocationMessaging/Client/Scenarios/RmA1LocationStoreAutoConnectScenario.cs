using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-A1 verifies that an endpoint-less channel client auto-connects from the
// shared location store and that both providers' peer location rows are alive:
// raw rows via IZLinkLocationRuntimeQuery.ListPeersAsync (no cache) and the
// member-peer user surface via IZLinkPeerLocationResolver.ListPeersAsync with
// Refresh, both exposed through the provider's /locations endpoints.
internal static class RmA1LocationStoreAutoConnectScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var reply = (await providerA.Post("/profile/request")
            .Body(new ProfileReq("rm-a1"))
            .SubmitAsync<ProfileRes>()).Body;

        ScenarioAssert.That(reply.Value == "profile:rm-a1", "RM-A1 reply value mismatch.");
        ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b", "RM-A1 provider rid was not api-a/api-b.");

        var rawPeers = providerA.Get("/locations/peers?mesh=profile").Fetch<PeerLocationRow[]>();
        var liveProviderRows = rawPeers.Count(row =>
            row.Role == "router" && row.NodeRid is "api-a" or "api-b");
        ScenarioAssert.That(
            liveProviderRows >= 2,
            "RM-A1 expected live peer location rows for both profile providers in the runtime query.");

        var memberPeers = providerA.Get("/locations/member-peers?mesh=profile").Fetch<PeerLocationRow[]>();
        ScenarioAssert.That(
            memberPeers.Count(row => row.Role == "router" && row.NodeRid is "api-a" or "api-b") >= 2,
            "RM-A1 expected both providers on the member-peer resolver surface (Refresh).");

        var status = providerA.Get("/locations/status").Fetch<LocationStatusRes>();
        ScenarioAssert.That(status.StoreHealthy, "RM-A1 expected a healthy location store.");

        var providerEvidence = providerA.Get("/evidence").Fetch<string[]>()
            .Concat(providerB.Get("/evidence").Fetch<string[]>())
            .ToArray();
        ScenarioAssert.That(
            providerEvidence.Any(line => line.Contains("value=rm-a1", StringComparison.Ordinal)),
            "RM-A1 provider evidence missing.");
        Console.WriteLine("scenario RM-A1 passed");
    }
}
