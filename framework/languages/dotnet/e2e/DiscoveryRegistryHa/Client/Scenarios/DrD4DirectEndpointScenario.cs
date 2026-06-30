using System.Text.Json;
using DiscoveryRegistryHa.Client.Support;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-D4 compares the same registry router endpoint through the registry app's
// in-process query endpoint and a separate probe app's remote query endpoint.
internal static class DrD4DirectEndpointScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var registry = ZLinkHttpClient.Create(options.Reg1Url)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var probe = ZLinkHttpClient.Create(options.ProbeUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

        await registry.Post("/registry/topology/wait")
            .Body(new TopologyReadyWaitReq(1))
            .SubmitRawAsync();
        await probe.Post("/registry/topology/wait")
            .Body(new TopologyReadyWaitReq(1))
            .SubmitRawAsync();

        var local = Normalize(await ReadJsonArrayAsync(registry, "/registry/topology"));
        var remote = Normalize(await ReadJsonArrayAsync(probe, "/registry/topology"));
        ScenarioAssert.That(
            local.Length > 0 && local.SequenceEqual(remote, StringComparer.Ordinal),
            "DR-D4 in-process and remote topology snapshots did not match.");

        Console.WriteLine("scenario DR-D4 passed");
    }

    private static async Task<JsonElement[]> ReadJsonArrayAsync(ZLinkHttpClient client, string path)
    {
        return (await client.Get(path).SubmitAsync<JsonElement[]>()).Body;
    }

    private static string[] Normalize(IEnumerable<JsonElement> entries)
    {
        return entries.Select(entry => string.Join("|",
                ReadJsonValue(entry, "channelName"),
                ReadJsonValue(entry, "routingId"),
                ReadJsonValue(entry, "endpoint"),
                ReadJsonValue(entry, "state"),
                ReadJsonValue(entry, "serviceRole")))
            .Order(StringComparer.Ordinal)
            .ToArray();
    }

    private static string ReadJsonValue(JsonElement element, string name)
    {
        return element.TryGetProperty(name, out var property)
            ? property.ToString()
            : "";
    }
}