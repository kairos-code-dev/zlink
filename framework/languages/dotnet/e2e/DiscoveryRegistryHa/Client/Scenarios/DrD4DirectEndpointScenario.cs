using DiscoveryRegistryHa.Client;
using DiscoveryRegistryHa.Shared;
using System.Text.Json;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-D4 compares the same registry router endpoint through the registry app's
// in-process query endpoint and a separate probe app's remote query endpoint.
internal static class DrD4DirectEndpointScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var registry = CreateClient(options.Reg1Url);
        using var probe = CreateClient(options.ProbeUrl);

        await registry.Post("/registry/topology/wait")
            .Body(new TopologyReadyWaitRequest(1))
            .SubmitRawAsync();
        await probe.Post("/registry/topology/wait")
            .Body(new TopologyReadyWaitRequest(1))
            .SubmitRawAsync();

        var local = Normalize(await ReadJsonArrayAsync(registry, "/registry/topology"));
        var remote = Normalize(await ReadJsonArrayAsync(probe, "/registry/topology"));
        ScenarioAssert.That(
            local.Length > 0 && local.SequenceEqual(remote, StringComparer.Ordinal),
            "DR-D4 in-process and remote topology snapshots did not match.");

        Console.WriteLine("scenario DR-D4 passed");
    }

    static ZLinkHttpClient CreateClient(string baseUrl) =>
        ZLinkHttpClient.Create(baseUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

    static async Task<JsonElement[]> ReadJsonArrayAsync(ZLinkHttpClient client, string path) =>
        (await client.Get(path).SubmitAsync<JsonElement[]>()).Body;

    static string[] Normalize(IEnumerable<JsonElement> entries) =>
        entries.Select(entry => string.Join("|",
                ReadJsonValue(entry, "channelName"),
                ReadJsonValue(entry, "routingId"),
                ReadJsonValue(entry, "endpoint"),
                ReadJsonValue(entry, "state"),
                ReadJsonValue(entry, "serviceRole")))
            .Order(StringComparer.Ordinal)
            .ToArray();

    static string ReadJsonValue(JsonElement element, string name) =>
        element.TryGetProperty(name, out var property)
            ? property.ToString()
            : "";

}
