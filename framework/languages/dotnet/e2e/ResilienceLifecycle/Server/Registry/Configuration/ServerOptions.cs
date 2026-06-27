using ResilienceLifecycle.Server.Registry.Endpoints;
using ResilienceLifecycle.Server.Registry.Handlers;
using ResilienceLifecycle.Server.Registry.Infrastructure;
using ResilienceLifecycle.Server.Registry;
namespace ResilienceLifecycle.Server.Registry.Configuration;

internal sealed record ServerOptions(
    string Role,
    string Rid,
    string HttpUrl,
    string LogDir,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    string? ChannelEndpoint,
    string? EvidenceFile,
    int Weight)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
    {
        var values = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Unexpected argument '{key}'.");
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for '{key}'.");
            }

            var value = args[++i];
            if (!values.TryGetValue(key, out var bucket))
            {
                bucket = [];
                values.Add(key, bucket);
            }

            bucket.Add(value);
        }

        string? Get(string name) => values.TryGetValue(name, out var bucket) ? bucket[^1] : null;
        return new ServerOptions(
            Role: defaultRole,
            Rid: Get("--rid") ?? "node",
            HttpUrl: Get("--http-url") ?? "http://127.0.0.1:0",
            LogDir: Get("--log-dir") ?? "logs",
            RegistryPubEndpoint: Get("--registry-pub-endpoint"),
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            ChannelEndpoint: Get("--channel-endpoint"),
            EvidenceFile: Get("--evidence-file"),
            Weight: int.TryParse(Get("--weight"), out var weight) ? weight : 100);
    }
}
