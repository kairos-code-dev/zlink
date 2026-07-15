using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-A1 verifies that an endpoint-less channel client auto-connects from the
// shared location store and that both providers' peer location rows are alive:
// live rows via IZLinkLocationRuntimeQuery.ListPeerLocationsAsync (no cache) and the
// member-peer user surface via IZLinkPeerLocationResolver.ListLivePeersAsync with
// Refresh, both exposed through the provider's /locations endpoints.
internal static class RmA1LocationStoreAutoConnectScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var reply = (await providerA.Post("/profile/request")
            .Body(new ProfileReq("rm-a1"))
            .Async<ProfileRes>()).Body;

        ZlinkStreamAssert.Ensure(reply.Value == "profile:rm-a1", "RM-A1 reply value mismatch.");
        ZlinkStreamAssert.Ensure(reply.ProviderRid is "api-a" or "api-b", "RM-A1 provider rid was not api-a/api-b.");

        var rawPeers = providerA.Get("/locations/peers?mesh=profile").Async<PeerLocationRow[]>().AsTask().GetAwaiter().GetResult().Body;
        var liveProviderRows = rawPeers.Count(row =>
            row.Role == "Router" && row.NodeRid is "api-a" or "api-b");
        ZlinkStreamAssert.Ensure(
            liveProviderRows >= 2,
            "RM-A1 expected live peer location rows for both profile providers in the runtime query.");

        var memberPeers = providerA.Get("/locations/member-peers?mesh=profile").Async<PeerLocationRow[]>().AsTask().GetAwaiter().GetResult().Body;
        ZlinkStreamAssert.Ensure(
            memberPeers.Count(row => row.Role == "Router" && row.NodeRid is "api-a" or "api-b") >= 2,
            "RM-A1 expected both providers on the member-peer resolver surface (Refresh).");

        var status = providerA.Get("/locations/status").Async<LocationStatusRes>().AsTask().GetAwaiter().GetResult().Body;
        ZlinkStreamAssert.Ensure(status.StoreHealthy, "RM-A1 expected a healthy location store.");

        var providerEvidence = providerA.Get("/evidence").Async<string[]>().AsTask().GetAwaiter().GetResult().Body
            .Concat(providerB.Get("/evidence").Async<string[]>().AsTask().GetAwaiter().GetResult().Body)
            .ToArray();
        ZlinkStreamAssert.Ensure(
            providerEvidence.Any(line => line.Contains("value=rm-a1", StringComparison.Ordinal)),
            "RM-A1 provider evidence missing.");
    }
}
