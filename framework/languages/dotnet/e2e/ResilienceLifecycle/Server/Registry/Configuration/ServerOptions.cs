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
                throw new ArgumentException($"Unexpected argument '{key}'.");

            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{key}'.");

            var value = args[++i];
            if (!values.TryGetValue(key, out var bucket))
            {
                bucket = [];
                values.Add(key, bucket);
            }

            bucket.Add(value);
        }

        string? Get(string name)
        {
            return values.TryGetValue(name, out var bucket) ? bucket[^1] : null;
        }

        return new ServerOptions(
            defaultRole,
            Get("--rid") ?? "node",
            Get("--http-url") ?? "http://127.0.0.1:0",
            Get("--log-dir") ?? "logs",
            Get("--registry-pub-endpoint"),
            Get("--registry-router-endpoint"),
            Get("--channel-endpoint"),
            Get("--evidence-file"),
            int.TryParse(Get("--weight"), out var weight) ? weight : 100);
    }
}