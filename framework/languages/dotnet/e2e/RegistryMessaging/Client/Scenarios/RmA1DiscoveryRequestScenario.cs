using Zlink.HttpClient;
using RegistryMessaging.Client;
using RegistryMessaging.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Registry;

namespace RegistryMessaging.Client.Scenarios;

// RM-A1 verifies that a role server endpoint can request the profile channel
// through registry discovery and receive a reply from one of the advertised providers.
internal static class RmA1DiscoveryRequestScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        ZLinkHttpClient registry)
    {
        var reply = (await providerA.Post("/profile/request")
            .Body(new ProfileRequest("rm-a1"))
            .SubmitAsync<ProfileReply>()).Body;

        ScenarioAssert.That(reply.Value == "profile:rm-a1", "RM-A1 reply value mismatch.");
        ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b", "RM-A1 provider rid was not api-a/api-b.");

        var topology = registry.Get("/registry/topology").Fetch<ZLinkRegistryTopologyEntry[]>();
        var readyProfileProviders = topology.Count(entry =>
            entry.ChannelName == "profile"
            && entry.ServiceRole == ZLinkServiceRole.Router
            && entry.State == ZLinkTopologyState.Ready);
        ScenarioAssert.That(readyProfileProviders >= 2, "RM-A1 expected two ready profile providers in topology.");

        var providerEvidence = providerA.Get("/evidence").Fetch<string[]>()
            .Concat(providerB.Get("/evidence").Fetch<string[]>())
            .ToArray();
        ScenarioAssert.That(
            providerEvidence.Any(line => line.Contains("value=rm-a1", StringComparison.Ordinal)),
            "RM-A1 provider evidence missing.");
        Console.WriteLine("scenario RM-A1 passed");
    }
}
